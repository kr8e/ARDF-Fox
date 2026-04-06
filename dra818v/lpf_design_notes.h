/*
  2m Low-Pass Filter Design — ARDF Fox KR8E
  ==========================================
  7-element Chebyshev Type I, 0.1 dB ripple
  Cutoff frequency: 160 MHz
  Impedance: 50 Ω

  This filter MUST be installed between the DRA818V RF output
  and the antenna. The DRA818V produces significant harmonics
  without filtering. FCC Part 97 requires harmonic suppression.

  Component Values (calculated for 50Ω / 160 MHz):
  ─────────────────────────────────────────────────
  C1 = 56 pF   (NP0/C0G, 50V, ±5%)
  L1 = 100 nH  (air-core or toroid, Q > 100)
  C2 = 120 pF  (NP0/C0G)
  L2 = 120 nH
  C3 = 120 pF  (NP0/C0G)
  L3 = 100 nH
  C4 = 56 pF   (NP0/C0G)

  Layout (pi-L-pi-L-pi-L-pi):
  ─────────────────────────────────────────────────
  IN ──┬── L1 ──┬── L2 ──┬── L3 ──┬── OUT
       C1       C2       C3       C4
      GND      GND      GND      GND

  Construction notes:
  ─────────────────────────────────────────────────
  - Use T50-6 toroids (Amidon/Fair-Rite) for L1–L3
  - Wind L1, L3: 7 turns #22 AWG on T50-6 → ~100 nH
  - Wind L2:     8 turns #22 AWG on T50-6 → ~120 nH
  - Alternatively use 0603 or 0805 SMD inductors (Murata LQW18A series)
  - Keep C leads as short as possible; use NP0/C0G caps only
  - Mount in a small tin/aluminium enclosure for shielding
  - SMA connectors on input and output

  Attenuation targets:
  ─────────────────────────────────────────────────
  144 MHz (fundamental):   < 0.5 dB insertion loss
  288 MHz (2nd harmonic):  > 40 dB attenuation
  432 MHz (3rd harmonic):  > 55 dB attenuation

  PCB options:
  ─────────────────────────────────────────────────
  - SV1AFN DRA818 VHF breakout (includes LPF pads)
  - W6PQL 2m LPF PCB (free Gerbers available)
  - Hand-wound on copper-clad board with SMA connectors
*/

// This file is documentation only — no code needed for the filter.
// See the build guide (ARDF_Fox_KR8E_Build_Guide.docx) for assembly photos.
