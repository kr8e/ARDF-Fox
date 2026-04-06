/*
  ╔══════════════════════════════════════════════════════════════╗
  ║   ARDF 2m Fox Transmitter — KR8E                            ║
  ║   Hardware: Seeed XIAO ESP32C3 + DRA818V                    ║
  ║   Battery: 3.7V 18650 LiPo via XIAO BAT+ pad               ║
  ╚══════════════════════════════════════════════════════════════╝

  WIRING SUMMARY
  ──────────────────────────────────────────────────────────────
  XIAO D4  (GPIO4)  ──────────────────→ DRA818V RXD  (config UART in)
  XIAO D5  (GPIO5)  ←────────────────── DRA818V TXD  (config UART out)
  XIAO D6  (GPIO6)  ──────────────────→ DRA818V PTT  (LOW = transmit)
  XIAO D7  (GPIO7)  ──────────────────→ DRA818V PD   (LOW = sleep)
  XIAO D9  (GPIO9)  ──[10kΩ]──┬──────→ DRA818V MIC
                     [10kΩ] to GND
  XIAO D10 (GPIO10) ──────────────────→ LED status indicator (+ 330Ω to GND)
  XIAO D3  (GPIO3)  ←────────────────── Tactile button (other end to GND)
  XIAO 3V3          ──────────────────→ DRA818V VCC
  XIAO GND          ──────────────────→ DRA818V GND
  DRA818V RF out    ──→ 7-element LPF ──→ SMA antenna connector

  BATTERY
  ──────────────────────────────────────────────────────────────
  3.7V 18650 cell (2000–3500 mAh) connected to XIAO BAT+ / GND pads.
  XIAO ESP32C3 has on-board LiPo charge circuit (MCP73831, 500mA).
  Charging via USB-C while field deployed is supported.

  IARU 2m ARDF TIMING (60-second cycle)
  ──────────────────────────────────────────────────────────────
  Fox 1 — MOE  (_._)   transmits seconds  0–60 of each minute
  Fox 2 — MOI  (_.._)  transmits seconds  0–50
  Fox 3 — MOS  (_..._) transmits seconds  0–40
  Fox 4 — MOH  (_...._)transmits seconds  0–30
  Fox 5 — MO5  (_....._)transmits seconds 0–20
  Beacon — MO  (_-)    transmits full minute (FOX_NUMBER = 0)

  NOTE: A valid FCC amateur radio licence is required to operate.
        KR8E — authorised operator.
*/

#include "Arduino.h"
#include "esp_sleep.h"

// ═══════════════════════════════════════════════════════════════
//  USER CONFIGURATION  — edit these before building
// ═══════════════════════════════════════════════════════════════
#define FOX_NUMBER   1          // 0 = beacon, 1–5 = MOE/MOI/MOS/MOH/MO5
#define TX_FREQ      144.500    // MHz — IARU Region 2 ARDF common freq
#define CALLSIGN     "KR8E"     // sent in CW at end of each ID sequence
#define WPM          8          // 6–12 for ARDF; 8 is standard
#define TX_POWER_LOW true       // true = 0.5W (HL=HIGH), false = 1W (HL=LOW)
                                // 0.5W is plenty for 80m fox range; saves battery

// ═══════════════════════════════════════════════════════════════
//  PIN ASSIGNMENTS (XIAO ESP32C3)
// ═══════════════════════════════════════════════════════════════
#define PIN_DRA_TX    4   // D4 — UART1 TX → DRA818V RXD
#define PIN_DRA_RX    5   // D5 — UART1 RX ← DRA818V TXD
#define PIN_PTT       6   // D6 — PTT (LOW = transmit)
#define PIN_SLP       7   // D7 — PD/Sleep (LOW = power down)
#define PIN_AUDIO     9   // D9 — PWM tone → voltage divider → MIC
#define PIN_LED      10   // D10 — status LED (active high)
#define PIN_BUTTON    3   // D3 — tactile button (active low, internal pull-up)
#define PIN_HL        2   // D2 — DRA818V HL power select (HIGH=0.5W, LOW=1W)

