#include "can_decoder.h"
#include "board_pins.h"
#include "app_config.h"

#include <Arduino.h>

#ifndef DEMO_FAKE_DATA

#include "driver/twai.h"
#include "esp_timer.h"
#include "freertos/task.h"

// Per-signal byte/bit extraction below was cross-validated against
// dbc/toyota_yaris_xp130_reversed.dbc using cantools, decoding both real
// drive logs (data/logs/session_0044.log, session_0047.log) and brute-force
// searching every candidate byte offset/bit position until only the one
// matching cantools' own DBC-driven decode survived, across hundreds of
// samples per signal. This caught one real discrepancy against this
// project's own prose docs: docs/decoded_signals_summary.md describes SPEED
// as "(byte5<<8|byte4)", but the actual .dbc `SG_ SPEED : 47|16@0+` bit
// position decodes (and was confirmed against real non-zero speed samples)
// to data[5]/data[6], not data[4]/data[5] -- implemented as verified here,
// not as the prose describes.
namespace {

VehicleState g_state;
portMUX_TYPE g_stateMux = portMUX_INITIALIZER_UNLOCKED;

volatile float g_framesPerSec = 0;
volatile uint32_t g_busErrorCount = 0;
volatile uint32_t g_busOffCount = 0;

void twaiInit() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)PIN_TWAI_TX, (gpio_num_t)PIN_TWAI_RX, TWAI_MODE_LISTEN_ONLY);
    g_config.rx_queue_len = TWAI_DRIVER_RX_QUEUE_LEN;
    g_config.tx_queue_len = TWAI_DRIVER_TX_QUEUE_LEN;
    g_config.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                              TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_ERR_PASS |
                              TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    static_assert(CAN_BITRATE_BPS == 500000,
                  "t_config above is hardcoded for 500kbps -- update if CAN_BITRATE_BPS changes");
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());

    Serial.println("[can_decoder] TWAI started: 500kbps, LISTEN_ONLY, accept-all filter");
}

void pollAlerts() {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) != ESP_OK) {
        return;
    }
    if (alerts & TWAI_ALERT_BUS_ERROR) g_busErrorCount++;
    if (alerts & TWAI_ALERT_BUS_OFF) {
        g_busOffCount++;
        Serial.println("[can_decoder] WARNING: TWAI bus-off detected, restarting driver");
        twai_initiate_recovery();
    }
}

inline int16_t signExtend16(uint16_t raw) {
    return static_cast<int16_t>(raw);
}

// Decodes one recognized frame directly into g_state, under the critical
// section. Unrecognized IDs are ignored (not every ID on the bus is decoded,
// only the ~8 this cluster displays).
void decodeFrame(const twai_message_t &msg) {
    const uint8_t *d = msg.data;
    uint8_t dlc = msg.data_length_code;

    taskENTER_CRITICAL(&g_stateMux);
    switch (msg.identifier) {
        case 0x0B4: // SPEED
            if (dlc >= 7) {
                uint16_t raw = (static_cast<uint16_t>(d[5]) << 8) | d[6];
                g_state.speed_kph = raw * 0.01f;
            }
            break;

        case 0x1C4: // ENGINE_RPM
            if (dlc >= 2) {
                uint16_t raw = (static_cast<uint16_t>(d[0]) << 8) | d[1];
                float rpm = signExtend16(raw) * 0.78125f;
                g_state.rpm = static_cast<int16_t>(rpm);
            }
            break;

        case 0x127: // GEAR_PACKET
            if (dlc >= 6) {
                uint8_t raw = (d[5] >> 4) & 0x0F;
                if (raw <= static_cast<uint8_t>(Gear::B)) {
                    g_state.gear = static_cast<Gear>(raw);
                }
            }
            break;

        case 0x245: // GAS_PEDAL (also carries ICE_RUNNING)
            if (dlc >= 4) {
                g_state.ice_running = ((d[3] >> 4) & 0x01) != 0;
            }
            break;

        case 0x498: // HYBRID_STATUS
            if (dlc >= 6) {
                g_state.ev_drive = ((d[5] >> 7) & 0x01) != 0;
            }
            break;

        case 0x247: // HYBRID_SYSTEM_INDICATOR (CHG/PWR bar)
            if (dlc >= 2) {
                g_state.hsi_power = static_cast<int8_t>(d[1]);
            }
            break;

        case 0x4A7: // HV_BATTERY
            if (dlc >= 3) {
                g_state.battery_soc_pct = d[2] * 0.5f;
            }
            break;

        case 0x442: // CLIMATE_0x442 (ambient temperature)
            if (dlc >= 1) {
                g_state.ambient_temp_c = static_cast<float>(d[0]) - 40.0f;
            }
            break;

        default:
            break;
    }
    taskEXIT_CRITICAL(&g_stateMux);
}

