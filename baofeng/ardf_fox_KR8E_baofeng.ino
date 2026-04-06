/*
  ╔══════════════════════════════════════════════════════════════════╗
  ║   ARDF 2m Fox Transmitter — KR8E  (BaoFeng Fork)               ║
  ║   Hardware: Seeed XIAO ESP32C3 + BaoFeng HT (UV-5R / UV-82)    ║
  ║   Interface: K1 speaker-mic connector (2.5mm + 3.5mm dual plug) ║
  ║   Battery: 3.7V 18650 LiPo via XIAO BAT+ pad                   ║
  ║   Requires: ESP32 Arduino core v3.x (Espressif)                 ║
  ╚══════════════════════════════════════════════════════════════════╝

  OVERVIEW
  ──────────────────────────────────────────────────────────────────
  This fork replaces the DRA818V RF module with any BaoFeng HT that
  uses the standard K1 (Kenwood-style) dual-plug speaker-mic connector.
  Compatible radios include the UV-5R, UV-5RA, UV-5RE, BF-F8HP,
  UV-82, UV-82HP, and any other BaoFeng with the K1 port.

  The XIAO controls the radio via two signals:
    1. PTT  — pulls the 2.5mm sleeve to GND via an NPN transistor
    2. Audio — PWM tone through a voltage divider into the 2.5mm Ring (Mic+)

  No UART. No AT commands. No RF module on the board. The BaoFeng
  handles all RF, frequency selection, and power output.

  K1 CONNECTOR PINOUT (BaoFeng / Kenwood standard)
  ──────────────────────────────────────────────────────────────────
  3.5mm TRS plug:
    Tip    = Speaker +  (audio out from radio — not used)
    Ring   = RX audio   (not used)
    Sleeve = GND / PTT  <- connect to NPN collector + XIAO GND

  2.5mm TRS plug:
    Tip    = +V bias    (3-5V from radio — leave unconnected)
    Ring   = Mic +      <- audio tone input here
    Sleeve = GND / PTT  <- short to 3.5mm sleeve via NPN collector

  PTT ACTIVATION:
    NPN transistor ON -> both sleeves pulled to GND -> radio transmits.

  WIRING SUMMARY
  ──────────────────────────────────────────────────────────────────
  XIAO D6  (GPIO6)  -> 1kOhm -> NPN Base (2N3904 / BC547)
  NPN Collector     -> 2.5mm Sleeve AND 3.5mm Sleeve (tied together)
  NPN Emitter       -> XIAO GND

  XIAO D9  (GPIO9)  -> [10kOhm]--+---> 2.5mm Ring (Mic+)
                       [10kOhm to GND]

  3.5mm Sleeve      -> XIAO GND
  2.5mm Tip (+V)    -> leave unconnected (do not short to GND)

  XIAO D10 (GPIO10) -> 330Ohm -> LED -> GND  (status indicator)
  XIAO D3  (GPIO3)  -> tactile button -> GND  (test TX, pull-up enabled)
  XIAO BAT+         -> 18650 positive (protected cell)
  XIAO GND          -> 18650 negative

  IMPORTANT NOTES
  ──────────────────────────────────────────────────────────────────
  1. Set the BaoFeng to the ARDF frequency (e.g. 144.500 MHz) and
     LOW power BEFORE deploying. The XIAO cannot change the radio
     frequency — use the keypad or CHIRP.

  2. Set BaoFeng to NFM (narrow FM). VOX must be OFF.

  3. The XIAO and BaoFeng are powered INDEPENDENTLY.
     Only GND and the mic/PTT signal lines are shared.

  4. If the first element of each ID is clipped, increase PTT_LEAD_MS.

  IARU ARDF TIMING (60-second cycle)
  ──────────────────────────────────────────────────────────────────
  Fox 1 - MOE  (_._)     transmits seconds  0-60
  Fox 2 - MOI  (_.._)    transmits seconds  0-50
  Fox 3 - MOS  (_..._)   transmits seconds  0-40
  Fox 4 - MOH  (_...._)  transmits seconds  0-30
  Fox 5 - MO5  (_....._) transmits seconds  0-20
  Beacon- MO   (_-)      transmits full minute (FOX_NUMBER = 0)
*/

