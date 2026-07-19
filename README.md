# Toyota Yaris (3rd Gen / XP130) — Virtual Cockpit

![Platform](https://img.shields.io/badge/platform-ESP32--S3-3aa0ff)
![UI](https://img.shields.io/badge/UI-LVGL%208.3-00e5ff)
![Status](https://img.shields.io/badge/status-hardware%20bring--up-e03a2f)

A from-scratch digital instrument cluster for a 2014 Toyota Yaris Hybrid
(3rd gen / XP130, THS-II NoDSU), driven entirely by the car's own
**reverse-engineered CAN bus** — no dealer tools, no published DBC, no OEM
documentation.

The repository is divided into three main folders:

1. **Reverse-engineering** (`re/`) — a CAN bus sniffer and all the
   documentation, Python tooling and findings used to decode the bus
   signal by signal.
2. **CAN Database (DBC)** (`dbc/`) — the CAN Database with all the decoded
   signals and their translation formulas.
3. **3D Models** (`3d/`) — Fusion360 Project File and 3MF print files to 3d print the enclosure
4. **The dashboard** (`firmware/`) — an LVGL-based instrument cluster
   firmware that decodes the resulting DBC live and drives a physical
   display strip mounted in the car.

> **Safety note.** The firmware only ever *listens* to the vehicle's CAN bus
> (`TWAI_MODE_LISTEN_ONLY` — no transmit, no ACK, per the ESP-IDF driver
> contract). It cannot send commands to the car. That said, this taps a real
> vehicle's diagnostic bus: read the staged bring-up procedure in
> [`re/obd-capture-fw/README.md`](re/obd-capture-fw/README.md) before
> connecting anything to a running car.

## Screens

The cluster cycles through three screens either with the board's physical
**BOOT button**, or by **tapping anywhere on the panel** (presence-only
touch, no coordinates/gestures — an earlier swipe-based `lv_tileview`
navigation was dropped for being laggy on real hardware, but a discrete tap
doesn't have that problem):

| Screen | Shows |
|---|---|
| **Cockpit** | Speed, gear (or 0–50 / 0–100 accel timer), RPM/EV state, CHG/PWR power-flow bar, HV battery gauge |
| **Energy Flow** | Animated ENGINE / MOTOR / BATTERY / WHEELS diagram, Prius-style flow arrows |
| **Efficiency** | Session stats: EV vs. engine share, distance, regen %, avg/max speed |

## Hardware

- **Board**: Waveshare ESP32-S3-Touch-LCD-3.49 (ESP32-S3R8, 8 MB PSRAM,
  16 MB flash) — AXS15231B QSPI display, physical panel 172×640 portrait,
  rotated in software to a 640×172 landscape strip.
- **CAN transceiver**: SN65HVD230, wired to OBD-II pins 6 (CAN-H) / 14
  (CAN-L). Full wiring table and GPIO rationale in
  [`re/obd-capture-fw/README.md`](re/obd-capture-fw/README.md).

## Repository layout

```
.
├── dbc/                     # Reverse-engineered DBC (toyota_yaris_xp130_reversed.dbc)
├── firmware/                # Dashboard firmware (LVGL) + desktop simulator
│   ├── include/             #   board pins, VehicleState struct, LVGL config
│   ├── src/
│   │   ├── ui/               #   screens: cockpit_ui, energy_flow_ui, efficiency_ui
│   │   └── can_decoder.cpp   #   DBC-driven CAN -> VehicleState decode
│   └── sim/                 #   SDL2 desktop simulator, no board required
└── re/                      # Reverse-engineering workspace
    ├── obd-capture-fw/      #   ESP32-S3 passive CAN sniffer + SD logger
    ├── docs/                #   Signal reverse-engineering notes and findings
    ├── tools/               #   Python scripts used to reverse-engineer the bus
    └── data/logs/           #   Sample real-drive CAN captures (candump format)
```

## Building the dashboard firmware

Requires [PlatformIO](https://platformio.org/) (CLI or VS Code extension).

```sh
cd firmware
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial console @ 115200
```

No car handy? Build the `demo` environment instead — it sweeps `VehicleState`
with fake data and skips CAN entirely:

```sh
pio run -e demo -t upload
```

## Desktop simulator

Iterate on screen UI without any hardware, replaying a real drive log in an
SDL2 window (macOS, via Homebrew):

```sh
brew install sdl2 cmake
cd firmware/sim
cmake -B build
cmake --build build -j
./build/sim
```

Press SPACE to cycle screens, same as the board's BOOT button. Details and
known rough edges in [`firmware/sim/README.md`](firmware/sim/README.md).

## Building the CAN sniffer (`re/obd-capture-fw`)

```sh
cd re/obd-capture-fw
pio run
pio run -t upload
pio device monitor
```

Logs every frame to SD in `candump`-compatible text, and can stream the same
data live over USB (`STREAM ON` at the serial console) for capture straight
to a PC via `re/tools/usb_stream_logger.py`. Full bring-up order, wiring, and
safety steps in [`re/obd-capture-fw/README.md`](re/obd-capture-fw/README.md).

## Reverse-engineering workflow

1. Capture raw frames with `re/obd-capture-fw` → SD card, `candump` format.
2. Analyze with the scripts in [`re/tools/`](re/tools/) (`id_stats.py`,
   `byte_stats.py`, `decode_dbc.py`, `cross_validate_candidates.py`, …) or
   import into SavvyCAN / `python-can` / `cantools`.
3. Confirmed signals go into [`dbc/toyota_yaris_xp130_reversed.dbc`](dbc/toyota_yaris_xp130_reversed.dbc),
   with methodology and evidence in [`re/docs/signal_findings.md`](re/docs/signal_findings.md)
   and a quick-reference table in [`re/docs/decoded_signals_summary.md`](re/docs/decoded_signals_summary.md).
4. `firmware/src/can_decoder.cpp` decodes the same signals live on
   the dashboard.

Speed, RPM, gear, pedals, EV-drive, HV battery SOC, and the CHG/ECO/PWR
gauge are all confirmed on two independent real-drive logs. HV battery
current/voltage and remaining range are confirmed **absent** from this bus.

## Status

Hobby, single-vehicle reverse-engineering project — signal mappings may not
carry over to other trims/markets/model years. Parts of this repo have not
yet been compiled or run on real hardware (see the caveats in
`re/obd-capture-fw/README.md` and `firmware/sim/README.md`); expect to
iterate on first boot.

## Contributing

Issues and PRs are welcome, especially signal validation on other Yaris
Hybrid units — please note your model year/market if you're confirming or
contradicting a signal in `re/docs/signal_findings.md`.

## License

No license has been chosen yet.

## Acknowledgments

- [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32)
- [lewisxhe/SensorsLib](https://github.com/lewisxhe/SensorsLib)
- [LVGL](https://lvgl.io/) 8.3
- Waveshare's official ESP32-S3-Touch-LCD-3.49 example code (display/board
  bring-up reference)
