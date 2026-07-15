#pragma once

#include <cstdint>

// --- ADC input ---
// GPIO3 = ADC1_CH3 on the ESP32-C3. Deliberately NOT GPIO2/8/9 (C3
// strapping pins) and NOT GPIO5 (ADC2, unreliable/deprecated on C3).
// GPIO0/1/3/4 are all fine ADC1 alternatives if 3 is inconvenient.
constexpr int PIN_SW_ADC = 3;

// 11dB attenuation reads ~0-2500mV usable range on the C3. The wiring
// (README.md) puts a 2:1 divider in front of the pin, so line voltages up
// to ~5V land inside that range no matter whether the Simplesoft box
// biases the SW line to 3.3V or 5V.
// Reported values are pin-side millivolts (calibrated, via
// analogReadMilliVolts); multiply by the divider ratio for line-side volts.

// --- Sampling ---
// The ladder is a plain DC level; 1kHz sampling with a median-of-5 per
// sample is far more temporal resolution than a human button press needs,
// while still catching a quick tap's full plateau.
constexpr uint32_t SAMPLE_INTERVAL_US = 1000;
constexpr int MEDIAN_SAMPLES = 5;

// --- Event detector (always on) ---
// A "level change" event fires when the median moves more than
// EVENT_THRESHOLD_MV away from the current stable level and stays on the
// new level for EVENT_DEBOUNCE_MS. Ladder steps are hundreds of mV apart
// (that's the whole point of a resistive ladder), so 60mV cleanly
// separates real steps from ADC noise; 15ms rides out contact bounce.
constexpr int EVENT_THRESHOLD_MV = 60;
constexpr uint32_t EVENT_DEBOUNCE_MS = 15;

// Periodic reminder of the current stable level, so drift or a slowly
// dying bias is visible even with no button activity.
constexpr uint32_t LEVEL_HEARTBEAT_MS = 5000;

// --- Raw stream (optional, "STREAM ON"/"STREAM OFF") ---
// One CSV line per window with min/avg/max of the calibrated millivolts,
// for offline plots. 20ms windows = 50 lines/s, comfortably within USB-CDC.
constexpr uint32_t STREAM_WINDOW_MS = 20;

// --- Serial ---
constexpr uint32_t SERIAL_BAUD = 115200; // native USB-CDC ignores it, kept for consistency