#include "Arduino.h"
#include "esp_sleep.h"

// ═══════════════════════════════════════════════════════════════════
//  USER CONFIGURATION
// ═══════════════════════════════════════════════════════════════════
#define FOX_NUMBER    1         // 0 = beacon MO, 1-5 = MOE/MOI/MOS/MOH/MO5
#define CALLSIGN      "KR8E"   // transmitted in CW after each fox ID
#define WPM           8        // Morse speed (6-12; 8 is IARU standard)

// Audio tone into BaoFeng mic
#define TONE_FREQ     1000     // Hz
#define TONE_RES         8     // LEDC resolution in bits
#define TONE_DUTY      128     // 50% duty cycle (out of 255 for 8-bit)

// PTT hold time before first element (ms) — allows BaoFeng to key up fully
#define PTT_LEAD_MS    200     // increase to 300-400 if first dit is clipped

// ═══════════════════════════════════════════════════════════════════
//  PIN ASSIGNMENTS (XIAO ESP32C3)
// ═══════════════════════════════════════════════════════════════════
#define PIN_PTT       6   // D6 -> 1kOhm -> NPN base -> PTT line
#define PIN_AUDIO     9   // D9 -> voltage divider -> 2.5mm Ring (Mic+)
#define PIN_LED      10   // D10 -> 330Ohm -> LED (status)
#define PIN_BUTTON    3   // D3 -> GND (test button, internal pull-up)

// ═══════════════════════════════════════════════════════════════════
//  MORSE TIMING
// ═══════════════════════════════════════════════════════════════════
#define DIT_MS       (1200 / WPM)
#define DAH_MS       (DIT_MS * 3)
#define ELEM_GAP      DIT_MS
#define CHAR_GAP     (DIT_MS * 3)
#define WORD_GAP     (DIT_MS * 7)

// Fox on-times per 60s cycle: [0]=beacon, [1]=fox1 ... [5]=fox5
const uint8_t FOX_ON_SEC[] = {60, 60, 50, 40, 30, 20};

// ═══════════════════════════════════════════════════════════════════
//  MORSE TABLE
// ═══════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════════
uint32_t cycle_count = 0;

// ═══════════════════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════════
void ardf_cycle();
void send_fox_id(uint8_t fox);
void send_string(const String& s);
void send_char(char c);
void send_dit();
void send_dah();
void ptt(bool tx);

// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n[ARDF Fox BaoFeng] KR8E booting..."));

  pinMode(PIN_PTT,    OUTPUT); digitalWrite(PIN_PTT, LOW);  // NPN off = PTT idle
  pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // ── LEDC audio (ESP32 Arduino core v3.x API) ──────────────────
  // v3.x combines ledcSetup + ledcAttachPin into a single ledcAttach call.
  // Reference the pin directly in ledcWrite — no channel number needed.
  ledcAttach(PIN_AUDIO, TONE_FREQ, TONE_RES);
  ledcWrite(PIN_AUDIO, 0);  // silent until keyed

  // Startup blink — 3 flashes confirm boot
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, HIGH); delay(150);
    digitalWrite(PIN_LED, LOW);  delay(150);
  }

  Serial.printf("[ARDF Fox BaoFeng] Fox %d  %d WPM  Callsign %s\n",
                FOX_NUMBER, WPM, CALLSIGN);
  Serial.println(F("[ARDF Fox BaoFeng] *** Set BaoFeng freq, LOW power, VOX=OFF ***"));
  Serial.println(F("[ARDF Fox BaoFeng] Starting in 5 seconds..."));
  delay(5000);
}

