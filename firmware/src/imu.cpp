#include "imu.h"
#include "board_pins.h"

#include <Arduino.h>
#include <Wire.h>

// QMI8658A accelerometer driver, register-level over the plain Arduino Wire
// API -- same call this project already makes for the touch controller (see
// touch_nav.cpp): it is a handful of register writes and one 6-byte burst
// read, and keeping it at that level means the bring-up reads directly
// against the datasheet instead of against a library's abstraction.
//
// Note the bus: the IMU sits on the board's SENSOR I2C bus (GPIO47/48,
// shared with the PCF85063 RTC), which is a DIFFERENT bus from the touch
// controller's (GPIO17/18). touch_nav.cpp owns Wire; this owns Wire1. The
// vendored src/i2c_bsp.c also describes this bus, but nothing calls its
// i2c_master_Init() -- it is reference material, not a live driver, so
// there is no second master to collide with here.
//
// NOT YET VERIFIED ON HARDWARE. The register map, the ODR/full-scale
// encodings and the LPF bits below are transcribed from the QMI8658A
// datasheet, not read back off a working board -- same caveat the desktop
// simulator carries in sim/README.md. The failure mode is benign and
// visible: a wrong WHO_AM_I leaves present() false and the G-meter screen
// says NO SENSOR, rather than the cluster misbehaving.
namespace Imu {

namespace {

// Address lives in board_pins.h with the rest of the board's map, so there is
// one place to look rather than two that can drift.
constexpr uint8_t kAddr = I2C_ADDR_IMU_QMI8658;

// --- QMI8658A register map (datasheet section 8) ---
constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;  // serial interface: bit6 ADDR_AI, bit5 BE
constexpr uint8_t kRegCtrl2 = 0x03;  // accel: bits[6:4] full scale, bits[3:0] ODR
constexpr uint8_t kRegCtrl5 = 0x06;  // sensor data processing: bit0 aLPF_EN, bits[2:1] aLPF_MODE
constexpr uint8_t kRegCtrl7 = 0x08;  // enables: bit0 aEN, bit1 gEN
constexpr uint8_t kRegAccelX = 0x35; // AX_L, then AX_H, AY_L, AY_H, AZ_L, AZ_H
constexpr uint8_t kRegReset = 0x60;

constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kResetCommand = 0xB0;

// CTRL1 = address auto-increment on (so the 6-byte burst read below walks
// 0x35..0x3A), little-endian output (BE=0, matching the L-before-H register
// order), no interrupt pins wired on this board.
constexpr uint8_t kCtrl1Value = 0x40;

// CTRL2 = +/-4 g (bits[6:4] = 001) at 125 Hz (bits[3:0] = 0110).
//
// +/-4 g rather than +/-2 g: a road car tops out near 1.2 g, but suspension
// impacts and door slams spike well past that, and a clipped sample would
// poison the gravity reference g_meter.cpp captures at a standstill. The
// resolution cost is irrelevant here -- 8192 LSB/g still resolves 0.0001 g.
constexpr uint8_t kCtrl2Value = 0x16;
constexpr float kLsbPerG = 8192.0f; // 32768 / 4

// CTRL5 = accel low-pass filter on, mode 00 (2.66% of ODR) -> ~3.3 Hz at
// 125 Hz ODR. That is the point of sampling the chip at 125 Hz and reading
// it at 30: the filter runs on the chip's own rate, so road buzz is gone
// before it can alias into the UI's slower sampling. g_meter.cpp smooths
// again in software, which is what keeps the reading sane if these
// particular filter bits turn out not to do what the datasheet says.
constexpr uint8_t kCtrl5Value = 0x01;

// CTRL7 = accelerometer enabled, gyro left off (unused, and it is the
// power-hungry half).
constexpr uint8_t kCtrl7Value = 0x01;

constexpr uint32_t kReprobeIntervalMs = 2000;

bool g_present = false;
uint32_t g_lastProbeMs = 0;

bool writeReg(uint8_t reg, uint8_t value) {
    Wire1.beginTransmission(kAddr);
    Wire1.write(reg);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire1.beginTransmission(kAddr);
    Wire1.write(reg);
    // Repeated START, no STOP in between -- same single-transaction shape
    // touch_nav.cpp needs for the touch die.
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(static_cast<int>(kAddr), static_cast<int>(len)) != len) return false;
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = Wire1.available() ? static_cast<uint8_t>(Wire1.read()) : 0;
    }
    return true;
}

// Full bring-up, from reset to streaming. Returns false at the first sign
// the chip isn't there; the caller retries later rather than blocking.
bool configure() {
    uint8_t who = 0;
    if (!readRegs(kRegWhoAmI, &who, 1)) return false;
    if (who != kWhoAmIValue) {
        Serial.printf("[imu] QMI8658 not found: WHO_AM_I=0x%02X (expected 0x%02X)\n", who,
                      kWhoAmIValue);
        return false;
    }

    if (!writeReg(kRegReset, kResetCommand)) return false;
    delay(15); // datasheet: ~15ms before the chip answers again after a soft reset

    bool ok = writeReg(kRegCtrl1, kCtrl1Value);
    ok = writeReg(kRegCtrl2, kCtrl2Value) && ok;
    ok = writeReg(kRegCtrl5, kCtrl5Value) && ok;
    ok = writeReg(kRegCtrl7, kCtrl7Value) && ok;
    if (!ok) return false;

    Serial.println("[imu] QMI8658 accelerometer up: +/-4g, 125Hz, LPF on");
    return true;
}

} // namespace

void begin() {
    Wire1.begin(PIN_SENSOR_I2C_SDA, PIN_SENSOR_I2C_SCL);
    Wire1.setClock(400000);
    g_lastProbeMs = millis();
    g_present = configure();
}

bool present() { return g_present; }

Sample read() {
    if (!g_present) {
        // Re-probe occasionally rather than giving up for the session: this
        // shares a bus with the RTC and comes up alongside the panel reset,
        // so a boot-order hiccup shouldn't cost a NO SENSOR screen until the
        // next power cycle.
        uint32_t nowMs = millis();
        if (nowMs - g_lastProbeMs >= kReprobeIntervalMs) {
            g_lastProbeMs = nowMs;
            g_present = configure();
        }
        return Sample{};
    }

    uint8_t raw[6];
    if (!readRegs(kRegAccelX, raw, sizeof(raw))) {
        g_present = false; // bus dropped out; read() above will re-probe
        return Sample{};
    }

    Sample s;
    s.valid = true;
    s.ax = static_cast<int16_t>(static_cast<uint16_t>(raw[0]) | (static_cast<uint16_t>(raw[1]) << 8)) / kLsbPerG;
    s.ay = static_cast<int16_t>(static_cast<uint16_t>(raw[2]) | (static_cast<uint16_t>(raw[3]) << 8)) / kLsbPerG;
    s.az = static_cast<int16_t>(static_cast<uint16_t>(raw[4]) | (static_cast<uint16_t>(raw[5]) << 8)) / kLsbPerG;
    return s;
}

} // namespace Imu