// ═══════════════════════════════════════════════════════════════
//  AUDIO
// ═══════════════════════════════════════════════════════════════
#define TONE_FREQ    1000   // Hz — CW sidetone frequency
#define TONE_CH         0   // LEDC channel (0–7)
#define TONE_RES        8   // bits resolution
#define TONE_DUTY     128   // 50% duty cycle (out of 255)

// ═══════════════════════════════════════════════════════════════
//  MORSE TIMING
// ═══════════════════════════════════════════════════════════════
#define DIT_MS       (1200 / WPM)
#define DAH_MS       (DIT_MS * 3)
#define ELEM_GAP     DIT_MS          // gap between elements within a character
#define CHAR_GAP     (DIT_MS * 3)    // gap between characters (minus elem_gap already elapsed)
#define WORD_GAP     (DIT_MS * 7)    // gap between words

// Fox on-times (seconds per 60s cycle): index 0=beacon, 1=fox1 … 5=fox5
const uint8_t FOX_ON_SEC[] = {60, 60, 50, 40, 30, 20};

// ═══════════════════════════════════════════════════════════════
//  MORSE TABLE
// ═══════════════════════════════════════════════════════════════
struct MorseChar { char c; const char* code; };
const MorseChar MORSE[] = {
  {'A',".-"},    {'B',"-..."},  {'C',"-.-."},  {'D',"-.."},
  {'E',"."},     {'F',"..-."},  {'G',"--."},   {'H',"...."},
  {'I',".."},    {'J',".---"},  {'K',"-.-"},   {'L',".-.."},
  {'M',"--"},    {'N',"-."},    {'O',"---"},   {'P',".--."},
  {'Q',"--.-"},  {'R',".-."},   {'S',"..."},   {'T',"-"},
  {'U',"..-"},   {'V',"...-"},  {'W',".--"},   {'X',"-..-"},
  {'Y',"-.--"},  {'Z',"--.."},
  {'0',"-----"}, {'1',".----"}, {'2',"..---"}, {'3',"...--"},
  {'4',"....-"}, {'5',"....."},  {'6',"-...."},{'7',"--..."},
  {'8',"---.."}, {'9',"----."},
  {'/', "-..-."},{'?', "..--.."},
  {0, 0}
};

// ═══════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════
HardwareSerial DraSerial(1);   // UART1 for DRA818V
bool dra_ok = false;
uint32_t cycle_count = 0;

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n[ARDF Fox] KR8E booting..."));

  // Output pins
  pinMode(PIN_PTT,    OUTPUT); digitalWrite(PIN_PTT, HIGH);   // idle = RX
  pinMode(PIN_SLP,    OUTPUT); digitalWrite(PIN_SLP, HIGH);   // idle = active
  pinMode(PIN_HL,     OUTPUT); digitalWrite(PIN_HL, TX_POWER_LOW ? HIGH : LOW);
  pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // LEDC audio
  ledcSetup(TONE_CH, TONE_FREQ, TONE_RES);
  ledcAttachPin(PIN_AUDIO, TONE_CH);
  ledcWrite(TONE_CH, 0);

  // DRA818V UART — UART1, 9600 baud
  DraSerial.begin(9600, SERIAL_8N1, PIN_DRA_RX, PIN_DRA_TX);
  delay(2000);  // DRA818V startup time

  // Blink LED to show we're alive
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, HIGH); delay(100);
    digitalWrite(PIN_LED, LOW);  delay(100);
  }

  dra_ok = dra818_init();

  if (dra_ok) {
    Serial.printf("[ARDF Fox] Ready — Fox %d, %.4f MHz, %d WPM, %s\n",
                  FOX_NUMBER, TX_FREQ, WPM,
                  TX_POWER_LOW ? "0.5W" : "1W");
    // Confirmation: send callsign once in CW (no RF, just blink LED)
    Serial.println(F("[ARDF Fox] Starting in 3 seconds..."));
    delay(3000);
  } else {
    Serial.println(F("[ARDF Fox] DRA818V init FAILED — check wiring!"));
    // Fast blink indefinitely on failure
    while (true) {
      digitalWrite(PIN_LED, HIGH); delay(50);
      digitalWrite(PIN_LED, LOW);  delay(50);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  // Allow button press to skip to next fox slot for testing
  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println(F("[TEST] Button pressed — sending one ID now"));
    dra818_sleep(false); delay(50);
    ptt(true);
    send_fox_id(FOX_NUMBER);
    ptt(false);
    dra818_sleep(true);
    delay(500);  // debounce
    return;
  }

  ardf_cycle();
  cycle_count++;
}

