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
    int16_t hsi_power = 0;             // 0x247 HSI_VALUE, -100..+255 (neg=CHG, pos=PWR);
                                        // sign comes from HSI_ZONE, not from the raw byte, because
                                        // the two sides' raw ranges overlap -- see can_decoder.cpp
    float battery_soc_pct = 0.0f;     // 0x49D BATTERY_SOC, already *2, 0-100 (2% steps)
    float battery_current_a = 0.0f;   // 0x49D BATTERY_CURRENT, amps, positive = discharge
    int8_t accel_demand = 0;          // 0x320 ACCEL_DEMAND, signed, sign = traction(+)/regen-brake(-)
    bool brake_pressed = false;       // 0x230 BRAKE_PRESSED
    float soc_trend_pct_per_s = 0.0f; // derived from battery_current_a and the pack capacity (+ = charging)
    // Powertrain presence: 0x0B4 SPEED only exists on the bus while the car
    // is switched on (before READY the bus carries body messages only), so
    // its last arrival time tells "car on" from "car off".
    bool powertrain_seen = false;
    uint32_t powertrain_ms = 0;       // millis() of the last 0x0B4 frame

    // Fuel. All four are negative until their first frame has arrived.
    float fuel_flow_mm3s = 0.0f;      // 0x3D3 FUEL_FLOW, mm3/s, 0 with the engine off
    float inst_consumption_l100 = -1.0f; // 0x610 INSTANT_CONSUMPTION; negative = none (standstill)
    float range_km = -1.0f;           // 0x619 RANGE_TO_EMPTY
    float fuel_level_pct = -1.0f;     // 0x4A6 FUEL_LEVEL as a share of the 36 l tank
    // 0x3B6 ENERGY_MONITOR: the car's own energy-flow flags (1 Hz). Only
    // meaningful while energy_flags_seen is set and energy_flags_ms is recent;
    // energy_flow_ui.cpp falls back to inferring the flows otherwise.
    bool energy_flags_seen = false;
    uint32_t energy_flags_ms = 0;     // millis() of the last 0x3B6 frame
    bool ef_engine_running = false;   // byte5 bit2
    bool ef_engine_to_wheels = false; // byte5 bit5
    bool ef_batt_charging = false;    // byte6 bit3
    bool ef_batt_discharging = false; // byte6 bit4
    bool ef_motor_to_wheels = false;  // byte6 bit6
    bool ef_wheels_to_motor = false;  // byte6 bit7 (regen)
    bool usb_dump = false;            // not CAN: a USB CAN dump is streaming (usb_dump.h), shown on the cockpit
};
