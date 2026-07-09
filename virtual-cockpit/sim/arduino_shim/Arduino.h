#pragma once

// Minimal stand-in for the handful of Arduino symbols can_decoder.cpp's
// DEMO_FAKE_DATA branch and screen_nav.cpp need (millis(),
// Serial.println/printf, pinMode/digitalRead) -- just enough to compile
// those files unmodified against a desktop toolchain. Put this directory
// ahead of the real project headers on the include path so
// `#include <Arduino.h>` resolves here instead.

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

inline uint32_t millis() {
    static const auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

struct SerialShim {
    void println(const char *s) { std::printf("%s\n", s); }
    void printf(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::vprintf(fmt, args);
        va_end(args);
    }
};
inline SerialShim Serial;

// screen_nav.cpp reads the board's BOOT button via pinMode()/digitalRead().
// There's no real GPIO on a desktop -- main_sim.cpp flips this flag from the
// spacebar instead, standing in for a press of that physical button.
inline bool g_simButtonHeld = false;

constexpr int INPUT_PULLUP = 0;
constexpr int LOW = 0;
constexpr int HIGH = 1;

inline void pinMode(int, int) {}
inline int digitalRead(int) { return g_simButtonHeld ? LOW : HIGH; }
