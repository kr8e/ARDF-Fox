# ARDF Fox Transmitter — KR8E

> IARU 2m fox transmitter controller for Amateur Radio Direction Finding events.  
> Two hardware variants: **DRA818V RF module** or **BaoFeng HT via K1 speaker-mic port**.  
> Built around the Seeed XIAO ESP32C3. Battery powered. Callsign: **KR8E**.

---

## Overview

This project implements a complete IARU Region 2 standard ARDF fox transmitter. The controller generates timed CW identification sequences (MOE / MOI / MOS / MOH / MO5 / beacon) on the 2m amateur band, with automatic callsign transmission and ESP32 light sleep during off-time to extend battery life.

### Two Variants

| | DRA818V Version | BaoFeng Fork |
|---|---|---|
| **RF source** | DRA818V VHF module (on-board) | BaoFeng HT via K1 speaker-mic cable |
| **Frequency control** | Firmware (`TX_FREQ` define) | BaoFeng keypad or CHIRP |
| **UART required** | Yes (D4/D5 → DRA818V) | No |
| **LPF required** | Yes (7-element, build required) | No (internal to BaoFeng) |
| **Interface components** | DRA818V module + LPF | 1× NPN transistor + 3× resistors |
| **Power output** | 0.5W or 1W (HL pin) | BaoFeng LOW power (~1W) |
| **Both batteries needed** | No — single 18650 | Yes — XIAO 18650 + BaoFeng pack |
| **Build difficulty** | Moderate | Easy |

---

## Repository Contents

```
├── dra818v/
│   ├── ardf_fox_KR8E.ino                  Firmware — DRA818V version
│   ├── lpf_design_notes.h                 7-element LPF component values
│   ├── ARDF_Fox_KR8E_Build_Guide.docx     Full assembly and wiring guide
│   ├── ARDF_Fox_KR8E_BOM.docx             Bill of materials with sources
│   └── ARDF_Fox_KR8E_Schematic.svg        Printable wiring schematic
│
├── baofeng/
│   ├── ardf_fox_KR8E_baofeng.ino          Firmware — BaoFeng fork
│   ├── ARDF_Fox_KR8E_BaoFeng_Build_Guide.docx
│   ├── ARDF_Fox_KR8E_BaoFeng_BOM.docx
│   └── ARDF_Fox_KR8E_BaoFeng_Schematic.svg
│
└── README.md
```

---

## Hardware

### Common to both variants

- **Seeed XIAO ESP32C3** — [Seeed Studio SKU 113991054](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html)
  - ESP32-C3 RISC-V, 160 MHz, 4MB flash
  - Built-in MCP73831 LiPo charger (500 mA via USB-C)
  - 3.3V regulated output, 11× GPIO
- **18650 LiPo cell** (protected, 2500–3500 mAh) connected to XIAO BAT+/GND pads
- Status LED (D10 / GPIO10, 330Ω series)
- Test button (D3 / GPIO3 → GND, internal pull-up)

### DRA818V variant

- **DRA818V VHF transceiver module** — 134–174 MHz, up to 1W, 3.3V, UART control
- 7-element Chebyshev low-pass filter (see `lpf_design_notes.h` for values)
- SMA connectors, RG316 coax patch between DRA818V and LPF

### BaoFeng variant

- **BaoFeng UV-5R, UV-82, BF-F8HP**, or any K1 dual-port model (see compatibility table below)
- **2N3904 NPN transistor** (or BC547 / 2N2222) for PTT switching
- BaoFeng K1 speaker-mic cable (donor cable — harvest the dual plug end)

---

## Wiring

### DRA818V version

```
XIAO D4  (GPIO4)  ──────────→ DRA818V RXD      UART1 TX (config)
XIAO D5  (GPIO5)  ←────────── DRA818V TXD      UART1 RX (config)
XIAO D6  (GPIO6)  ──────────→ DRA818V PTT      LOW = transmit
XIAO D7  (GPIO7)  ──────────→ DRA818V PD       LOW = sleep
XIAO D9  (GPIO9)  ─[10kΩ]─┬─→ DRA818V MIC     audio tone
                   [10kΩ]─GND
XIAO D2  (GPIO2)  ──────────→ DRA818V HL       HIGH = 0.5W, LOW = 1W
XIAO 3V3          ──────────→ DRA818V VCC
XIAO GND          ──────────→ DRA818V GND
DRA818V RF_OUT    ──────────→ LPF → SMA → Antenna
18650 BAT+        ──────────→ XIAO BAT+ pad
```

