#pragma once

// Minimal stand-in for the Arduino Wire (I2C) API touch_nav.cpp needs,
// enough to compile that file unmodified against a desktop toolchain --
// same idea as this directory's Arduino.h shim for screen_nav.cpp's GPIO
// read. There's no real touch controller on a desktop, so this fakes the
// AXS15231B read response (see touch_nav.cpp) from a flag main_sim.cpp sets
// on left mouse button down/up, standing in for a finger touching the
// panel anywhere (TouchNav only ever looks at the point-count byte, never
// coordinates, so "anywhere" is exactly what this needs to simulate).

#include <cstddef>
#include <cstdint>

inline bool g_simTouchHeld = false;

class WireShim {
public:
    void begin(int, int) {}
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t) {}
    size_t write(const uint8_t *, size_t len) { return len; }
    uint8_t endTransmission() { return 0; }

    int requestFrom(int, int len) {
        m_readIdx = 0;
        m_len = len;
        return len;
    }
    int available() { return m_readIdx < m_len ? 1 : 0; }
    int read() {
        // Byte layout matches touch_axs15231b_read_data()'s response frame:
        // [0]=gesture, [1]=point count (all touch_nav.cpp reads), [2:]=coords.
        int value = (m_readIdx == 1) ? (g_simTouchHeld ? 1 : 0) : 0;
        m_readIdx++;
        return value;
    }

private:
    int m_readIdx = 0;
    int m_len = 0;
};

inline WireShim Wire;
