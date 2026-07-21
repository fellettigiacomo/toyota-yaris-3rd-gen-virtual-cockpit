# obd-capture-fw

Firmware for the ESP32-S3 (Waveshare ESP32-S3-Touch-LCD-3.49) that passively
sniffs the Toyota Yaris Hybrid (3rd gen / XP130, 2014) OBD-II CAN bus, shows a
live "candump on screen" table on the built-in display, and logs every raw
frame to a microSD card in a format directly importable into SavvyCAN /
python-can / cantools for offline reverse-engineering.

This is step one of the larger virtual-cockpit dashboard project (`../../firmware/`):
Toyota does not publish a DBC for this vehicle, so nothing (EV mode, energy
flow, speed, etc.) can be decoded until the CAN IDs are reverse-engineered
from a capture -- this tool's only job is to capture everything so that can
happen afterwards on a PC.

## Hardware

- **Board**: Waveshare ESP32-S3-Touch-LCD-3.49 (ESP32-S3R8, 8MB PSRAM, 16MB
  flash, silkscreen "Rev1.1"). AXS15231B display (172x640, QSPI), PCF85063
  RTC, onboard microSD slot (SDMMC 1-bit).
- **CAN transceiver**: SN65HVD230 (3.3V, no level shifting needed).

### Wiring

| SN65HVD230 pin | Connect to |
|---|---|
| VCC | 3V3 (expansion header, right column) |
| GND | G (expansion header, right column) |
| TX  | GPIO1 |
| RX  | GPIO2 |
| RS  | GND (always high-speed mode) |
| CANH | OBD-II pin 6 |
| CANL | OBD-II pin 14 |
| (car GND) | OBD-II pin 4 or 5 |

**Important:**
- Leave the SN65HVD230 breakout's onboard 120-ohm termination jumper **open
  / disabled**. The vehicle's CAN bus is already terminated at both ends;
  adding a third 120-ohm resistor degrades signal integrity.
