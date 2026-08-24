# Toyota Yaris Hybrid XP130 (2014, THS-II NoDSU) — CAN bus signal findings

Full methodology and validation evidence behind the signals in
[`toyota_yaris_xp130_reversed.dbc`](../../dbc/toyota_yaris_xp130_reversed.dbc).
For a compact table, see [`decoded_signals_summary.md`](decoded_signals_summary.md).

## Capture setup

- **Tap point**: the factory head-unit connector (28-pin, p/n `90980-12555`),
  pins 9 (CAN-H) / 10 (CAN-L), 500 kbps — **not** the OBD-II diagnostic port.
  This is the same point where the aftermarket Simplesoft CAN-bus box
  normally taps in; the message set seen here (`SPEED`, `RPM`, `GEAR`,
  `BRAKE`, `TURN_SIGNALS`, `ODOMETER`, …) matches what's expected on the
  main HS-CAN bus, so this connector is treated as the same logical bus, not
  a separate/richer one.
- **Logs**: two independent real-drive captures, `session_0044.log` (~430s,
  ESP32 in place of the Simplesoft box) and `session_0047.log` (~440s, ESP32
  inline between the Simplesoft box and the car). A signal is only marked
  "confirmed" once its behaviour reproduces on both.
- **Reference DBCs**: `toyota_nodsu_hybrid_pt_generated.dbc` from
  `BogGyver/opendbc` (branch `tesla_unity_dev`), cross-checked against the
  Prius/Camry/Highlander hybrid variants and `commaai/opendbc` upstream.
  None of these define SOC, range, or fuel — those come from Prius-community
  reverse-engineering docs (EAA-PHEV wiki, PriusChat) and original analysis.
- **Toolchain**: the scripts in [`../tools/`](../tools/) plus `cantools` for
  strict DBC-based decoding. `cross_validate_candidates.py` and
  `find_word16_candidates.py` implement the cross-log check described above.

## Confirmed signals

| Signal | ID | Field | Formula |
|---|---|---|---|
| Gear (P/R/N/D/B) | `0x127` | `GEAR` | 4-bit enum, byte5 high nibble |
| Vehicle speed | `0x0B4` | `SPEED` | `(byte5<<8\|byte6) * 0.01` km/h |
| Engine RPM | `0x1C4` | `RPM` | signed 16-bit `* 0.78125` rpm |
| Accelerator pedal | `0x245` | `GAS_PEDAL` | `byte2 * 0.005` (0–1 = 0–100%) |
| ICE running (bit) | `0x245` | `ICE_RUNNING` | byte3 bit4 |
| Brake pressed (bit) | `0x230` | `BRAKE_PRESSED` | single bit |
| Turn signals | `0x614` | `TURN_SIGNALS` | 2-bit enum + hazard bit |
| Odometer (total) | `0x611` | `ODOMETER` | 20-bit, km/miles unit flag |
| Ambient temperature | `0x442` | `AMBIENT_TEMP` | `byte0 - 40` = °C |
| HV battery SOC | `0x4A7` | `BATTERY_SOC` | `byte2 * 0.5` = % |
| EV drive (bit) | `0x498` | `EV_DRIVE` | byte5 bit7 |
| CHG/ECO/PWR indicator | `0x247` | `HSI_VALUE` / `HSI_ZONE` | see below |

**Gear** — validated across both logs: session_0044 shows a clean
P(0)→R(1)→D(3) sequence with `SPEED` and `BRAKE_PRESSED` transitions lining
up exactly with each shift; session_0047 adds N and a full end-of-drive
cycle through all five states including B (Engine Brake). `CAR_MOVEMENT`
(byte4 of the same message, signed, oscillates ±) is a separate field, not
part of the gear value.

**Speed / RPM** — inherited from opendbc, reconfirmed with `cantools`
against real 0→60→0 kph and idle/rev profiles. `WHEEL_SPEEDS` (0x0AA) from
opendbc is not present on this bus.

`SPEED`'s 0.01 kph scale is cross-validated against the odometer, which is
the one distance reference on board that regulation requires to be
*accurate* (unlike the speedometer — see below). Taking the exact instants
`0x611`'s `ODOMETER` ticks over a whole kilometre, the interval between the
first and last tick in session_0044 is exactly 2 km; integrating the decoded
`0x0B4` speed over that same interval gives 2.0001 km, i.e. **+0.01%**. The
method's own uncertainty is ±0.82%, set by `0x611`'s 1 Hz rate (each tick's
instant is only known to ±1 s). session_0047 never crosses two whole-km
boundaries, so no exact interval can be isolated there.

