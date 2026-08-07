# Changelog

## v1.0 — 2026-08-06

First release.

### Detection
- Eleven integrity checks across four independent measurement paths: position,
  carrier, geometry and time.
- Corroboration counted in families rather than checks, so three carrier checks
  reading the same C/N0 numbers count once.
- No single check can reach the top verdict on its own; the score is capped at
  96. Both properties are asserted by the test suite rather than hoped for.
- Flags latch for a configurable hold (15 s to 5 min) so a one-epoch event is
  still on screen when you look up, then decay.
- Jamming detected and reported separately from spoofing, with the two carrier
  shape checks standing down below 25 dB-Hz mean so a raised noise floor is
  never read as a fake sky.
- Low / Normal / High sensitivity scales the statistical thresholds; absolute
  carrier power shifts by a fixed decibel offset instead.

### Input
- NMEA 0183 parser: GGA, RMC, GSA, GSV, GLL and VTG, with NMEA 4.10 unified
  satellite numbering so combined-talker sentences resolve to the right
  constellation.
- GPS, GLONASS, Galileo and BeiDou, up to 32 satellites tracked at once.
- Checksums verified when present, accepted when absent, counted either way.
- USART (GPIO 13/14) or LPUART (15/16) at 9600, 38400, 57600 or 115200 baud.
- Receive only. There is no transmit path compiled in.

### Screens
- **Monitor** — arc gauge, verdict, and an eleven-cell check strip that reads
  as a barcode of system health.
- **Sky** — polar satellite plot sized by C/N0, and a carrier bar chart with
  mean, standard deviation and elevation correlation.
- **Drift** — the last 64 fixes plotted around their mean, auto-scaled.
- **Evidence** — every check with its state and history; OK opens a card giving
  the reading, what a real sky does, and the innocent explanation.
- **How spoofing works** — six animated frames, ending on what the app cannot
  tell you.
- **Wiring** — drawn pinout with the TX/RX crossover, plus the last sentence
  actually received.

### Demo
- Seven scenarios driving the real parser and the real engine at 4x speed:
  open sky, driving, held in place, carried off, clock pushed, lock captured
  and jammed. Attack scenarios run clean for eight seconds first, so the
  verdict can be watched turning over.

### Tests
- 252 host checks across the parser and the engine.
- Parser validated against published NMEA 0183 example sentences with their
  original checksums intact, plus malformed and oversized input fuzzing.
- Engine asserted in both directions: every attack caught by the specific check
  that should notice it, and every honest scenario silent - including at
  maximum sensitivity, and including a moving receiver.
