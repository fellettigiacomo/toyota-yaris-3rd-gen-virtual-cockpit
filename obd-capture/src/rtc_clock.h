#pragma once

#include <cstdint>
#include <ctime>

// Thin wrapper around the onboard PCF85063 RTC (SensorLib). The board has no
// GPS/network time source of its own, so the RTC must be set once, either
// over serial ("SETTIME <unix_epoch_seconds>") or via NTP (see main.cpp).
namespace RtcClock {

void begin();
bool isValid();
time_t now();
void set(time_t epochSeconds);

} // namespace RtcClock
