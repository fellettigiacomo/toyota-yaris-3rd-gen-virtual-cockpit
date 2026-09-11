#pragma once

#include "vehicle_state.h"

#include <cstdint>

// Turns raw QMI8658 samples (imu.h, chip axes, gravity included) into the
// forward/lateral g the G-meter screen draws, by working out how the board
// is actually mounted in the car instead of assuming it.
//
// Same split as the rest of this project: 100% of the math lives here,
// gmeter_ui.cpp does none of it -- it only ever reads the Reading below.
// update() is cheap and must be called every UI tick regardless of which
// screen is visible, so the calibration keeps converging and the session
// peaks keep accumulating while the driver is looking at the cockpit.
namespace GMeter {

enum class Phase : uint8_t {
    NoSensor, // no IMU answered on the bus
    Zeroing,  // waiting for a standstill long enough to capture which way is up
    Learning, // up is known; still working out which way the car points
    Ready,
};

struct Reading {
    Phase phase = Phase::Zeroing;

    // Signed, in g, in VEHICLE axes -- the whole point of this module.
    float longG = 0.0f; // + accelerating, - braking
    float latG = 0.0f;  // + right-hand turn, - left-hand turn
    float magG = 0.0f;  // magnitude of the horizontal vector (longG, latG)

    // Session peaks, each an unsigned magnitude of its own direction.
    float peakAccelG = 0.0f;
    float peakBrakeG = 0.0f;
    float peakLeftG = 0.0f;
    float peakRightG = 0.0f;

    float learnPct = 0.0f; // 0-100, how far the forward-axis fit has converged
};

void begin();
void update(const VehicleState &state);
Reading get();

} // namespace GMeter
