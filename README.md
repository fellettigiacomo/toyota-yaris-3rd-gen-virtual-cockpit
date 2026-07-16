# Toyota Yaris (3rd Gen / XP130) — Virtual Cockpit

![Platform](https://img.shields.io/badge/platform-ESP32--S3-3aa0ff)
![UI](https://img.shields.io/badge/UI-LVGL%208.3-00e5ff)
![Status](https://img.shields.io/badge/status-hardware%20bring--up-e03a2f)

A from-scratch digital instrument cluster for a 2014 Toyota Yaris Hybrid
(3rd gen / XP130, THS-II NoDSU), driven entirely by the car's own
**reverse-engineered CAN bus** — no dealer tools, no published DBC, no OEM
documentation.

The is divided in three main folders:

1. **Reverse-engineering** (`re/`) — a
   CAN bus sniffer, SW sniffer, all the documentation and the
   Python tooling and findings used to decode signal by signal.
2. **CAN Database (DBC)** (`dbc/`) — the CAN Database with all the main commands and their translation
3. **The dashboard** (`firmware/`) — an LVGL-based instrument cluster
   firmware that decodes the resulting DBC live and drives a physical
   display strip mounted in the car.

> **Safety note.** The firmware only ever *listens* to the vehicle's CAN bus
> (`TWAI_MODE_LISTEN_ONLY` — no transmit, no ACK, per the ESP-IDF driver
> contract). It cannot send commands to the car. That said, this taps a real
> vehicle's diagnostic bus: read the staged bring-up procedure in
> [`obd-capture/README.md`](obd-capture/README.md) before connecting anything
> to a running car.

## Screens

The cluster cycles through three screens with the board's physical **BOOT
button** (touch was tried and dropped — laggy on real hardware):

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
  [`obd-capture/README.md`](obd-capture/README.md).

## Repository layout

```
.
├── virtual-cockpit/     # Dashboard firmware (LVGL) + desktop simulator
│   ├── include/         #   board pins, VehicleState struct, LVGL config
│   ├── src/
│   │   ├── ui/          #   screens: cockpit_ui, energy_flow_ui, efficiency_ui
│   │   └── can_decoder.cpp  #   DBC-driven CAN -> VehicleState decode
│   └── sim/             #   SDL2 desktop simulator, no board required
├── obd-capture/         # Earlier firmware: passive CAN sniffer + SD logger
├── sw-capture/          # ESP32-C3 ADC logger for the steering-wheel switch
│                        #   resistive ladder (MODE button is analog, not CAN)
├── dbc/                 # Reverse-engineered DBC (toyota_yaris_xp130_reversed.dbc)
├── docs/                # Signal reverse-engineering notes and findings
├── tools/                # Python scripts used to reverse-engineer the bus
└── data/logs/            # Sample real-drive CAN captures (candump format)
```

## Building the dashboard firmware

Requires [PlatformIO](https://platformio.org/) (CLI or VS Code extension).

```sh
cd virtual-cockpit
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
cd virtual-cockpit/sim
cmake -B build
cmake --build build -j
./build/sim
```

Press SPACE to cycle screens, same as the board's BOOT button. Details and
known rough edges in [`virtual-cockpit/sim/README.md`](virtual-cockpit/sim/README.md).

## Building the CAN sniffer (`obd-capture`)

```sh
cd obd-capture
pio run
pio run -t upload
pio device monitor
```

Logs every frame to SD in `candump`-compatible text, and can stream the same
data live over USB (`STREAM ON` at the serial console) for capture straight
to a PC via `tools/usb_stream_logger.py`. Full bring-up order, wiring, and
safety steps in [`obd-capture/README.md`](obd-capture/README.md).

## Reverse-engineering workflow

1. Capture raw frames with `obd-capture` → SD card, `candump` format.
2. Analyze with the scripts in `tools/` (`id_stats.py`, `byte_stats.py`,
   `find_soc_like.py`, `decode_dbc.py`, `plot_soc_timeline.py`, …) or import
   into SavvyCAN / `python-can` / `cantools`.
3. Confirmed signals go into [`dbc/toyota_yaris_xp130_reversed.dbc`](dbc/toyota_yaris_xp130_reversed.dbc),
   with methodology and evidence in [`docs/signal_findings.md`](docs/signal_findings.md)
   and a quick-reference table in [`docs/decoded_signals_summary.md`](docs/decoded_signals_summary.md).
4. `virtual-cockpit/src/can_decoder.cpp` decodes the same signals live on
   the dashboard.

Speed, RPM, gear, pedals, EV-drive, HV battery SOC, and the CHG/ECO/PWR
gauge are all confirmed on two independent real-drive logs. HV battery
current/voltage and remaining range are confirmed **absent** from this bus.

## Status

Hobby, single-vehicle reverse-engineering project — signal mappings may not
carry over to other trims/markets/model years. Parts of this repo have not
yet been compiled or run on real hardware (see the caveats in
`obd-capture/README.md` and `virtual-cockpit/sim/README.md`); expect to
iterate on first boot.

## Contributing

Issues and PRs are welcome, especially signal validation on other Yaris
Hybrid units — please note your model year/market if you're confirming or
contradicting a signal in `docs/signal_findings.md`.

## License

No license has been chosen yet.

## Acknowledgments

- [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32)
- [lewisxhe/SensorsLib](https://github.com/lewisxhe/SensorsLib)
- [LVGL](https://lvgl.io/) 8.3
- Waveshare's official ESP32-S3-Touch-LCD-3.49 example code (display/board
  bring-up reference)
