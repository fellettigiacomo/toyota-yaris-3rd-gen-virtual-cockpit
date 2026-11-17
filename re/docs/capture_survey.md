# Full capture survey

A sweep of all three captures (`session_0044.log`, `session_0047.log`,
`mode_button_capture_20260714.log`) across all 100 CAN IDs, looking for
anything the earlier, signal-by-signal work had not covered. Solid results
are in [`toyota_yaris_xp130_reversed.dbc`](../../dbc/toyota_yaris_xp130_reversed.dbc);
this file is the evidence and the list of what is still open.

## Method

1. Every byte, every big-endian 16-bit word and every 12-bit field of every
   ID was correlated against a reference set, separately on each drive log:
   the already-decoded signals, plus derived ones (acceleration from speed,
   kinetic power, battery power, an engine-power estimate).
2. A candidate counts only if the same reference wins with the same sign on
   both logs. Its scale then comes from a linear fit.
3. Every bit that toggles was matched against driving conditions (gear,
   brake, engine running, EV, charging, standstill, …) by agreement rate.
4. The mode-button capture was diffed byte by byte in time order.

`session_0047` has gaps (its frame rate is ~78% of `session_0044`'s), so
its correlations run a little lower throughout.

## New signals

| Signal | ID | Field | Scale | Evidence |
|---|---|---|---|---|
| Steering-wheel angle | `0x025` | 12-bit signed, byte0 low nibble + byte1 | 1.5 °/bit | r=0.994/0.997 between yaw rate and speed × angle; range −491…+500° |
| Yaw rate | `0x024` | 10-bit, byte0–1, minus 512 | 0.245 °/s/bit | scale from lateral accel = v × yaw |
| Lateral acceleration | `0x024` | 10-bit, byte4–5 | 0.03589 m/s² − 18.375 | r=0.89/0.85 against v × yaw |
| Longitudinal acceleration | `0x024` | 10-bit, byte2–3 | same scale | r=0.95 against d(speed)/dt in `session_0044`, 0.63 in `session_0047` |
| Generator (MG1) speed | `0x49A` | signed 16-bit, byte0–1 | 0.625 rpm/bit | raw = 5.75·RPM − 122·km/h, R²=0.997–0.998 |
| Traction motor (MG2) speed | `0x353` | signed 16-bit | 1 rpm/bit | 77 per km/h, negative in reverse |
| Engine power | `0x49C` | byte7 | ≈0.5 kW/bit | r=0.94/0.93 against the engine-power estimate; 0 with the engine off |
| Wheel speeds ×4 | `0x4A0` | byte3…byte6 | ≈1.3 km/h/bit | left–right difference follows the steering angle, r=0.73–0.87 |
| Second speed | `0x2A4` | 16-bit, byte0–1 | 0.01 km/h | R²=0.9995 against `SPEED` |
| Displayed speed | `0x610` | byte2 | 1 km/h | 1:1 with `SPEED` |
| Signed speed | `0x49B` byte7, `0x361` byte1 | signed 8-bit | 1 and 1.31 km/h | negative in reverse |
| Accelerator, 12-bit | `0x49B` | byte0 + byte1 high nibble | 0.025 %/bit | R²=0.998 against `GAS_PEDAL` |
| Gear lamps P/R/N/D/B | `0x3BC` | one bit each | — | P, R, D agree with `GEAR` 98–100%; N and B seen in one log only |
| Shift position, one-hot | `0x49B` | byte3 bits 7–4 = D/R/P/N | — | 97–100% agreement |
| Engine stopped | `0x49C` | byte5 bit6 | — | exact inverse of engine running, both logs |
| Trip time | `0x61C` | byte7 | 1 min/bit | increments every 60 s on both logs |
| Energy-monitor flags | `0x3B6` | byte5, byte6 | — | see below |

The two motor speeds check each other. With MG2 at 77 rpm per km/h and the
ring gear at 29.3 rpm per km/h (from the MG1 fit, assuming the 3.6
planetary factor), MG2/ring comes out at 2.63 — the transaxle's 2.636
motor reduction gear. Neither scale has been compared with a tool.

**Energy monitor (`0x3B6`)** — byte6 takes one fixed pattern per driving
state, the same on both logs:

| State | byte6 |
|---|---|
| Standstill | `0x01` |
| EV drive | `0x75` |
| Regenerative braking | `0xAD` |
| Engine driving and charging | `0x6D` / `0x6C` |
| Engine driving, no charge | `0x0C` |

From those patterns: bit3 = battery charging and bit4 = battery discharging
(95–97% agreement with the sign of the battery current), bit6 = motor →
wheels, bit7 = wheels → motor, bit2 = vehicle moving; byte5 bits 2–3 =
engine running, bits 5–6 = engine → wheels. This looks like the flag set
the car's own energy-flow display is drawn from. The firmware's energy-flow
screen now reads its directions from these flags and only infers them when
the message is missing. The bit names have not been compared with the car's
display.

## Hypotheses

- **Instantaneous consumption and fuel flow** — `0x610` byte5–6 and
  `0x3D3` (16-bit). The `0x610` value is 0 while moving with the engine off,
  `0xFFFD` at standstill and 6–365 while the engine runs. It is a
  per-distance figure: value × speed = 3.51 × the `0x3D3` value, with the
  same constant on both logs (r=0.96), so `0x3D3` is fuel flow and `0x610`
  is that flow divided by speed.

  Read as 0.1 l/100 km per bit, the numbers hang together: `0x3D3` comes out
  at 0.975 mm³/s per bit (a round 1 within 2.5%), one step of the `0x3A0`
  fuel counter at 15–21 ml, and the whole of `session_0044` at 4.9 l/100 km.
  The 7–16 l/100 km seen while the engine runs is the engine-on figure on a
  cold start, while it is also lifting the battery from 60% to 78%; the trip
  figure is lower because nearly half the distance is driven with the engine
  off. Still unconfirmed against the car's own display.
- **Range to empty** — `0x619` byte5–6 as a 16-bit number of km. It reads
  290→287 over `session_0044` (3 km driven), 263→262 over `session_0047`
  (1 km), and 185 in the mode-button capture. Against the odometer: 21 km
  between the first two captures for 24 km of range, 73 km between the last
  two for 77 km of range. This overturns the earlier "remaining range is not
  on this bus". Three captures, one tank; no refuel seen.
- **Fuel level** — `0x4A6` byte4: constant inside each capture and falling
  across them, 36 → 34 → 26, i.e. one step per ~9.5 km. At 0.5 l/bit that is
  18 → 17 → 13 l and 5 l/100 km between captures, which also matches the
  range above (102 km of range lost for 5 l). The scale is a guess from
  three points.
- **Parking brake** — `0x620` byte7 bit4. In `session_0047` it is set at the
  start, clears 1 s before READY and sets again after the final stop. One
  log only.
- **Door and seatbelt bits** (`0x620`) — the earlier notes say these were
  never exercised; in `session_0047` a door bit (byte5 bit4) is set until
  t=69 s and the seatbelt bit clears at 95 s and sets again at 418 s.
  `0x626` byte2 drops `0x30`→0 at the same instant as the door bit.
- **Pack voltage** — `0x49D` byte0 and byte1: rise under charge current, sag
  under discharge, ≈95 at rest. No scale; ≈1.66 V/bit if the rested pack is
  at a typical 1.32 V/cell. `0x38B` byte5 and `0x4AE` byte5 are linear
  functions of it.
- **Battery power** — `0x4AF` byte0 (signed) follows the battery current
  (r=0.99) at 0.36 per amp, but not as current × the voltage bytes above.
- **Regenerative braking effort** — `0x4A2` byte2: non-zero only while
  regenerating (r=0.83/0.80 against regen current).
- **Brake pressures** — `0x4A2` byte0 and byte4 track `BRAKE_VALUE` at
  r=1.00 in `session_0044` but only 0.7 in `session_0047`.
- **Low-rate copies** — `0x4A1` byte2 = yaw rate, byte3 = steering angle
  (both +128), byte1 = longitudinal acceleration; `0x498` byte1 = RPM / 25,
  byte3 = speed in km/h; `0x361` byte2 = accelerator raw.

## Corrections to earlier notes

- **`0x618` byte3 is probably not coolant temperature.** It moves in steps
  of 10, starts at 0, and in `session_0047` falls from 130 to 110 within a
  minute of the car stopping. It behaves like a trip average — most likely
  average speed, next to trip time in the same message family — but no
  single scale fits both logs.
- **`0x245` byte4 is a checksum** (accelerator + flags + `0x4C`), not a
  signal, despite correlating with RPM.

## Negative results

- **Mode-button capture**: 31 s, car stationary. Every change in it is one
  of the free-running oscillators or a ±1 flicker also present in the drive
  logs. No button press shows up on this bus in this capture.
- **Simplesoft frames**: `0x2FC`, `0x387`, `0x389`, `0x38E`, `0x38F`,
  `0x434`, `0x435`, `0x443`, `0x458`, `0x6F9` only exist in the captures
  taken with the Simplesoft box in line and carry constant data — they come
  from the box, not the car.

## Fourth capture: `usb_20261117_065425.log`

47 s taken through the dashboard's own USB dump on a November morning
(odometer 102366 km): 31 s with the car on but not READY, then READY, a cold
engine start and the first metres of driving.

- **Before READY the bus carries body messages only** — 24 IDs, all in
  `0x44x` and `0x6xx`, about 35 frames/s. `SPEED` (`0x0B4`) and the whole
  powertrain set appear only at READY. The firmware uses that: `0x0B4` going
  silent is its "car switched off" cue for the trip summary. A capture that
  runs through a shutdown would confirm the bus looks the same on the way
  down.
- **Ambient temperature (`0x442` byte0) is in doubt.** It reads 37 °C before
  READY and 27 °C after, at 06:54 in mid-November — and 27 is the same value
  as one of the July captures. It looks like a state byte, not a temperature.
- **`0x3B9` byte0 reads 45 on this cold start**, against 105–110 at the July
  starts and 173 at the end of a warm drive. That is what coolant
  temperature would do; at 0.5 °C per bit the warm plateau is 86 °C.
- **Fuel level and range disagree.** `0x4A6` byte4 is 50 here (36, 34, 26 in
  July) while `0x619` gives 215 km (290, 263, 185 in July): more fuel, less
  range. One of the two readings is wrong, or the scale between them is not
  what three summer points suggested. In favour of `0x4A6` being the fuel
  sender: byte4, byte5 and byte6 are the same quantity at three filter
  strengths, and the fastest one (byte6) moves with lateral acceleration,
  as fuel sloshing would.
- **Somebody is running diagnostics at start-up.** Requests on `0x770` and
  `0x7C6` (services `0x21` and `0xA8`) are answered on `0x778` and `0x7CE`
  two seconds after READY. Not the dashboard, which never transmits; most
  likely the Simplesoft box.
- **`0x618` byte3 resets at READY** (30 → 0, then starts counting again),
  which fits a per-trip figure and not coolant.
- SOC starts at 60% again and `0x4A7` byte2 at 108 again, as before.

## Still open

- `0x38A` byte1: an engine-only value that tracks RPM (r=0.91–0.93) —
  load or air flow.
- `0x3B9` byte0: a temperature (105→173 over a drive, fastest with the
  engine on); the best coolant candidate now that `0x618` is in doubt.
- `0x4AE` byte7 (72→75), `0x49D` byte3/byte4, `0x3B0` byte1: slow risers,
  temperature candidates.
- `0x380` byte6 (49→43 over 3 km), `0x3B0` byte3 (79→75): slow down-counters.
- `0x4A7`: the former "SOC"; an engine-side thermal quantity.
- `0x025` bytes 4–6, `0x49C` byte1–4, `0x4AD`, `0x4A6`, `0x4A8`: unexplained.
- Wheel-speed assignment (which byte is which corner).
