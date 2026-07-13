#!/usr/bin/env python3
"""Capture the obd-capture firmware's live USB candump stream to a file.

Requires `pip install pyserial`. Talks to the board over the same USB-CDC
port used for flashing/serial monitor: sends 'STREAM ON', writes every
candump line it receives straight to the output file (same format as the
SD card logs, so it drops straight into parse_log.py/SavvyCAN/cantools),
and sends 'STREAM OFF' on exit so the board falls back to its normal
console output.

Usage:
    usb_stream_logger.py PORT [OUTPUT] [--baud BAUD] [--settime]

Examples:
    usb_stream_logger.py /dev/ttyACM0
    usb_stream_logger.py COM5 data/logs/bench_test.log --settime
"""
import argparse
import datetime
import re
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

# Same strict candump-line shape parse_log.py accepts -- anything else
# (status lines, session-open/close notices, bus-off warnings) is a firmware
# console message that slipped in and is shown on the console, not written
# to the log file, so the .log stays 100% strict-format.
LINE_RE = re.compile(r"^\(\d+\.\d+\)\s+\S+\s+[0-9A-Fa-f]+#[0-9A-Fa-f]*\s*$")

DEFAULT_LOG_DIR = Path(__file__).resolve().parent.parent / "data" / "logs"


def default_output_path():
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return DEFAULT_LOG_DIR / f"usb_{stamp}.log"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. /dev/ttyACM0 or COM5")
    ap.add_argument("output", nargs="?", type=Path, default=None,
                     help="output .log path (default: data/logs/usb_<timestamp>.log)")
    ap.add_argument("--baud", type=int, default=921600,
                     help="nominal baud rate (native USB-CDC ignores it, but pyserial wants one)")
    ap.add_argument("--settime", action="store_true",
                     help="send SETTIME with the host's current epoch before streaming")
    args = ap.parse_args()

    output = args.output or default_output_path()
    output.parent.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.3)  # let the port settle before writing commands
    ser.reset_input_buffer()

    if args.settime:
        epoch = int(time.time())
        ser.write(f"SETTIME {epoch}\n".encode())
        time.sleep(0.2)
        print(f"[usb_stream_logger] sent SETTIME {epoch}")

    ser.write(b"STREAM ON\n")
    print(f"[usb_stream_logger] streaming {args.port} -> {output} (Ctrl+C to stop)")

    frame_count = 0
    start = time.time()
    try:
        with open(output, "w") as f:
            while True:
                raw = ser.readline()
                if not raw:
                    continue  # read timeout, keep waiting
                line = raw.decode(errors="replace")
                if LINE_RE.match(line.strip()):
                    f.write(line if line.endswith("\n") else line + "\n")
                    frame_count += 1
                    if frame_count % 2000 == 0:
                        f.flush()
                else:
                    stripped = line.strip()
                    if stripped:
                        print(f"[firmware] {stripped}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.write(b"STREAM OFF\n")
        ser.close()
        elapsed = time.time() - start
        print(f"\n[usb_stream_logger] stopped: {frame_count} frames in {elapsed:.1f}s "
              f"-> {output}")


if __name__ == "__main__":
    main()
