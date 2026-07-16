# sw-capture-fw — steering-wheel switch ADC characterization (ESP32-C3 mini)

Third firmware in this repo, complementary to `obd-capture-fw` (CAN) and the
main `firmware` cluster. It exists because the **MODE button does not
transit the CAN bus** (see [`../docs/signal_findings.md`](../docs/signal_findings.md)):
the steering-wheel audio controls are an **analog resistive ladder** — each
button closes a different resistance across the steering-switch (SW) pin
pair of the 28-pin radio connector (`90980-12555`), and whoever reads them
(stock: the Simplesoft CAN box) just measures a voltage level.

This firmware samples that level with the ADC of a separate **ESP32-C3
mini** board (the cluster's ESP32-S3 stays untouched while probing the car)
and prints over USB:

- `EVT` lines (always on): every stable-level change, with timestamp,
  previous/new level and duration — press buttons one at a time and the
  ladder's value table comes straight out of the log.
- `LVL` lines: a heartbeat of the current stable level every 5s.
- an optional CSV stream (`STREAM ON` / `STREAM OFF`, same commands as
  `obd-capture-fw`): `t_ms,min_mV,avg_mV,max_mV` every 20ms, captured to a
  file with [`../tools/sw_adc_logger.py`](../tools/sw_adc_logger.py).

Printed values are **millivolts at the pin** (calibrated); the actual
voltage on the SW line is `mV × 2` with the 2:1 divider described below.

## Wiring

### First: check with a multimeter (2 minutes)

Resistance between the two SW pins of the 28-pin connector (steering-wheel
controls — typically a dedicated SW / SW-GND pair, see `90980-12555` on
pinoutguide.com), **with the connector unplugged or ignition off**: should
read high/open at rest and a low, distinct, stable value for each button
held down. This confirms which pin pair is the ladder before wiring
anything to it.

### Mode A — sniffing in parallel with the CAN box (measures "in service")

The CAN box stays connected and provides the line's bias. Tap in parallel
at **high impedance**, injecting nothing:

```
SW line (at connector pin) ──[R1 100kΩ]──┬──> GPIO3 (ADC)
                                          │
                                     [R2 100kΩ]
                                          │
SW-GND (same connector) ─────────────────┴──> ESP32-C3 GND
```

- 2:1 divider: even if the box biases the line at 5V, the pin only sees
  ~2.5V — inside the C3 ADC's calibrated 11dB range. A 3.3V bias reads fine
  too (ladder steps are hundreds of mV).
- Added load: 200kΩ to ground — negligible next to the ladder (~0.1-3kΩ)
  and the box's pull-up; verify the buttons still work on the head unit
  after wiring in.
- **Common ground is mandatory**, taken from the connector's SW-GND pin
  (the ladder's return), not from a random chassis point.
- **Never** connect the ESP32's 3.3V/5V rail to the SW line while the box
  is connected.
- Power the C3 from USB (the logging laptop) or a clean 5V source.

This also measures the box's real bias voltage (the idle level), needed to
size the final reading in the cluster firmware.

### Mode B — bench test / box disconnected

With no box connected nothing biases the line, so the ESP32 provides it:

```
3V3 ESP32-C3 ──[Rpull 1kΩ]──┬── SW line
                             └──[R1 100kΩ]──┬──> GPIO3 (ADC)
                                        [R2 100kΩ]
                                             │
SW-GND ──────────────────────────────────────┴──> ESP32-C3 GND
```

Idle ≈ 3.3V on the line (≈1650mV at the pin); each button forms an
`Rpull / R_button` divider with its own plateau. With a 1kΩ pull-up, the
typical Toyota values (~0.1-3.3kΩ) spread across most of the range.

Pin note: GPIO3 = ADC1_CH3, chosen to avoid the C3's strapping pins
(GPIO2/8/9) and ADC2 (GPIO5, unreliable on the C3). Configurable in
`include/app_config.h`.

## Use

```bash
cd re/sw-capture-fw
pio run -t upload          # board: ESP32-C3 mini/SuperMini, native USB port
pio device monitor         # EVT/LVL lines visible immediately

# to log a CSV file (from ../tools/):
python3 ../tools/sw_adc_logger.py /dev/ttyACM0 ../data/logs/sw_ladder_test.csv
```

Suggested test protocol (same spirit as the CAN capture, but this signal is
per-press, no merge window):

1. 30s untouched (idle level + noise).
2. Every button on the pod, one at a time: 3 short presses + 1 held for 2s,
   noting the order.
3. Repeat the whole round once more (plateau repeatability).

Expected log output per press:

```
EVT 41213 2497 -> 812 (prev held 5210 ms)   <- MODE pressed
EVT 41455 812 -> 2496 (prev held 242 ms)    <- released (held 242ms)
```

Table to fill in with the results (goes into
[`../docs/signal_findings.md`](../docs/signal_findings.md)):

| Button | mV at pin (real idle bias) | V on line | notes |
|---|---|---|---|
| (idle) | | | |
| MODE | | | |
| VOL+ / VOL- / SEEK / ... | | | |

## After characterization

With the plateau table in hand, integrating this into the cluster is an
ADC threshold + debounce (~20 lines in the `firmware` codebase, immediate
per-press events — no 2Hz broadcast, no merge window for close-together
presses). At that point, decide whether to read the ladder directly from
the cluster's own ESP32-S3 (same divider) or keep the C3 as a bridge. The
electrical constraint stays the same either way: high impedance, never
bias the line while the CAN box is connected.
