# The checks, and the claims behind them

Every threshold in Meridian is a claim about the physical world. This document
states each one, the number it is set to, and the reasoning it rests on — so
that if a number is wrong, there is somewhere to argue with it.

The implementation is [`helpers/mrd_detect.c`](helpers/mrd_detect.c). The
constants below are the ones defined at the top of that file, and they are
exercised by [`test/host_detect_test.c`](test/host_detect_test.c).

---

## Why any of this is necessary

The civil GPS L1 C/A signal is defined by IS-GPS-200. The spreading code for
every satellite is published in that document. The navigation message carries no
signature. Received power at the surface is specified at a minimum of
**−158.5 dBW** — about −128.5 dBm — which is roughly 20 dB *below* the thermal
noise floor in the same bandwidth. A receiver recovers it only by correlating
against a code it already knows.

Three properties follow, and together they are the whole problem:

1. Anyone can generate a structurally valid signal, because the codes are public.
2. Nothing in the signal proves who generated it, because nothing is signed.
3. A counterfeit only has to be **slightly louder** than a signal that arrived
   below the noise floor.

There is no bit in NMEA that means "this might not be real". So a detector
cannot ask the receiver whether it is being lied to. It has to watch for the
things a real sky does that a transmitter has to imitate — and imitating all of
them at once is significantly harder than transmitting a convincing position.

---

## Family: Position

### Impossible motion

| | |
|---|---|
| **Fires when** | implied ground speed between two consecutive locked fixes |
| **WARN** | > 120 m/s (432 km/h) |
| **ALERT** | > 340 m/s (Mach 1 at sea level) |
| **Requires** | both epochs valid, gap ≤ 10 s |

120 m/s is past any road vehicle and most light aircraft; 340 m/s is the speed of
sound. A spoofer taking over a receiver that is already locked has to move it
from the true position to the false one, and unless it walks the fix across
gradually, that transition happens in a single epoch.

The continuity requirement matters: a receiver's *first* fixes after a cold start
can land a long way from its second, which is not an attack. Only two
consecutive locked epochs count.

### Speed mismatch

| | |
|---|---|
| **Fires when** | \|Doppler speed − position-derived speed\| |
| **WARN** | > 3 m/s, sustained 3 epochs |
| **ALERT** | > 6 m/s, sustained 3 epochs |
| **Requires** | RMC/VTG speed present, gap ≤ 3 s |

This is the most interesting check in the set, because it compares **two
independent measurements of the same quantity**. Speed over ground is derived
from carrier Doppler, not from differencing positions — different signal path,
different maths. On an honest receiver they agree to well under 1 m/s.

A simulator replaying a canned track frequently forgets to keep them in step,
which is why the *Carried off* demo scenario is caught by this check and by
nothing else in the position family: at 9 m/s no single position step is
remarkable.

Position noise alone puts around 0.5 m/s of jitter on the derived figure when
stationary, so the bar sits well clear of it and requires three consecutive
epochs.

### Position frozen

| | |
|---|---|
| **Fires when** | consecutive fixes are **bit-identical**, over a 16-epoch window |
| **WARN** | ≥ 10 of 16 |
| **ALERT** | ≥ 14 of 16 |
| **Requires** | ≥ 4 satellites in the solution |

A position is a *computed* quantity. Thermal noise, multipath and satellites
drifting through the solution move a stationary fix by a metre or two every
second. Coordinates repeating to the last emitted digit mean the number is being
recited rather than solved.

This is why the parser guarantees that identical input text produces a
bit-identical `double`, and why coordinates are stored in `double` rather than
`float` — a float quantises latitude to about 0.3 m at mid latitudes, which would
make genuinely different fixes compare equal and fire this check falsely.

**Innocent cause:** many receivers ship with static-hold or position-pinning
enabled, which freezes output deliberately when the module decides you are
stationary. Check your module's configuration before concluding anything.

### Altitude anomaly

| | |
|---|---|
| **WARN** | step > 200 m between epochs ≤ 5 s apart, **or** exactly 0.0 m for 20 epochs with a 3D fix |
| **ALERT** | step > 1000 m |

Height is the weakest axis of a GPS solution and the one simulators most often
neglect — pinned at zero, or stepping between seconds. 2D fixes report a fixed or
absent altitude by design, so the pinned-zero test only runs when the receiver
claims a 3D fix.

### Lock captured

| | |
|---|---|
| **Fires when** | the fix returns after an outage ≥ 2 s, judged by implied speed across the gap |
| **WARN** | > 45 m/s |
| **ALERT** | > 90 m/s (324 km/h) |