**Indicated vs. true speed** — the car's own speedometer reads *higher* than
`SPEED`: roughly +10 km/h at an indicated 140 against a true 130 on the
vehicle this was developed on (a single needle reading, so call it +5..+15).
This is deliberate and regulatory, not a decode error. UNECE R39 requires
that the indicated speed never be *lower* than the true speed and caps the
excess at 10% + 4 km/h, so manufacturers bias the cluster upward to stay
compliant across tyre wear, optional tyre sizes and temperature. At a true
130 km/h the legal window is 130–147.

The biased value is computed inside the instrument cluster and is **not**
broadcast: no field on this bus carries it. Four independent speed-carrying
fields all agree with `0x0B4` to within 0.5% across the full 0–63 km/h range
the logs cover — `0x2A4` byte[0:2] (Δ mean 0.00 km/h), `0x610` byte[2]
(1.0001×kph, R²=0.9996) and `0x498` byte[3] (0.9951×kph, R²=0.9957) — and
the odometer check above anchors the scale absolutely. A GPS comparison on
the road matches the decoded value as well.

No correction is applied in firmware, on purpose: `SPEED` is the true speed,
which is what the odometer, GPS and the other bus fields all agree on.
Matching the cluster instead would mean modelling its bias (`indicated =
k × true + c`), and on this car the cluster is an **analog needle** — a
driver reads it to ±3 km/h at best, and any photo-based calibration is
dominated by a systematic parallax error that repeating measurements cannot
average away. Chasing a ~5% correction whose own uncertainty is a few km/h
would add error rather than remove it.

**Accelerator / ICE running** — `GAS_PEDAL` ranges 0–35% observed, tracking
the accelerate/coast pattern of city driving. `ICE_RUNNING` agrees with
`RPM > 50` 98–99% of the time across both logs and is cleaner (no threshold
tuning needed).

**Brake pressed** — transitions line up exactly with gear shifts and speed
changes (brake held at standstill around each shift).

**Turn signals** — all three states (left/right/none) observed with
multi-second durations consistent with real indicator usage.

**Odometer** — confirmed as the *total* odometer (99552→99555 km over the
log), not remaining range; `UNITS` flag correctly reads km.

**Ambient temperature** — the only near-constant byte in the low-frequency
climate message family (`0x440`/`0x442`/`0x44D`/`0x45C`); raw values differed
between the two capture days (77 vs 67 → 37 °C vs 27 °C) and the scale was
confirmed against the actual outside temperature on each day. This is
outside air temperature, not engine/coolant temperature.

**HV battery SOC** — byte[2] of `0x4A7`, range 108–143 raw → 54.0–71.5%,
matching the real THS-II NiMH operating band (~40–80%). A SOC is the
*integral* of power flow, so it barely correlates with the accelerator at
zero lag; it was found by scoring each value *change* against expected
battery physics instead (discharge during EV driving, charge during ICE-on
or regen) rather than by instantaneous correlation. Physics checks out on
both logs: flat while parked, steady climb during stationary ICE warm-up
charging, slow decline through every EV stretch, recovery through every
ICE-on/regen stretch, never wraps. The 0.5%/bit scale matches the known
Techstream/Prius-Gen2 convention but has not been checked against a
reference tool reading.

**EV drive** — byte5 bit7 of `0x498`. On only while the vehicle moves with
the ICE stopped (pure electric propulsion); off at standstill even with the
ICE off, and off while driving with the ICE running. Matches the dash EV
indicator's semantics exactly (100% agreement with the moving+ICE-off state
across both logs). Two redundant "ICE stopped" bits were also found:
`0x49C` byte5 bit6 and `0x4AD` byte3 bit6.

**CHG/ECO/PWR indicator (Hybrid System Indicator)** — the gauge needle
position is broadcast directly on `0x247` (42.5 Hz, companion message to
`GAS_PEDAL`), no synthesis needed. `HSI_VALUE` (byte1) is the needle
position in percent of scale: 0 at standstill, +30..+47 at steady cruise
(the test that rules out this being just a copy of accelerator demand —
cruise needs sustained power even with a near-zero acceleration signal),
up to +86/+94 under moderately hard acceleration, negative while
coasting/braking with a hard clamp at exactly -100 (full CHG). `HSI_ZONE`
(byte0) carries the sign (12=drive/positive, 15=charge/negative,
4=zero-in-gear, 6=P/R). `HSI_GEAR_FLAG` (byte2) is 50 in forward gears,
255 in P/R.