- GPIO1/GPIO2 were chosen from the 22-pin expansion header pins confirmed
  present on this board's silkscreen (0, 1, 2, 3, 4, 5, 19, 20, 38, 39, 40,
  41, 43, 44). Avoided: **GPIO0/3** (boot strapping -- GPIO0 is already wired
  to the BOOT button), **GPIO19/20** (native USB D-/D+ -- this is the same
  USB-C port used for flashing and the serial monitor; wiring anything else
  to these pins conflicts electrically with the USB PHY and will break USB
  communication), **GPIO39/40/41** (used internally by the onboard SD card
  slot), **GPIO43/44** (UART0 / USB-serial bridge), and **SDA/SCL** (GPIO47/48,
  already used internally for the RTC/IMU I2C bus -- also electrically
  incompatible with the transceiver's TX/RX signaling even if it were free).
  GPIO4/GPIO5 are an equally valid alternative pair (also on the header's
  right column) if GPIO1/2 turn out to be inconvenient to wire.
- All onboard peripheral pins (display, RTC, SD) in `include/board_pins.h`
  were extracted directly from Waveshare's own official example code
  (`github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49`), not guessed.

### Physical stop-capture control

There's no touchscreen UI in this tool -- the physical **BOOT button** on the
board is used to safely close the current capture session and start a new
one (drains the SD write queue, closes the file cleanly, writes a `.meta`
sidecar). Touch was intentionally skipped here to avoid depending on the
AXS15231B's proprietary touch protocol for a tool whose only real job is
logging; touch/LVGL is reserved for the later dashboard project.

## Building

Requires [PlatformIO](https://platformio.org/) (CLI or VS Code extension).

```
cd re/obd-capture-fw
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial console (115200 baud)
```

The project uses the [pioarduino](https://github.com/pioarduino/platform-espressif32)
community platform instead of the official PlatformIO `espressif32` platform,
because it ships arduino-esp32 3.x (IDF5-based), needed for the QSPI
`quad_mode` support in `esp_lcd_panel_io_spi_config_t` that the display
driver uses (see `src/WAVESHARE_VENDORED.md`). The official registry
platform was still pinned to an older arduino-esp32 2.0.x core at the time
this was written.

The display is driven via ESP-IDF's `esp_lcd` component and Waveshare's own
panel driver (ported verbatim into `src/axs15231b/`, `src/touch/` -- see
`src/WAVESHARE_VENDORED.md`), the same fix `../../firmware/` uses for the
same panel -- not `GFX Library for Arduino`'s own AXS15231B panel driver,
which produces flicker/split-screen/black-line corruption on this panel over
QSPI (see `src/display_ui.cpp`'s header comment). `GFX Library for Arduino`
is still a dependency, but only for its headless `Arduino_Canvas`
(RAM-only framebuffer + bitmap-font text rendering).

This firmware has **not yet been compiled/tested on real hardware** -- it was
written against Waveshare's official example code and the documented APIs of
`driver/twai.h`/`esp_lcd` (ESP-IDF), `GFX Library for Arduino`, and
`SensorLib` (`lewisxhe/SensorsLib`, the same RTC library Waveshare's own
demos use), but please build it yourself and watch the serial monitor
closely on first boot.

## Setting the clock

The board has no GPS/network time source. Log timestamps use monotonic
`esp_timer_get_time()` deltas regardless, but the *absolute* time in each
session's filename and candump timestamps comes from the onboard PCF85063
RTC, which needs to be set once. Over the serial monitor:

```
SETTIME <unix_epoch_seconds>
```

If the RTC has never been set, sessions still capture correctly, just with
timestamps relative to the ESP32's own boot time (filenames get a
`RTCUNSET` tag instead of a date, and each `.meta` file records
`rtc_valid=0`).

## Live USB streaming (debug / new-feature work without pulling the SD card)

In addition to logging to the SD card, every captured frame can be streamed
live over the same USB-CDC port used for flashing/the serial monitor, in the
same candump text format as the `.log` files. This is a separate fan-out
from `canRxTask` (own queue, own task, see `usb_stream.cpp`), so a slow or
absent PC-side reader can never affect SD logging or drop frames from the
capture.

Over the serial monitor:

```
STREAM ON    # raw candump lines start flowing over the port
STREAM OFF   # back to the normal [status]/[main]/... console output
```

While streaming is on, the periodic `[status]` line is suppressed so the
port carries pure candump lines; occasional other firmware messages (e.g. a
bus-off warning) can still interleave, so anything reading the stream should
just ignore lines that don't match the candump shape.

To capture straight to a file on a PC, use
[`../tools/usb_stream_logger.py`](../tools/usb_stream_logger.py)
(`pip install pyserial`):

```
python3 ../tools/usb_stream_logger.py /dev/ttyACM0
python3 ../tools/usb_stream_logger.py /dev/ttyACM0 ../data/logs/bench_test.log --settime
```

It sends `STREAM ON`, writes every valid candump line to the output file
(filtering out any non-candump firmware messages), and sends `STREAM OFF` +
closes cleanly on Ctrl+C. The resulting file is byte-for-byte the same
format as an SD `.log`, so it drops straight into `../tools/parse_log.py`,
SavvyCAN, `python-can`, or `cantools`.

## Bring-up order (do this in order, do not skip ahead)

1. **Display/SD/RTC only.** Flash and check the serial monitor + screen with
   nothing connected to GPIO1/2 yet. Confirm the stats bar renders, the SD
   card mounts (`SD:` shows free space instead of `NONE`), and the RTC
   responds.
2. **CAN bench test, no vehicle.** Wire the SN65HVD230 to GPIO1/2 (RS to
   GND, termination jumper open) but do **not** connect CANH/CANL to the car
   yet. The TWAI driver runs in `TWAI_MODE_LISTEN_ONLY` -- per the ESP-IDF
   documentation this mode does not transmit or ACK anything on the bus, so
   it is safe to bring up in isolation. Confirm no bus errors are reported
   with nothing connected.
3. **Ignition-accessory only, engine off.** Connect CANH/CANL to OBD-II pins
   6/14 (and ground to pin 4 or 5). Before powering the transceiver, check
   with a multimeter that the bus looks sane (idle CAN-H/CAN-L both settle
   near 2.5V; do this with the transceiver disconnected). Turn the key to
   accessory/on (engine off) and watch the error/bus-off counters on the
   stats bar closely for the first few minutes.
4. **Engine running.** Only once step 3 looks clean, start the engine and
   let a full capture session run.
5. **Stop and pull the card.** Press BOOT to cleanly close the session, then
   remove the SD card and import `/CANLOGS/session_NNNN_*.log` into SavvyCAN,
   `python-can`, or `cantools`.

## Log format

Each `.log` file is plain SocketCAN-`candump`-compatible text, one frame per
line:

```
(1720098765.123456) can0 1d0#0011223344556677
```

- Timestamp: seconds.microseconds (RTC epoch + monotonic delta if the RTC is
  set, otherwise relative to boot).
- `can0` is a literal label (not tied to actual wiring) -- this is what
  generic candump-log importers expect.
- CAN ID: lowercase hex, zero-padded to 3 digits (standard/11-bit) or 8
  digits (extended/29-bit).
- Data: lowercase hex, 2 characters per byte, byte count implied by DLC.
- RTR frames are logged as `id#R` with no data bytes.

A sidecar `.meta` file (same base name) holds session metadata (frame count,
bus-error/bus-off counts, RTC validity, bitrate) -- deliberately **not**
inlined into the `.log` file, so the log stays 100% strict-format and safe
to feed straight into SavvyCAN/python-can/cantools without pre-processing.

## Notes / known limitations

- **No second CAN node was available to test the listen-only mode's
  passivity** (e.g. by deliberately inducing a bus error and watching for any
  dominant bits with a scope). The firmware relies on `TWAI_MODE_LISTEN_ONLY`
  behaving exactly as ESP-IDF documents it (fully passive, no ACK, no
  transmission). Do the ignition-accessory-only bring-up step above before
  ever running with the engine on, and stop immediately if bus errors climb.
- Data written since the last periodic `fsync` (every 5s, see
  `SD_SYNC_INTERVAL_MS` in `include/app_config.h`) can be lost on abrupt power
  loss. Always use the BOOT button to stop a session cleanly before
  disconnecting power.
- Bus load % is an approximation (fixed ~47 bits of framing overhead per
  frame, ignoring CAN bit-stuffing).
- This firmware has not been compiled yet (see "Building" above). The display
  driver was ported from `../../firmware/`'s already-hardware-verified
  `esp_lcd`-based approach (see `src/display_ui.cpp`'s header comment), but
  that specific port has not itself been tested on this project's hardware --
  watch the panel closely on first boot.