> **Do not use D6 for UART** — it is connected to U0TXD and outputs boot messages at startup.

### BaoFeng variant — K1 pinout

| Plug | Contact | Signal | Connection |
|------|---------|--------|------------|
| 3.5mm | Sleeve | GND / PTT | NPN collector + XIAO GND |
| 2.5mm | Ring | Mic + | Audio divider output |
| 2.5mm | Sleeve | GND / PTT | NPN collector (tied to 3.5mm sleeve) |
| 2.5mm | Tip | +V bias | **Leave unconnected** |
| 3.5mm | Tip | Speaker + | Not connected |
| 3.5mm | Ring | RX audio | Not connected |

```
XIAO D6 (GPIO6) ──[1kΩ]──→ NPN Base
NPN Collector   ──────────→ 2.5mm Sleeve + 3.5mm Sleeve (tied together)
NPN Emitter     ──────────→ XIAO GND

XIAO D9 (GPIO9) ──[10kΩ]──┬──→ 2.5mm Ring (Mic+)
                           [10kΩ]
                            GND
```

PTT activates when the NPN transistor pulls both sleeve contacts to GND simultaneously.

---

## Firmware

### Requirements

- Arduino IDE 2.x
- **ESP32 by Espressif Systems v3.x** (install via Board Manager)
  - Board Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Board selection: `Tools → Board → ESP32 Arduino → XIAO_ESP32C3`

> **Core v3.x required.** This firmware uses the updated LEDC API introduced in ESP32 Arduino core v3.0:
> `ledcAttach(pin, freq, resolution)` and `ledcWrite(pin, duty)`.  
> The older v2.x calls `ledcSetup` / `ledcAttachPin` / channel-based `ledcWrite` were removed in v3.x and will produce compile errors.

### Configuration

Edit the defines at the top of either `.ino` file before flashing:

```cpp
#define FOX_NUMBER    1         // 0 = beacon, 1–5 = MOE/MOI/MOS/MOH/MO5
#define CALLSIGN      "KR8E"   // your callsign — transmitted after every ID
#define WPM           8        // Morse speed; 8 WPM is IARU standard

// DRA818V version only:
#define TX_FREQ       144.500  // MHz
#define TX_POWER_LOW  true     // true = 0.5W, false = 1W

// BaoFeng version only:
#define PTT_LEAD_MS   200      // ms PTT hold before tone starts
```

### Flashing

1. Connect XIAO via USB-C
2. Select the correct port in Arduino IDE
3. Click Upload
4. If upload fails: hold BOOT, press RESET, release BOOT, then retry
5. Open Serial Monitor at **115200 baud** to observe startup and confirm DRA818V handshake (DRA818V version)

---

## IARU ARDF Timing

Standard IARU Region 2 two-meter fox timing. Each fox transmits within its assigned window of a 60-second cycle, then the XIAO enters light sleep for the remainder.

| Fox | CW ID | Transmits | Sleeps |
|-----|-------|-----------|--------|
| Fox 1 | MOE `– · –` | 0 – 60 s | — |
| Fox 2 | MOI `– · · –` | 0 – 50 s | 50 – 60 s |
| Fox 3 | MOS `– · · · –` | 0 – 40 s | 40 – 60 s |
| Fox 4 | MOH `– · · · · –` | 0 – 30 s | 30 – 60 s |
| Fox 5 | MO5 `– · · · · · –` | 0 – 20 s | 20 – 60 s |
| Beacon | MO `– –` | 0 – 60 s | — |

Each transmission ends with callsign **KR8E** in CW at 8 WPM.

> Without an RTC module each fox runs a free-running 60-second cycle from power-on. To synchronize multiple foxes to wall-clock time, add a DS3231 RTC on I2C (D4/D5) and modify `ardf_cycle()` to align to absolute seconds.

---

## Battery Life

### DRA818V version — single 18650, 2500 mAh cell

| Fox | TX duty | Runtime at 0.5W |
|-----|---------|-----------------|
| Fox 1 | 100% | ~8–10 hours |
| Fox 2 | 83% | ~9–11 hours |
| Fox 3 | 67% | ~11–13 hours |
| Fox 4 | 50% | ~13–16 hours |
| Fox 5 | 33% | ~16–20 hours |

ESP32C3 light sleep (~800 µA) and DRA818V power-down (~1 mA) during off-time.

### BaoFeng variant