**The sign lives in `HSI_ZONE`, not in `HSI_VALUE`'s byte.** The two sides
of the gauge use different encodings of byte1 and their raw ranges overlap,
so the byte on its own is ambiguous:

| Zone | Meaning | byte1 encoding | Raw seen in logs |
|---|---|---|---|
| 12 | ECO/PWR (positive) | unsigned magnitude, sweeps past 155 | 0–94 |
| 15 | CHG (negative) | two's complement, clamped at -100 | 156–255 (= -100..-1) |
| 4 | zero in gear | always 0 | 0 |
| 6 | P/R, gauge inactive | always 0 | 0 |

The zone/value correlation is exact across both sessions (n=32779, zero
exceptions), which is what makes it usable as the sign source. An earlier
firmware revision instead inferred the sign from byte1 alone (raw≥156 ⇒
negative, anchored on the confirmed CHG floor). That works for everything
in these two calm-driving logs but breaks on the car under full throttle:
the PWR sweep runs past 155, so a hard-PWR raw was read as a large negative
and the bar swung to CHG. On-car testing confirmed the flip; the decoder
now branches on `HSI_ZONE`.

On the display, the PWR side is shown halved (`PWR% = raw/2`) because the
ECO/PWR boundary (raw≈100) is only the midpoint of the real gauge's PWR
sweep, not its top — so full PWR ≈ raw 200 reads 100%. That the raw value
does climb past 155 under full throttle (rather than clamping at +100 like
the CHG side does at -100) is exactly what the false-CHG flip demonstrated,
and it is consistent with this halved scaling.

## Strong hypotheses (semantics solid, exact scale unconfirmed)

| Signal | ID | Field | Notes |
|---|---|---|---|
| Brake pedal (analog) | `0x224` | `BRAKE_VALUE` | 16-bit, strongly correlated with the brake bit |
| Coolant temperature | `0x618` | `TEMP_RAW` | warm-up curve to plateau; scale guess `*0.375` ≈ 90 °C |
| Second temperature (oil/CVT/HV pack?) | `0x3B9` | `TEMP2_RAW` | correlates with elapsed time and coolant temp |
| Longitudinal accel/torque demand | `0x320` | `ACCEL_DEMAND` | signed, positive on acceleration, negative on braking/coast |
| Fuel consumption counter | `0x3A0` | `FUEL_COUNTER_RAW` | decrements with ICE-on runtime, **not** a tank-level gauge |

**Brake analog value** — byte[4:6] of `0x224` (~41 Hz, same rate as
`SPEED`/`BRAKE_PRESSED`, likely the same ECU). Median 0 while released vs.
median 62 while pressed (n=12027/5731); 99.99% of released samples are ≤5,
84% of pressed samples are >5. A single release event shows a smooth
continuous decay over ~700 ms, consistent with an analog pressure or pedal
position rather than a second bit. Exact units and full-scale range are not
confirmed (max observed 537, slightly above the 0–511 9-bit range opendbc's
unrelated `BRAKE_MODULE`/0x226 template would suggest — that ID is absent
from this bus).

**Coolant temperature** — byte[3] of `0x618`, part of a low-frequency
(~0.1 Hz) "combination meter" message family (`0x610`-`0x615`,
`0x618`-`0x61C`). Climbs from 0 at cold start to a plateau around 230–240 by
t≈350–430s, matching a textbook engine coolant warm-up curve reaching
thermostat-regulated temperature after ~5 minutes. `temp_C ≈ raw * 0.375`
places the plateau at a plausible 90 °C but is an unverified guess, not a
reference-thermometer calibration; could equally be transmission/CVT fluid.

**Second temperature signal** — byte[0] of `0x3B9` (dlc=3, only this byte
active, ~1 Hz). Climbs smoothly to a plateau late in the drive on both logs,
correlating strongly with elapsed time (r=0.945/0.968) and with the coolant
curve above (r=0.950/0.969), including a shared inflection point. Weak
correlation with instantaneous RPM (r≈-0.1) rules out a fast-responding
sensor and points to a larger thermal mass — candidates: engine oil,
CVT/transaxle fluid, or HV battery pack. Plateau value differs between the
two logs (173 vs 160), unlike a thermostat-regulated coolant signal, which
fits a less tightly-regulated mass. No scale proposed.

**Longitudinal accel/torque demand** — byte[4] of `0x320` (signed, ~20 Hz):
positive under acceleration (+10..+31 with pedal), negative while
braking/coasting (-8..-48), ~0 at rest and at steady cruise — this last
point is what rules it out as HV battery current, since current should stay
nonzero at cruise. byte[7] is the same signal with a constant +42 offset;
byte[5] is a `{0,8}` flag active only at standstill. Scale unconfirmed
(plausible ~0.02–0.05 m/s² per LSB).

