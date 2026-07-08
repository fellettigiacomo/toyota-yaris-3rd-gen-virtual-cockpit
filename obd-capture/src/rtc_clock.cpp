#include "rtc_clock.h"
#include "board_pins.h"
#include <Wire.h>
#include <SensorPCF85063.hpp>

namespace {
SensorPCF85063 g_rtc;
bool g_present = false;

// The PCF85063 has no dedicated "time is valid" flag exposed by SensorLib.
// Heuristic: a chip that has never been set (power-on default, or battery
// fully discharged) reports a year well before this firmware was written.
constexpr uint16_t RTC_PLAUSIBLE_MIN_YEAR = 2024;
} // namespace

namespace RtcClock {

void begin() {
    g_present = g_rtc.begin(Wire, PIN_SENSOR_I2C_SDA, PIN_SENSOR_I2C_SCL);
    if (!g_present) {
        Serial.println("[rtc_clock] WARNING: PCF85063 not found, timestamps will use a fixed fallback epoch");
    }
}

bool isValid() {
    if (!g_present) return false;
    RTC_DateTime dt = g_rtc.getDateTime();
    return dt.getYear() >= RTC_PLAUSIBLE_MIN_YEAR;
}

time_t now() {
    if (!g_present) return 0;
    struct tm t = g_rtc.getDateTime().toUnixTime();
    return mktime(&t);
}

void set(time_t epochSeconds) {
    if (!g_present) return;
    struct tm t;
    gmtime_r(&epochSeconds, &t);
    g_rtc.setDateTime(static_cast<uint16_t>(t.tm_year + 1900),
                       static_cast<uint8_t>(t.tm_mon + 1),
                       static_cast<uint8_t>(t.tm_mday),
                       static_cast<uint8_t>(t.tm_hour),
                       static_cast<uint8_t>(t.tm_min),
                       static_cast<uint8_t>(t.tm_sec));
}

} // namespace RtcClock
