#!/usr/bin/env python3
"""Capture the sw-capture firmware's ADC output to a file.

Requires `pip install pyserial`. Same idea as usb_stream_logger.py for
obd-capture, but for the steering-wheel switch ladder logger (sw-capture/):
sends 'STREAM ON', writes every CSV window line (t_ms,min_mV,avg_mV,max_mV)
AND every EVT/LVL event line to the output file, echoes events and firmware
console messages to the terminal so button presses are visible live, and
sends 'STREAM OFF' on exit.

The output file interleaves two strict line shapes:
    <t_ms>,<min_mV>,<avg_mV>,<max_mV>          the 20ms CSV windows
    EVT <t_ms> <prev_mV> -> <new_mV> (...)     debounced level changes
    LVL <t_ms> <mV> ...                        stable-level heartbeats
so a plot script can split them on the first comma/space token.

Usage:
    sw_adc_logger.py PORT [OUTPUT] [--baud BAUD]

Examples:
    sw_adc_logger.py /dev/ttyACM0
    sw_adc_logger.py COM7 data/logs/sw_ladder_test.csv
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

CSV_RE = re.compile(r"^\d+,-?\d+,-?\d+,-?\d+$")
EVT_RE = re.compile(r"^(EVT|LVL)\s+\d+\s")

DEFAULT_LOG_DIR = Path(__file__).resolve().parent.parent / "data" / "logs"


def default_output_path():
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return DEFAULT_LOG_DIR / f"sw_{stamp}.csv"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. /dev/ttyACM0 or COM7")
    ap.add_argument("output", nargs="?", type=Path, default=None,
                    help="output path (default: data/logs/sw_<timestamp>.csv)")
    ap.add_argument("--baud", type=int, default=115200,
                    help="nominal baud rate (native USB-CDC ignores it, but pyserial wants one)")
    args = ap.parse_args()

    output = args.output or default_output_path()
    output.parent.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.3)  # let the port settle before writing commands
    ser.write(b"STREAM ON\n")

    written = 0
    events = 0
    try:
        with open(output, "w") as out:
            print(f"logging to {output} -- Ctrl-C to stop")
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode(errors="replace").strip()
                if CSV_RE.match(line):
                    out.write(line + "\n")
                    written += 1
                elif EVT_RE.match(line):
                    out.write(line + "\n")
                    written += 1
                    events += 1
                    print(line)  # button activity is the interesting bit -- echo it
                elif line:
                    print(f"[fw] {line}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.write(b"STREAM OFF\n")
        time.sleep(0.2)
        ser.close()
        print(f"\n{written} lines written ({events} EVT/LVL) to {output}")


if __name__ == "__main__":
    main()