// ═══════════════════════════════════════════════════════════════
//  ARDF 60-SECOND CYCLE
// ═══════════════════════════════════════════════════════════════
void ardf_cycle() {
  uint8_t fox   = (uint8_t)constrain(FOX_NUMBER, 0, 5);
  uint32_t on_ms = (uint32_t)FOX_ON_SEC[fox] * 1000UL;
  uint32_t cycle_start = millis();

  Serial.printf("[Cycle %lu] TX on for %us\n", cycle_count, FOX_ON_SEC[fox]);

  // Wake radio
  dra818_sleep(false);
  delay(50);

  // LED on during TX
  digitalWrite(PIN_LED, HIGH);

  uint32_t tx_start = millis();
  ptt(true);

  while ((millis() - tx_start) < on_ms) {
    send_fox_id(fox);
    // Brief un-keyed gap between ID repeats (~300ms)
    ledcWrite(TONE_CH, 0);
    delay(300);
  }

  ptt(false);
  digitalWrite(PIN_LED, LOW);

  // Sleep radio for remainder of cycle
  dra818_sleep(true);

  uint32_t elapsed = millis() - cycle_start;
  uint32_t sleep_ms = (elapsed < 60000UL) ? (60000UL - elapsed) : 0;

  if (sleep_ms > 0) {
    Serial.printf("[Cycle %lu] Sleeping %lums\n", cycle_count, sleep_ms);
    // Use ESP32 light sleep to save battery during off-time
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL);  // microseconds
    esp_light_sleep_start();
  }
}

// ═══════════════════════════════════════════════════════════════
//  FOX ID TRANSMIT
//  Standard IARU: M O [dits for fox number]  callsign
// ═══════════════════════════════════════════════════════════════
void send_fox_id(uint8_t fox) {
  send_char('M');  delay(CHAR_GAP);
  send_char('O');  delay(CHAR_GAP);

  // Suffix dits: fox 1=1 dit, fox 2=2 dits, etc.; fox 0 = dash (beacon)
  if (fox == 0) {
    send_dah();
  } else {
    for (uint8_t i = 0; i < fox; i++) {
      send_dit();
      if (i < fox - 1) delay(ELEM_GAP);
    }
  }

  // Gap then callsign
  delay(WORD_GAP);
  send_string(String(CALLSIGN));
}

// ═══════════════════════════════════════════════════════════════
//  MORSE PRIMITIVES
// ═══════════════════════════════════════════════════════════════
void send_dit() {
  ledcWrite(TONE_CH, TONE_DUTY);  delay(DIT_MS);
  ledcWrite(TONE_CH, 0);
}

void send_dah() {
  ledcWrite(TONE_CH, TONE_DUTY);  delay(DAH_MS);
  ledcWrite(TONE_CH, 0);
}

void send_char(char c) {
  c = toupper(c);
  if (c == ' ') { delay(WORD_GAP); return; }
  for (const MorseChar* m = MORSE; m->c; m++) {
    if (m->c == c) {
      for (const char* p = m->code; *p; p++) {
        if (*p == '.') send_dit(); else send_dah();
        if (*(p + 1)) delay(ELEM_GAP);
      }
      return;
    }
  }
}

