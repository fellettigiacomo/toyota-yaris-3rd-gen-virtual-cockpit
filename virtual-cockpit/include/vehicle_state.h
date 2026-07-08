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
    int8_t hsi_power = 0;             // 0x247 HSI_VALUE, -100..100 (neg=CHG, pos=PWR)
    float battery_soc_pct = 0.0f;     // 0x4A7 BATTERY_SOC, already *0.5, 0-100
    float ambient_temp_c = 0.0f;      // 0x442 AMBIENT_TEMP, already -40 offset applied
};
