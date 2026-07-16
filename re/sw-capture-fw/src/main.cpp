// sw-capture: characterize the steering-wheel switch resistive ladder via
// the ESP32-C3's ADC. The steering audio switches (MODE/VOL/SEEK...) never
// transit on CAN on this car (see docs/signal_findings.md Addendum 5
// retraction); each button instead closes a distinct resistance across the
// SW pin pair of the 28-pin radio connector, and whoever reads them (the
// Simplesoft CAN box in the stock setup) does so as an analog level.
//
// This firmware turns button activity into two serial outputs:
//
//   EVT lines (always on) -- debounced stable-level changes:
//     EVT <t_ms> <prev_mV> -> <new_mV> (prev held <ms> ms)
//   Press MODE a few times and the ladder table falls straight out of the
//   log: idle level, one distinct plateau per button, back to idle.
//
//   CSV stream (optional, "STREAM ON"/"STREAM OFF", same command style as
//   obd-capture) -- one "<t_ms>,<min_mV>,<avg_mV>,<max_mV>" line per
//   STREAM_WINDOW_MS for offline plotting; tools/sw_adc_logger.py captures
//   exactly these lines to a file.
//
// Values are pin-side calibrated millivolts (analogReadMilliVolts). With
// the 2:1 divider from README.md, line volts = reported mV * 2 / 1000.
// Single-core C3, DC-slow signal: a plain paced loop() is all this needs
// -- no tasks, no queues.

#include <Arduino.h>
#include <algorithm>

#include "app_config.h"

namespace {

bool g_streamOn = false;

// --- event detector state ---
int g_stableMv = -1; // -1: not initialized yet (first sample seeds it)
uint32_t g_stableSinceMs = 0;
int g_candidateMv = -1;
uint32_t g_candidateSinceMs = 0;
long g_candidateSumMv = 0;
int g_candidateCount = 0;
uint32_t g_lastHeartbeatMs = 0;

// --- stream window state ---
int g_winMinMv = 0;
int g_winMaxMv = 0;
long g_winSumMv = 0;
int g_winCount = 0;
uint32_t g_winStartMs = 0;

int readMedianMv() {
    int v[MEDIAN_SAMPLES];
    for (int i = 0; i < MEDIAN_SAMPLES; i++) {
        v[i] = static_cast<int>(analogReadMilliVolts(PIN_SW_ADC));
    }
    std::sort(v, v + MEDIAN_SAMPLES);
    return v[MEDIAN_SAMPLES / 2];
}

void resetCandidate() {
    g_candidateMv = -1;
    g_candidateSumMv = 0;
    g_candidateCount = 0;
}

void detectEvents(int mv, uint32_t nowMs) {
    if (g_stableMv < 0) {
        g_stableMv = mv;
        g_stableSinceMs = nowMs;
        Serial.printf("LVL %lu %d (initial)\n", static_cast<unsigned long>(nowMs), mv);
        return;
    }

    if (abs(mv - g_stableMv) <= EVENT_THRESHOLD_MV) {
        // back on (or still on) the stable level -- discard any candidate
        resetCandidate();
    } else if (g_candidateMv < 0 || abs(mv - g_candidateMv) > EVENT_THRESHOLD_MV) {
        // first sample off-level, or the excursion moved again (multi-step
        // transition still settling): restart the candidate window
        g_candidateMv = mv;
        g_candidateSinceMs = nowMs;
        g_candidateSumMv = mv;
        g_candidateCount = 1;
    } else {
        g_candidateSumMv += mv;
        g_candidateCount++;
        if (nowMs - g_candidateSinceMs >= EVENT_DEBOUNCE_MS) {
            int newLevel = static_cast<int>(g_candidateSumMv / g_candidateCount);
            Serial.printf("EVT %lu %d -> %d (prev held %lu ms)\n",
                          static_cast<unsigned long>(nowMs), g_stableMv, newLevel,
                          static_cast<unsigned long>(nowMs - g_stableSinceMs));
            g_stableMv = newLevel;
            g_stableSinceMs = nowMs;
            resetCandidate();
        }
    }

    if (nowMs - g_lastHeartbeatMs >= LEVEL_HEARTBEAT_MS) {
        g_lastHeartbeatMs = nowMs;
        Serial.printf("LVL %lu %d\n", static_cast<unsigned long>(nowMs), g_stableMv);
    }
}

void streamWindow(int mv, uint32_t nowMs) {
    if (g_winCount == 0) {
        g_winStartMs = nowMs;
        g_winMinMv = g_winMaxMv = mv;
        g_winSumMv = 0;
    }
    g_winMinMv = std::min(g_winMinMv, mv);
    g_winMaxMv = std::max(g_winMaxMv, mv);
    g_winSumMv += mv;
    g_winCount++;

    if (nowMs - g_winStartMs >= STREAM_WINDOW_MS) {
        if (g_streamOn) {
            Serial.printf("%lu,%d,%ld,%d\n", static_cast<unsigned long>(g_winStartMs),
                          g_winMinMv, g_winSumMv / g_winCount, g_winMaxMv);
        }
        g_winCount = 0;
    }
}

void handleSerialCommands() {
    static String line;
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c != '\n' && c != '\r') {
            line += c;
            continue;
        }
        if (line.length() == 0) {
            continue;
        }
        if (line == "STREAM ON") {
            g_streamOn = true;
            Serial.println("[main] CSV stream ON (t_ms,min_mV,avg_mV,max_mV) -- 'STREAM OFF' to stop");
        } else if (line == "STREAM OFF") {
            g_streamOn = false;
            Serial.println("[main] CSV stream OFF");
        } else {
            Serial.println("[main] unknown command. Known: STREAM ON, STREAM OFF");
        }
        line = "";
    }
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300); // let USB-CDC/serial monitor attach

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_SW_ADC, ADC_11db);

    Serial.println("\n[main] sw-capture starting");
    Serial.printf("[main] ADC on GPIO%d, %dHz median-of-%d, event threshold %dmV / %lums\n",
                  PIN_SW_ADC, static_cast<int>(1000000 / SAMPLE_INTERVAL_US), MEDIAN_SAMPLES,
                  EVENT_THRESHOLD_MV, static_cast<unsigned long>(EVENT_DEBOUNCE_MS));
    Serial.println("[main] values are pin-side mV; line V = mV * divider ratio (see README)");
    Serial.println("[main] EVT/LVL lines always on; 'STREAM ON' for the CSV stream");
}

void loop() {
    static uint32_t nextSampleUs = micros();

    // paced sampling: catch up without drift, but never spin on overrun
    int32_t behindUs = static_cast<int32_t>(micros() - nextSampleUs);
    if (behindUs < 0) {
        delayMicroseconds(static_cast<uint32_t>(-behindUs));
    }
    nextSampleUs += SAMPLE_INTERVAL_US;
    if (static_cast<int32_t>(micros() - nextSampleUs) > static_cast<int32_t>(10 * SAMPLE_INTERVAL_US)) {
        nextSampleUs = micros(); // fell way behind (USB stall etc.) -- resync
    }

    int mv = readMedianMv();
    uint32_t nowMs = millis();

    detectEvents(mv, nowMs);
    streamWindow(mv, nowMs);
    handleSerialCommands();
}