The textbook takeover: suppress the receiver until it loses lock, then hand it a
stronger fake. Deliberately judged as **speed across the outage** rather than
distance, so that genuinely driving through a tunnel does not fire it — you can
cover a kilometre in a 30-second tunnel at motorway speed, but not at 324 km/h.

Distinct from *Impossible motion*, which requires continuity and therefore cannot
see this at all.

---

## Family: Carrier

These three all read the same C/N0 numbers, which is exactly why they count as
**one** family for corroboration purposes.

All of them stand down when the mean drops below **25 dB-Hz**. Below that the
constellation is in the noise and the shape of the distribution is a measurement
of thermal noise rather than of anything an attacker did. This is what stops a
jammer being reported as a spoofer.

### Flat carrier power

| | |
|---|---|
| **Fires when** | population σ of C/N0 across tracked satellites |
| **WARN** | < 2.0 dB |
| **ALERT** | < 1.0 dB |
| **Requires** | ≥ 5 tracked satellites, mean ≥ 25 dB-Hz |

An open-sky constellation spans roughly 25 dB-Hz at the horizon to 48 at the
zenith, so σ normally lands between 4 and 10 dB. One transmitter generating every
channel from one power amplifier flattens it to nothing.

### Carrier too strong

| | |
|---|---|
| **Fires when** | mean C/N0 across tracked satellites |
| **WARN** | > 47 dB-Hz |
| **ALERT** | > 50 dB-Hz |
| **Requires** | ≥ 4 tracked satellites |

A passive patch antenna peaks near 48–50 dB-Hz on the single highest satellite. A
*mean* above 50 across the whole constellation is not something open sky
delivers. To beat the real signal, a close spoofer has to arrive hotter.

This is the one threshold sensitivity shifts by a fixed **±1.5 dB offset** rather
than scaling: 50 dB-Hz is 50 dB-Hz however paranoid you are feeling, and what the
setting expresses is how much allowance to make for a high-gain active antenna.

**Innocent cause:** an active antenna with a good LNA genuinely reports higher
C/N0. If yours does, expect this one to sit warm and read the others instead.

### Power vs elevation

| | |
|---|---|
| **Fires when** | Pearson r between elevation and C/N0 |
| **WARN** | r < −0.05 |
| **ALERT** | r < −0.35 |
| **Requires** | ≥ 6 tracked, elevation span ≥ 30°, mean ≥ 25 dB-Hz |

High satellites travel through less atmosphere and suffer less ground multipath,
so power climbs with elevation: r is typically +0.4 to +0.8. A spoofer transmits
from one point on the ground, so every fake satellite shares one real geometry
and the relationship disappears — or inverts, because the source is near the
horizon.

The bar is set *below* zero rather than at it, and the elevation-span requirement
exists because a handful of satellites bunched at one elevation says nothing
about how power varies with elevation.

**Innocent cause:** dense urban multipath, a tilted antenna, or a metal roof over
half the sky can flatten or invert this without any attacker involved.

---

## Family: Geometry

### Sky not moving

| | |
|---|---|
| **Fires when** | satellites whose integer elevation changed over a 4-minute window |
| **WARN** | fewer than 25% moved |
| **ALERT** | **none** moved |
| **Requires** | ≥ 6 satellites surviving the comparison |

A GPS satellite crosses the sky in a few hours. Apparent elevation moves fastest
low down — well over 1°/min — and slowest at culmination. GSV reports elevation
as whole degrees, so a satellite near its highest point can genuinely hold the
same integer for minutes.

That is exactly why the ALERT condition is **zero of six or more**, not "some
satellite did not move". Across six satellites spread over the sky, at least one
is always climbing or setting fast enough to cross a degree boundary in four
minutes. A canned sky holds still.

Because this check only produces a verdict once per window, it latches for a
whole window plus slack rather than the usual hold period.

### Accuracy implausible

| | |
|---|---|
| **WARN** | HDOP < 0.40 |
| **ALERT** | HDOP unchanged for 12 epochs while satellites are in the fix |

Dilution of precision follows from satellite geometry, so it changes as the sky
does. A value below 0.4 is a claim the geometry cannot support on consumer
hardware.

The repeated-value test is the stronger of the two: modern multi-GNSS receivers
legitimately reach 0.5 with a full sky, but a DOP that never moves while the
satellite count does was not computed from anything.

---

## Family: Time

### Clock inconsistent

