#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Live candump-format CAN stream over the same USB-CDC port used for the
// serial monitor, for tools/usb_stream_logger.py (or SavvyCAN/socat/etc. on
// the other end) to capture in real time without pulling the SD card.
//
// Off by default: the SD card remains the source of truth, this is purely
// a debug/live-view add-on, so it starts disabled and must be turned on
// explicitly (see main.cpp's "STREAM ON"/"STREAM OFF" serial commands) to
// avoid flooding a serial monitor session that isn't expecting raw frames.
namespace UsbStream {

// Creates the queue, spawns usbStreamTask pinned to CORE_SD_AND_DISPLAY at
// TASK_PRIO_USB_STREAM, and returns the queue that canRxTask should push
// CanFrameRecord items onto. Must be called after Serial.begin().
QueueHandle_t begin();

// Enables/disables writing candump lines to Serial. Recomputes the
// time base from the current RTC state on every enable, so a SETTIME sent
// after boot but before streaming is picked up. When disabled, queued
// frames are simply dropped (not buffered) so re-enabling starts clean.
void setEnabled(bool enabled);
bool isEnabled();

} // namespace UsbStream
