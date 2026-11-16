#pragma once

#include <cstdint>

// Live candump of every CAN frame over the board's USB port, built into the
// cockpit firmware so capturing the bus no longer means flashing
// re/obd-capture-fw.
//
// Speaks the same serial protocol as obd-capture's USB stream, so
// re/tools/usb_stream_logger.py drives it unchanged:
//   STREAM ON        start streaming candump lines
//   STREAM OFF       stop
//   SETTIME <epoch>  host wall-clock time, for absolute timestamps (this
//                    firmware keeps no clock of its own)
// Lines are byte-for-byte the SocketCAN candump/.log format obd-capture
// writes: "(<sec>.<usec>) can0 <id>#<data>\n".
//
// Started by the host, not by the cable being plugged in: a USB host is all
// the board can detect, and it can't tell a laptop from a car head unit or a
// powered hub -- and until a program actually opens the port nothing would
// read the frames anyway. It does stop by itself when the USB host goes
// away or stops reading, so a yanked cable never leaves it streaming.
namespace UsbDump {

// Creates the frame queue and the streaming task. Call after Serial.begin().
void begin();

// True while streaming (the cockpit shows "CAN DUMP USB" while it is).
bool active();

// Hands one received frame to the stream. Non-blocking and cheap when not
// streaming -- safe to call for every frame from the CAN RX task. When the
// queue is full the frame is dropped and counted, never waited on.
void push(uint32_t id, bool extended, bool rtr, uint8_t dlc, const uint8_t *data);

} // namespace UsbDump
