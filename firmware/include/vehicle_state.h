#pragma once

#include <cstdint>

// Gear values match dbc/toyota_yaris_xp130_reversed.dbc's VAL_ 295 GEAR table.
enum class Gear : uint8_t {
    P = 0,
    R = 1,
    N = 2,
    D = 3,
    B = 4, // "Engine Brake" -- only ever observed with the shifter held in B
};

// Flat, already-decoded, UI-ready snapshot of the vehicle signals this
// cluster displays. can_decoder.cpp does 100% of the DBC bit/scale math;
// cockpit_ui.cpp does 0% of it -- it only ever reads these fields.
struct VehicleState {
    float speed_kph = 0.0f;           // 0x0B4 SPEED, 0-250
    int16_t rpm = 0;                  // 0x1C4 RPM, already *0.78125
    Gear gear = Gear::P;              // 0x127 GEAR
    bool ice_running = false;         // 0x245 ICE_RUNNING
    bool ev_drive = false;            // 0x498 EV_DRIVE
    int16_t hsi_power = 0;             // 0x247 HSI_VALUE, -100..+155 (neg=CHG, pos=PWR);
                                        // upper ceiling >100 unverified pending a hard-PWR capture
    float battery_soc_pct = 0.0f;     // 0x4A7 BATTERY_SOC, already *0.5, 0-100
    int8_t accel_demand = 0;          // 0x320 ACCEL_DEMAND, signed, sign = traction(+)/regen-brake(-)
    bool brake_pressed = false;       // 0x230 BRAKE_PRESSED
    // NOTE: there is deliberately no steering-wheel MODE button field here.
    // 0x4AC byte[6] (mask 0x90) was decoded as the MODE button for a while
    // but is actually a free-running 4.5s-on/4.5s-off oscillator, present
    // identically in every log with the button untouched -- it cycled the
    // screens by itself every 9s on the road. The button does not appear
    // anywhere on this CAN bus (exhaustive cross-validated search, see
    // docs/signal_findings.md Addendum 5); it is almost certainly on the
    // analog steering-switch ladder wires of the radio connector instead.
    float soc_trend_pct_per_s = 0.0f; // derived: EMA'd rate of change of battery_soc_pct, not a raw signal
};