| | |
|---|---|
| **ALERT** | receiver time runs backwards |
| **ALERT** | per-epoch disagreement > 2000 ms |
| **WARN** | accumulated drift > 5 s |
| **ALERT** | accumulated drift > 15 s |

GPS time advances at one second per second. Meridian compares it against the
Flipper's own tick — which runs off a 32.768 kHz crystal at a few tens of ppm, so
honest drift over an hour is under a tenth of a second. Seconds of disagreement
are not the crystal.

The local clock is the one reference in this application an attacker does not
control, which is what makes this check independent of everything else. It is the
only path that catches a **time-shifting attack**, where position stays perfectly
honest and only the clock is pushed — the attack that matters for the
infrastructure that uses GPS as a time source rather than a map.

Drift is accumulated in whole milliseconds as an integer. Summing thousands of
small floats would quietly lose the very drift the check exists to notice.

**Innocent cause:** a receiver that has just acquired may step its clock once as
it solves for time. One step at start-up is normal; repeated or growing
disagreement is not.

---

## Jamming, reported separately

| Reason | Condition |
|---|---|
| Satellites in view, none usable | ≥ 4 in view, 0 used, for 3 epochs |
| Carrier powers collapsed | ≥ 4 tracked, mean < 20 dB-Hz |
| Fix lost with the sky still visible | fix invalid, ≥ 4 satellites in view |

Denial, not deception. This never contributes to the spoofing score, and the
carrier checks stand down under it. Jamming and spoofing are different attacks
with different responses, and conflating them would make both reports useless.

**Innocent cause:** being indoors produces all three of these.

---

## Scoring

```
score = max(
    weighted average over armed checks,   // ALERT = 100, WARN = 45
    strongest single alerting floor,      // so one clear tell is not diluted
    family corroboration floor            // so agreement is worth more than repetition
)
capped at 96
```

| Check | Weight | Alone reaches | Family |
|---|---:|---:|:---:|
| Impossible motion | 18 | 55 | Position |
| Lock captured | 16 | 60 | Position |
| Speed mismatch | 14 | 45 | Position |
| Position frozen | 8 | 30 | Position |
| Altitude anomaly | 6 | 25 | Position |
| Flat carrier power | 14 | 50 | Carrier |
| Carrier too strong | 12 | 45 | Carrier |
| Power vs elevation | 10 | 40 | Carrier |
| Sky not moving | 12 | 45 | Geometry |
| Accuracy implausible | 6 | 25 | Geometry |
| Clock inconsistent | 14 | 50 | Time |

| Families alerting | Floor |
|:---:|---:|
| 1 | — (per-check floor only) |
| 2 | 62 |
| 3 | 80 |
| 4 | 92 |

| Score | Verdict |
|---:|---|
| < 20 | NOMINAL |
| 20–39 | ANOMALOUS |
| 40–64 | SUSPECT |
| ≥ 65 | SPOOF LIKELY |

Two consequences are deliberate, and both are asserted in the test suite:

- **The highest per-check floor is 60**, below the SPOOF LIKELY band. No single
  check can reach that verdict on its own, however clean its evidence.
- **The ceiling is 96.** A single antenna cannot prove spoofing, and the number
  refuses to claim otherwise.

Flags **latch** for a configurable hold (default 60 s) after the condition
passes, because a teleport is one epoch long and a screen that clears it before
the user looks up is useless. They decay afterwards, so the display recovers.

---

## Sensitivity

Low, Normal and High scale the *statistical* thresholds — σ, correlation, speeds,
drift — by 1.4× / 1.0× / 0.7× in the appropriate direction.

Absolute physical thresholds are not scaled. Carrier power shifts by a fixed
±1.5 dB, for the reason given above.

The test suite asserts that an honest sky stays NOMINAL with **zero alerts** even
at High, and that a real attack is still noticed at Low.

---

## References

- IS-GPS-200, *NAVSTAR GPS Space Segment / Navigation User Interfaces* — signal
  structure, published codes, minimum received power.
- NMEA 0183 v4.10 — sentence formats and the unified satellite numbering used to
  decode combined-talker GSA and GSV.
- Humphreys et al., *Assessing the Spoofing Threat* (ION GNSS 2008) — the
  capture-and-drag attack the *Lock captured* check is modelled on.
- Psiaki & Humphreys, *GNSS Spoofing and Detection* (Proc. IEEE, 2016) — survey
  of single-antenna detection metrics, including C/N0-based tests.

The implementation is original.