void canRxTask(void *) {
    twaiInit();

    uint64_t windowStartUs = esp_timer_get_time();
    uint32_t framesInWindow = 0;

    for (;;) {
        twai_message_t msg;
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_OK && !msg.rtr) {
            decodeFrame(msg);
            framesInWindow++;
        }

        pollAlerts();

        uint64_t nowUs = esp_timer_get_time();
        uint64_t elapsedUs = nowUs - windowStartUs;
        if (elapsedUs >= 1000000) {
            g_framesPerSec = framesInWindow * 1000000.0f / elapsedUs;
            framesInWindow = 0;
            windowStartUs = nowUs;
        }
    }
}

} // namespace

namespace CanDecoder {

void begin() {
    xTaskCreatePinnedToCore(canRxTask, "canRx", STACK_SIZE_CAN_RX, nullptr,
                             TASK_PRIO_CAN_RX, nullptr, CORE_CAN_RX);
}

VehicleState getSnapshot() {
    VehicleState copy;
    taskENTER_CRITICAL(&g_stateMux);
    copy = g_state;
    taskEXIT_CRITICAL(&g_stateMux);
    return copy;
}

CanDecoderStats getStats() {
    CanDecoderStats s{};
    s.frames_per_sec = g_framesPerSec;
    s.bus_error_count = g_busErrorCount;
    s.bus_off_count = g_busOffCount;
    return s;
}

} // namespace CanDecoder

#else // DEMO_FAKE_DATA

// Stage-1 bring-up mode (see the plan doc's "Bring-up order"): sweeps a
// fake VehicleState through all the visually interesting states (speed
// ramp, every gear, EV/RPM swap, full CHG/PWR swing, battery sweep) purely
// as a function of millis(), with no TWAI driver installed at all -- lets
// the whole UI be verified on the bench with no vehicle/CAN transceiver
// connected. Select via `pio run -e demo -t upload`.
namespace CanDecoder {

void begin() {
    Serial.println("[can_decoder] DEMO_FAKE_DATA build -- sweeping fake values, TWAI not initialized");
}

VehicleState getSnapshot() {
    uint32_t t = millis();
    VehicleState s;

    // Speed: 12s triangle ramp 0 -> 200 -> 0.
    uint32_t speedPhase = t % 12000;
    float speedTri = speedPhase < 6000 ? (speedPhase / 6000.0f) : (2.0f - speedPhase / 6000.0f);
    s.speed_kph = speedTri * 200.0f;

    // Gear cycles through all 5 states every 4s, holding P/D longer than the
    // transient R/N/B states since that's the realistic usage pattern.
    static const Gear kGearCycle[] = {Gear::P, Gear::R, Gear::N, Gear::D, Gear::B, Gear::D};
    uint32_t gearIdx = (t / 4000) % (sizeof(kGearCycle) / sizeof(kGearCycle[0]));
    s.gear = kGearCycle[gearIdx];

    // EV drive toggles every 6s; RPM only matters when not in EV.
    s.ev_drive = ((t / 6000) % 2) == 0;
    s.ice_running = !s.ev_drive;
    s.rpm = s.ev_drive ? 0 : static_cast<int16_t>(800 + speedTri * 4500);

    // CHG/PWR bar: 8s sine-like triangle sweep across -100..100.
    uint32_t powerPhase = t % 8000;
    float powerTri = powerPhase < 4000 ? (powerPhase / 4000.0f) : (2.0f - powerPhase / 4000.0f);
    s.hsi_power = static_cast<int8_t>((powerTri * 2.0f - 1.0f) * 100.0f);

    // Battery SOC: slow 30s sweep 20%..90%.
    uint32_t battPhase = t % 30000;
    float battTri = battPhase < 15000 ? (battPhase / 15000.0f) : (2.0f - battPhase / 15000.0f);
    s.battery_soc_pct = 20.0f + battTri * 70.0f;

    s.ambient_temp_c = 21.0f;

    return s;
}

CanDecoderStats getStats() {
    return CanDecoderStats{0.0f, 0, 0};
}

} // namespace CanDecoder

#endif // DEMO_FAKE_DATA