The XIAO 18650 is effectively unlimited (~10 mA average). Runtime is set by the BaoFeng pack:

| Battery | Power | Runtime (Fox 1 duty) |
|---------|-------|----------------------|
| BL-5 standard (1800 mAh) | LOW (1W) | ~8–10 hours |
| BL-5 extended (2800 mAh) | LOW (1W) | ~12–15 hours |
| BL-8 UV-82 (2800 mAh) | LOW (1W) | ~12–16 hours |

Always use **LOW power** on the BaoFeng — adequate range for ARDF, significantly better battery life. Carry a spare pack for all-day events.

---

## BaoFeng Compatibility

| Model | K1 Port | Compatible |
|-------|---------|------------|
| UV-5R / UV-5RA / UV-5RE | 2.5mm + 3.5mm | ✅ |
| BF-F8HP | 2.5mm + 3.5mm | ✅ |
| UV-5R V2 / V3 | 2.5mm + 3.5mm | ✅ |
| UV-82 / UV-82HP / UV-82C | 2.5mm + 3.5mm | ✅ |
| UV-9R Plus (IP67) | 2.5mm + 3.5mm | ✅ |
| GT-3 / GT-3TP | 2.5mm + 3.5mm | ✅ |
| **BF-888S** | Single 3.5mm only | ❌ Incompatible |

> BaoFeng jacks are recessed. Use **slim-body plugs** — plugs with thick insulation collars may not seat fully, causing intermittent PTT.

---

## LPF — DRA818V version only

A low-pass filter is **required** between the DRA818V and the antenna. Unfiltered output contains harmonics at 288 MHz, 432 MHz, etc. which violate FCC Part 97 spurious emission limits.

**7-element Chebyshev Type I, 50Ω, cutoff 160 MHz:**

```
IN ──┬──[L1 100nH]──┬──[L2 120nH]──┬──[L3 100nH]──┬── OUT
    [C1 56pF]      [C2 120pF]     [C3 120pF]     [C4 56pF]
     GND            GND            GND            GND
```

| Ref | Value | Type | Notes |
|-----|-------|------|-------|
| C1, C4 | 56 pF | NP0/C0G 50V | Mouser 810-GRM2195C1H560JA01D |
| C2, C3 | 120 pF | NP0/C0G 50V | Mouser 810-GRM2195C1H121JA01D |
| L1, L3 | ~100 nH | Toroid | 7T #22 AWG on Amidon T50-6 |
| L2 | ~120 nH | Toroid | 8T #22 AWG on Amidon T50-6 |

Mount in a shielded enclosure with SMA connectors. Full construction notes in `lpf_design_notes.h`.

---

## Troubleshooting

**`ledcSetup` / `ledcAttachPin` compile errors**
Upgrade to ESP32 Arduino core v3.x. These functions were removed in v3.0.

**DRA818V handshake fails at startup**
- Check UART wiring: XIAO D4 (TX) → DRA818V RXD; XIAO D5 (RX) ← DRA818V TXD
- Verify DRA818V VCC = 3.3V and PD pin is HIGH
- The firmware retries 5 times with 2-second delays — watch Serial Monitor
- Replace the module if all retries fail — DOA modules do occur

**BaoFeng does not key up**
- Measure NPN collector voltage during PTT: should be < 0.2V when ON
- Verify 3.5mm sleeve and 2.5mm sleeve are both wired to the collector
- Check 1kΩ base resistor is in place between D6 and NPN base

**First CW element clipped (BaoFeng)**
- Increase `PTT_LEAD_MS` from 200 to 300 or 400 ms

**No audio tone on receiver**
- Verify divider: D9 → 10kΩ → junction → Mic+, with 10kΩ from junction to GND
- Try 4.7kΩ for the lower resistor to increase audio level
- Confirm BaoFeng VOX is OFF

---

## Regulatory

This transmitter operates under **FCC Part 97** (Amateur Radio Service).

- A valid FCC amateur radio licence is required to operate
- Callsign **KR8E** is transmitted automatically after every fox ID sequence
- Confirm the event frequency with the organiser before programming
- The LPF must be installed before on-air use (DRA818V version)
- The BaoFeng UV-5R series is FCC type-accepted; no external LPF required (BaoFeng version)

---

## Licence

Released under the [MIT License](LICENSE).

All construction and on-air operation is at the builder's own risk. The builder is solely responsible for compliance with all applicable regulations, including holding a valid amateur radio licence and operating within authorised frequency allocations and power limits.
