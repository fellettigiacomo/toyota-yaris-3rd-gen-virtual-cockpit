# Toyota Yaris Hybrid XP130 (2014, THS-II NoDSU) — Decoded CAN Signals

Short reference of what has been reverse-engineered on this vehicle's CAN bus.
Full methodology, validation evidence and negative results: `docs/signal_findings.md`.
Machine-readable definitions: `dbc/toyota_yaris_xp130_reversed.dbc`.

Capture point: factory head-unit connector (28-pin, CAN-H/CAN-L), 500 kbps.
Validated on two independent real-drive logs (~430s and ~440s).

## What we found

- Every gauge-relevant signal on the dash is decoded: speed, RPM, gear, turn
  signals, accelerator, brake, coolant temperature, ambient temperature, and
  the CHG/ECO/PWR power-flow needle.
- The HV battery **SOC** and an explicit **EV-drive bit** were found as plain
  passive broadcasts, contrary to the initial assumption (based on Prius
  documentation) that they require active UDS diagnostic polling.
- A high-resolution **fuel-consumption counter** exists, but it is not a fuel
  level gauge — no absolute tank level is available on this bus.
- HV battery current/voltage and remaining driving range are confirmed absent
  from this bus; not obtainable by passive listening.

## Confirmed signals (validated on 2 independent logs)

| Signal | CAN ID | Field | Formula / meaning |
|---|---|---|---|
| Vehicle speed | `0x0B4` | `SPEED` | `(byte5<<8\|byte6) * 0.01` km/h |
| Engine RPM | `0x1C4` | `RPM` | signed 16-bit `* 0.78125` rpm |
| Gear (P/R/N/D/B) | `0x127` | `GEAR` | 4-bit enum, all 5 states observed |
| Accelerator pedal | `0x245` | `GAS_PEDAL` | `byte2 * 0.005` (0–1 = 0–100%) |
| ICE running (bit) | `0x245` | `ICE_RUNNING` | byte3 bit4, 98–99% agreement with RPM>50 |
| Brake pressed (bit) | `0x230` | `BRAKE_PRESSED` | single bit |
| EV drive (bit) | `0x498` | `EV_DRIVE` | byte5 bit7, on only while moving with ICE off |
| HV battery SOC | `0x4A7` | `BATTERY_SOC` | `byte2 * 0.5` = % (observed 54–71.5%, matches THS-II operating band) |
| CHG/ECO/PWR indicator | `0x247` | `HSI_VALUE` / `HSI_ZONE` | signed % of gauge scale (-100..+100, positive ceiling above +100 unverified); firmware decodes byte[1] unsigned with the CHG floor anchored at raw≥156 to avoid an `int8_t` sign-wrap under hard PWR (see `signal_findings.md` Addendum 4). Cluster display halves the raw value on the PWR side (PWR% = raw/2, CHG unchanged) per the owner's on-car observation that raw~100 is only the ECO/PWR midpoint, not full scale — provisional, ~77% max displayable with the current signal pending a hard-PWR capture |
| Turn signals | `0x614` | `TURN_SIGNALS` | 2-bit enum (+ hazard bit) |
| Odometer (total) | `0x611` | `ODOMETER` | 20-bit, km/miles unit flag — NOT remaining range |
| Ambient/outside temperature | `0x442` | `AMBIENT_TEMP` | `byte0 - 40` = °C, scale confirmed against real readings; decoded signal exists on the bus but is no longer shown on the cluster (it's outside air temp, not engine temp — no confirmed engine/coolant temp signal is wired into the firmware, see the "strong hypotheses" table below) |

## Strong hypotheses (semantics solid, exact scale/units not fully confirmed)

| Signal | CAN ID | Field | Notes |
|---|---|---|---|
| Brake pedal (analog pressure/position) | `0x224` | `BRAKE_VALUE` | 16-bit, strongly correlated with brake bit |
| Coolant temperature | `0x618` | `TEMP_RAW` | warm-up curve to plateau; scale guess `*0.375` ≈ 90°C |
| Second temperature (oil/CVT/HV pack?) | `0x3B9` | `TEMP2_RAW` | correlates with elapsed time and coolant temp |
| Longitudinal accel/torque demand | `0x320` | `ACCEL_DEMAND` | signed, positive on acceleration, negative on braking/coast |
| Fuel consumption counter | `0x3A0` | `FUEL_COUNTER_RAW` | decrements with ICE-on runtime, NOT a tank-level gauge; likely low byte of a wider register |

## Present but not independently validated (inherited from opendbc, message exists at expected layout)

- `0x620 SEATS_DOORS` — door open / seatbelt bits
- `0x622 LIGHT_STALK` — headlight/fog/auto-high-beam bits
- `0x399 PCM_CRUISE_SM` — cruise control state
- `0x3B7 ESP_CONTROL` — traction/VSC disable, brake lights

## Ruled out / excluded (documented so they aren't re-investigated)

- `0x4A8` byte2 top-3-bits — looked like a battery bar level, retracted: cyclic counter (wraps every 60–100s)
- `0x4A2` `CHASSIS_BRAKE2` — looked like battery power, actually a second brake/ABS-VSC signal
- `0x612` byte5, `0x619` byte6, `0x63B` byte6 — unstable or heartbeat-like, not physical signals
- `0x4AC` byte[6] (`OSC_9S_RAW`, was "steering-wheel MODE button") — retracted: free-running 4.5s-on/4.5s-off oscillator (9s period, phase-locked to the byte[4] rolling counter), runs identically in every log with the button untouched; cycled the cockpit screens by itself every 9s on the road. Found only because a 31s capture of a 9s square wave happens to look exactly like the 3-burst tap pattern the scan was told to find (see `signal_findings.md` Addendum 5 retraction)

## Confirmed absent from this bus (not obtainable by passive listening)

- **Remaining driving range (km)** — only the total odometer exists; likely computed inside the instrument cluster only
- **HV battery current / voltage / temperature** — no broadcast found; Prius-style diagnostic IDs (0x03B/0x3CB/0x529) are absent on this platform; would require solicited UDS requests to the battery ECU
- **Steering-wheel MODE button** (and by extension the other steering audio switches) — exhaustive cross-validated re-scan of the dedicated tap capture (`tools/find_button_crossval.py`: bit deviations, value-change/event counters, event-triggered or early frames, capture-only IDs) found nothing matching the tap pattern that is also quiet while driving. Consistent with Toyota's architecture: the steering audio switches are an analog resistive ladder wired to the head-unit connector's SW pins, read by the Simplesoft CAN box as an analog key input — they never transit CAN. Getting MODE for real means reading that ladder with an ESP32 ADC (or sniffing the box's UART key events)

## Architecture note

Signals `0x498`–`0x4AF` are an undocumented Toyota hybrid-powertrain telemetry
family (matches opendbc's opaque `ENG1D5x`/`CGW` gateway frames, absent on
non-hybrid Toyota models) — this is where SOC and hybrid status live. With
SOC, EV-drive, ICE-running and the power-demand signal above, the aftermarket
Simplesoft CAN-bus box's energy-flow display can be fully reconstructed from
passive listening alone, no active diagnostics needed.