// ═══════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════
void loop() {
  // Test button: press to force one complete ID immediately
  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println(F("[TEST] Forcing one ID now"));
    ptt(true);
    send_fox_id((uint8_t)constrain(FOX_NUMBER, 0, 5));
    ptt(false);
    delay(500);  // debounce
    return;
  }

  ardf_cycle();
  cycle_count++;
}

// ═══════════════════════════════════════════════════════════════════
//  ARDF 60-SECOND CYCLE
// ═══════════════════════════════════════════════════════════════════
void ardf_cycle() {
  uint8_t  fox         = (uint8_t)constrain(FOX_NUMBER, 0, 5);
  uint32_t on_ms       = (uint32_t)FOX_ON_SEC[fox] * 1000UL;
  uint32_t cycle_start = millis();

  Serial.printf("[Cycle %lu] TX on for %us / 60s\n", cycle_count, FOX_ON_SEC[fox]);

  digitalWrite(PIN_LED, HIGH);
  ptt(true);

  uint32_t tx_start = millis();
  while ((millis() - tx_start) < on_ms) {
    send_fox_id(fox);
    // Brief gap between ID repeats — PTT stays held, tone goes silent
    ledcWrite(PIN_AUDIO, 0);
    delay(400);
  }

  ptt(false);
  digitalWrite(PIN_LED, LOW);

  // Light-sleep for remainder of 60s cycle to save battery
  uint32_t elapsed  = millis() - cycle_start;
  uint32_t sleep_ms = (elapsed < 60000UL) ? (60000UL - elapsed) : 0;
  if (sleep_ms > 0) {
    Serial.printf("[Cycle %lu] Sleeping %lums\n", cycle_count, sleep_ms);
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL);  // arg in microseconds
    esp_light_sleep_start();
  }
}

// ═══════════════════════════════════════════════════════════════════
//  FOX ID  —  M O [dits]  callsign
//  Fox 0 (beacon): MO followed by a dah
//  Fox 1-5:        MO followed by N dits
// ═══════════════════════════════════════════════════════════════════
void send_fox_id(uint8_t fox) {
  send_char('M'); delay(CHAR_GAP);
  send_char('O'); delay(CHAR_GAP);

  if (fox == 0) {
    send_dah();
  } else {
    for (uint8_t i = 0; i < fox; i++) {
      send_dit();
      if (i < fox - 1) delay(ELEM_GAP);
    }
  }

  delay(WORD_GAP);
  send_string(String(CALLSIGN));
}

// ═══════════════════════════════════════════════════════════════════
//  MORSE PRIMITIVES
// ═══════════════════════════════════════════════════════════════════
void send_dit() {
  ledcWrite(PIN_AUDIO, TONE_DUTY);
  delay(DIT_MS);
  ledcWrite(PIN_AUDIO, 0);
}

void send_dah() {
  ledcWrite(PIN_AUDIO, TONE_DUTY);
  delay(DAH_MS);
  ledcWrite(PIN_AUDIO, 0);
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

// ═══════════════════════════════════════════════════════════════════
//  PTT  via NPN transistor
//
//  ptt(true)  -> GPIO6 HIGH -> transistor ON  -> both sleeves = GND -> TX
//  ptt(false) -> GPIO6 LOW  -> transistor OFF -> PTT released       -> RX
// ═══════════════════════════════════════════════════════════════════
void ptt(bool tx) {
  if (tx) {
    ledcWrite(PIN_AUDIO, 0);      // ensure tone silent before keying
    digitalWrite(PIN_PTT, HIGH);  // transistor ON
    delay(PTT_LEAD_MS);           // wait for BaoFeng to fully key up
  } else {
    ledcWrite(PIN_AUDIO, 0);      // silence tone first
    delay(30);                    // let last sample drain
    digitalWrite(PIN_PTT, LOW);   // transistor OFF -> RX
    delay(50);
  }
}