**Fuel consumption counter** — byte[7] of `0x3A0`, the only active byte in
an otherwise all-zero message, 10 Hz. Decrements track ICE-on runtime
(~1 unit per 20–25s of engine running, consistent to ~7% across both logs),
not wall-clock time or distance, which rules out both "fuel level" (a 36 L
tank on this scale would imply an impossible 50+ L/h burn rate) and
"distance to empty". Best read as a high-resolution fuel-consumed counter,
quantum ≈10–30 mL — most likely the low byte of a wider register, since a
wrap 255→0 has not yet been observed (would need a ≥30 min log to confirm).
Use it for instantaneous consumption, not as an absolute level.

## Present but not independently validated

Inherited from opendbc at their documented layout; message exists at the
expected ID but the specific bits were not exercised during capture:

- `0x620 SEATS_DOORS` — door open / seatbelt bits (seatbelt toggled 0/1;
  doors never opened during the drive)
- `0x622 LIGHT_STALK` — headlight/fog/auto-high-beam bits (low
  beam/parking light toggled, plausible)
- `0x399 PCM_CRUISE_SM` — cruise control state (all-zero, never engaged)
- `0x3B7 ESP_CONTROL` — traction/VSC disable, brake lights (all-zero, no
  VSC/TC intervention during this calm drive)

## Ruled out

Documented so these aren't re-investigated:

- **`0x4A8` byte2 top-3-bits** — looked like a battery-bar level; it's a
  cyclic counter that wraps every 60–100s, physically incompatible with an
  HV battery.
- **`0x4A2` byte3/byte5** (`CHASSIS_BRAKE2`) — anti-correlates with the
  accelerator/braking pattern strongly enough to look like battery power at
  first, but correlates even more strongly with the confirmed brake analog
  signal (`0x224`) and with `SPEED` (which battery power shouldn't track) —
  almost certainly a second ABS/VSC brake telemetry pair, not SOC.
- **`0x612` byte5, `0x619` byte6, `0x63B` byte6** — unstable trends whose
  correlation sign flips between the two logs, or fixed-period counters
  (`0x63B` increments every ~25.5s regardless of driving state) — heartbeat/
  counter artifacts, not physical signals.
- **`0x4AC` byte6** — free-running 4.5s-on/4.5s-off square wave (9.0s
  period), present identically in every log. Not an event/status signal.

## Confirmed absent from this bus

Not obtainable by passive listening on this connector:

- **Remaining driving range (km)** — only the total odometer exists;
  likely computed inside the instrument cluster and never broadcast.
- **HV battery current / voltage / temperature** — no broadcast found;
  the Prius-style diagnostic IDs (`0x03B`/`0x3CB`/`0x529`) are absent on
  this platform. Would require solicited UDS requests to the battery ECU.

## Architecture notes

Signals `0x498`–`0x4AF` are an undocumented Toyota hybrid-powertrain
telemetry family — opendbc's `toyota_2017_ref_pt.dbc` names the same ID
range `ENG1D50`..`ENG1D60`, opaque data-recorder frames from the `CGW`
(Central Gateway) node, absent on non-hybrid Toyota models. This is where
SOC and hybrid status live. With SOC, EV-drive, ICE-running and the HSI
power-demand signal, the aftermarket Simplesoft box's "OilElectricityInfo"
energy-flow display (3-bit battery level, hybrid flag, 6 flow-direction
flags) can be fully reconstructed from passive listening alone — no active
diagnostic polling needed, despite the ~10s the box's screen takes to load
(most likely just app initialization, not a diagnostic round-trip).

## Open questions

1. **SOC scale** (`0.5%/bit`) is a known Toyota/Techstream convention, not
   yet checked against a reference tool reading.
2. **Fuel counter wrap** (`0x3A0` byte7) — needs a ≥30 min log to observe
   the predicted 255→0 rollover and confirm it's the low byte of a wider
   register.
3. **`0x612` byte5** — a slow random walk centered on 128 (±16) with a
   byte1 heartbeat bit; excluded as SOC (no charge/discharge physics) but
   not identified. Possibly 12V-battery current with an offset. Low
   priority.
4. **`0x4AE` byte7** — rises slowly (72→75) in both logs; a weak,
   unconfirmed candidate for HV battery/inverter temperature.
5. **CHG/PWR positive ceiling** — the true raw value at 100% PWR is still
   unverified; needs a capture that includes hard acceleration into the
   PWR zone (see the CHG/ECO/PWR writeup above).
