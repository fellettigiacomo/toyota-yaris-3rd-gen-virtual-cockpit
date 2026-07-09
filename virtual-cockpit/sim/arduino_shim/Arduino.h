#pragma once

// Minimal stand-in for the handful of Arduino symbols can_decoder.cpp's
// DEMO_FAKE_DATA branch needs (millis(), Serial.println/printf) -- just
// enough to compile that file unmodified against a desktop toolchain. Put
// this directory ahead of the real project headers on the include path so
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