void send_string(const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    send_char(s[i]);
    if (i < s.length() - 1) delay(CHAR_GAP);
  }
}

// ═══════════════════════════════════════════════════════════════
//  PTT
// ═══════════════════════════════════════════════════════════════
void ptt(bool tx) {
  if (!tx) ledcWrite(TONE_CH, 0);
  digitalWrite(PIN_PTT, tx ? LOW : HIGH);  // DRA818V PTT = active LOW
  delay(5);  // PTT settling time
}

// ═══════════════════════════════════════════════════════════════
//  DRA818V CONTROL
// ═══════════════════════════════════════════════════════════════
void dra818_sleep(bool sleep) {
  // PD: LOW = power down, HIGH = normal operation
  digitalWrite(PIN_SLP, sleep ? LOW : HIGH);
  if (sleep) delay(10); else delay(50);  // settling
}

bool dra818_init() {
  bool ok = false;

  // Retry handshake up to 5 times
  for (int attempt = 0; attempt < 5 && !ok; attempt++) {
    Serial.printf("[DRA818] Handshake attempt %d...\n", attempt + 1);
    ok = dra818_handshake();
    if (!ok) delay(2000);
  }
  if (!ok) return false;

  // Set frequency group (BW=narrow, no CTCSS, squelch=1)
  if (!dra818_setgroup(TX_FREQ, 1, 0)) {
    Serial.println(F("[DRA818] SETGROUP failed!"));
    return false;
  }

  // Disable all audio processing (pre-emphasis, HPF, LPF) for clean CW tone
  dra818_setfilter(false, false, false);

  // Set RF power via HL pin (already set in setup(), confirm here)
  digitalWrite(PIN_HL, TX_POWER_LOW ? HIGH : LOW);

  return true;
}

bool dra818_handshake() {
  // Flush buffer
  while (DraSerial.available()) DraSerial.read();

  DraSerial.print("AT+DMOCONNECT\r\n");
  unsigned long t = millis();
  String resp = "";
  while (millis() - t < 2500) {
    while (DraSerial.available()) resp += (char)DraSerial.read();
    if (resp.indexOf("+DMOCONNECT:0") >= 0) {
      Serial.println(F("[DRA818] Handshake OK"));
      return true;
    }
  }
  Serial.print(F("[DRA818] No response: "));
  Serial.println(resp);
  return false;
}

bool dra818_setgroup(float freq, uint8_t sq, uint16_t ctcss) {
  // AT+DMOSETGROUP=BW,TXF,RXF,TX_CTCSS,SQ,RX_CTCSS
  // BW: 0 = 12.5 kHz narrow  (use narrow for ARDF)
  char buf[72];
  snprintf(buf, sizeof(buf),
           "AT+DMOSETGROUP=0,%.4f,%.4f,%04u,%u,%04u\r\n",
           freq, freq, ctcss, sq, ctcss);
  DraSerial.print(buf);
  Serial.printf("[DRA818] Cmd: %s", buf);

  unsigned long t = millis();
  String resp = "";
  while (millis() - t < 2500) {
    while (DraSerial.available()) resp += (char)DraSerial.read();
    if (resp.indexOf("+DMOSETGROUP:0") >= 0) {
      Serial.println(F("[DRA818] SETGROUP OK"));
      return true;
    }
  }
  Serial.print(F("[DRA818] SETGROUP response: "));
  Serial.println(resp);
  return false;
}

void dra818_setfilter(bool preemph, bool highpass, bool lowpass) {
  // Parameters are inverted in AT command: 0 = ON, 1 = OFF
  char buf[32];
  snprintf(buf, sizeof(buf), "AT+SETFILTER=%d,%d,%d\r\n",
           preemph  ? 0 : 1,
           highpass ? 0 : 1,
           lowpass  ? 0 : 1);
  DraSerial.print(buf);
  delay(200);
  while (DraSerial.available()) DraSerial.read();
}
