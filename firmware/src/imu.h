#pragma once

// Onboard QMI8658 6-axis IMU (I2C 0x6B on the board's sensor bus, GPIO47/48
// -- see board_pins.h). Only the accelerometer is used: the G-meter screen
// wants linear acceleration, not attitude, so the gyro stays powered down.
//
// This header is the hardware seam. imu.cpp talks to the real chip and is
// compiled only into the firmware; the desktop simulator links its own
// sim/imu_sim.cpp against this same declaration instead (there is no QMI8658
// on a desktop), which is what lets g_meter.cpp's calibration math -- the
// part actually worth testing -- run unmodified in both.
namespace Imu {

// One accelerometer reading, in g, in the CHIP's own axes. Nothing here
// knows how the board is mounted in the car; turning these into
// forward/lateral is g_meter.cpp's job.
struct Sample {
    bool valid = false;
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
};

// Brings the accelerometer up. Safe to call with no chip attached: present()
// then stays false and read() only ever returns invalid samples, which the
// G-meter screen reports as "NO SENSOR" rather than as a genuine zero g.
void begin();

// False until a chip answers on the bus. read() re-probes periodically, so
// this can turn true later than begin().
bool present();

// Latest reading. Cheap enough to call at the UI sync rate (~30 Hz).
Sample read();

} // namespace Imu
