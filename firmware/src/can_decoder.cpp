#include "can_decoder.h"
#include "board_pins.h"
#include "app_config.h"

#include <Arduino.h>
#include <algorithm>

namespace {

inline int16_t signExtend16(uint16_t raw) {
    return static_cast<int16_t>(raw);
}

// battery_soc_pct arrives in coarse 0.5% steps every few seconds -- too
// noisy to read instantaneously to tell "charging" from "discharging" near
// zero. This EMA only samples a new rate when the raw value actually ticks
// (an SOC that hasn't moved yet doesn't mean the rate is zero, it means we
// don't have a fresh sample), and decays the estimate toward zero if it
// stays quiet for a while so a stale trend doesn't linger after the vehicle
// settles into a steady state.
float g_socTrendEma = 0.0f;
float g_lastRawSoc = -1.0f; // sentinel: no sample observed yet
uint32_t g_lastSocChangeMs = 0;
uint32_t g_lastSocSampleMs = 0;
constexpr float kSocEmaAlpha = 0.3f;
constexpr uint32_t kSocDecayStartMs = 4000;
constexpr float kSocDecayPerSec = 0.5f;

constexpr uint8_t kHsiChgFloorRaw = 156;

void resetSocTrendState() {
    g_socTrendEma = 0.0f;
    g_lastRawSoc = -1.0f;
    g_lastSocChangeMs = 0;
    g_lastSocSampleMs = 0;
}

void updateSocTrend(VehicleState &state, float rawSocPct) {
    uint32_t nowMs = millis();
    if (g_lastRawSoc < 0.0f) {
        g_lastRawSoc = rawSocPct;
        g_lastSocChangeMs = nowMs;
        g_lastSocSampleMs = nowMs;
        state.soc_trend_pct_per_s = 0.0f;
        return;
    }

    if (rawSocPct != g_lastRawSoc) {
        float dtS = (nowMs - g_lastSocChangeMs) / 1000.0f;
        if (dtS > 0.05f) {
            float instRate = (rawSocPct - g_lastRawSoc) / dtS;
            g_socTrendEma = kSocEmaAlpha * instRate + (1.0f - kSocEmaAlpha) * g_socTrendEma;
        }
        g_lastRawSoc = rawSocPct;
        g_lastSocChangeMs = nowMs;
    } else if (nowMs - g_lastSocChangeMs > kSocDecayStartMs) {
        float dtS = (nowMs - g_lastSocSampleMs) / 1000.0f;
        float decay = kSocDecayPerSec * dtS;
        if (g_socTrendEma > 0.0f) {
            g_socTrendEma = std::max(0.0f, g_socTrendEma - decay);
        } else if (g_socTrendEma < 0.0f) {
            g_socTrendEma = std::min(0.0f, g_socTrendEma + decay);
        }
    }

    g_lastSocSampleMs = nowMs;
    state.soc_trend_pct_per_s = g_socTrendEma;
}

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
//
// Shared between the real TWAI decoder and the demo log-replay decoder
// below (both need identical bit-math against a {id, dlc, data} frame,
// whether it came from the live bus or the embedded demo log) -- unlike
// them, this function has no locking of its own, the caller decides that.
void decodeIntoState(VehicleState &state, uint32_t id, const uint8_t *d, uint8_t dlc) {
    switch (id) {
        case 0x0B4: // SPEED
            if (dlc >= 7) {
                uint16_t raw = (static_cast<uint16_t>(d[5]) << 8) | d[6];
                state.speed_kph = raw * 0.01f;
            }
            break;

        case 0x1C4: // ENGINE_RPM
            if (dlc >= 2) {
                uint16_t raw = (static_cast<uint16_t>(d[0]) << 8) | d[1];
                float rpm = signExtend16(raw) * 0.78125f;
                state.rpm = static_cast<int16_t>(rpm);
            }
            break;

        case 0x127: // GEAR_PACKET
            if (dlc >= 6) {
                uint8_t raw = (d[5] >> 4) & 0x0F;
                if (raw <= static_cast<uint8_t>(Gear::B)) {
                    state.gear = static_cast<Gear>(raw);
                }
            }
            break;

        case 0x245: // GAS_PEDAL (also carries ICE_RUNNING)
            if (dlc >= 4) {
                state.ice_running = ((d[3] >> 4) & 0x01) != 0;
            }
            break;

        case 0x498: // HYBRID_STATUS
            if (dlc >= 6) {
                state.ev_drive = ((d[5] >> 7) & 0x01) != 0;
            }
            break;

        case 0x247: // HYBRID_SYSTEM_INDICATOR (CHG/PWR bar)
            if (dlc >= 2) {
                // Byte is DBC-signed (@0-, [-100|100]) but the confirmed CHG
                // floor is exactly -100 = raw unsigned >=156; the naive
                // int8_t sign flip at 128 would wrap a hard-PWR raw of
                // 128-155 into deeply-negative false-CHG. Raw 101-155
                // (0x65-0x9b) has never appeared in the two real logs
                // (neither includes hard accelerator-into-PWR driving), so
                // anchor the negative branch at the confirmed floor instead.
                uint8_t raw = d[1];
                state.hsi_power = (raw >= kHsiChgFloorRaw)
                    ? static_cast<int16_t>(static_cast<int>(raw) - 256) // CHG: -100..-1
                    : static_cast<int16_t>(raw);                        // PWR/ECO: 0..155
            }
            break;

        case 0x4A7: // HV_BATTERY
            if (dlc >= 3) {
                float soc = d[2] * 0.5f;
                updateSocTrend(state, soc);
                state.battery_soc_pct = soc;
            }
            break;

        case 0x320: // PWR_DEMAND (ACCEL_DEMAND)
            if (dlc >= 5) {
                state.accel_demand = static_cast<int8_t>(d[4]);
            }
            break;

        case 0x230: // BRAKE_MODULE2 (BRAKE_PRESSED)
            if (dlc >= 4) {
                // DBC: SG_ BRAKE_PRESSED : 26|1@0+ -- byte = 26/8 = 3, shift = 26 mod 8 = 2.
                state.brake_pressed = ((d[3] >> 2) & 0x01) != 0;
            }
            break;

        default:
            break;
    }
}

} // namespace

#ifndef DEMO_FAKE_DATA

#include "driver/twai.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {

VehicleState g_state;
portMUX_TYPE g_stateMux = portMUX_INITIALIZER_UNLOCKED;

volatile float g_framesPerSec = 0;
volatile uint32_t g_busErrorCount = 0;
volatile uint32_t g_busOffCount = 0;
volatile uint32_t g_rxOverflowCount = 0;

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
    if (alerts & (TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN)) {
        // Frames were silently dropped by the driver/controller. These alerts
        // were always enabled in twaiInit() but never counted, which made
        // frame loss under real driving traffic an unanswerable question --
        // now it shows up here and in getStats().
        g_rxOverflowCount++;
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastWarnMs >= 1000) {
            lastWarnMs = nowMs;
            Serial.printf("[can_decoder] WARNING: RX overflow, frames dropped (count=%lu)\n",
                          static_cast<unsigned long>(g_rxOverflowCount));
        }
    }
    if (alerts & TWAI_ALERT_BUS_OFF) {
        g_busOffCount++;
        Serial.println("[can_decoder] WARNING: TWAI bus-off detected, restarting driver");
        twai_initiate_recovery();
    }
}

void canRxTask(void *) {
    twaiInit();

    uint64_t windowStartUs = esp_timer_get_time();
    uint32_t framesInWindow = 0;

    for (;;) {
        twai_message_t msg;
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_OK && !msg.rtr) {
            taskENTER_CRITICAL(&g_stateMux);
            decodeIntoState(g_state, msg.identifier, msg.data, msg.data_length_code);
            taskEXIT_CRITICAL(&g_stateMux);
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
    s.rx_overflow_count = g_rxOverflowCount;
    return s;
}

} // namespace CanDecoder

#else // DEMO_FAKE_DATA

#include "demo_log_data.h"

// Replays a real drive (see scripts/gen_demo_log.py for which log and why)
// through the exact same decodeIntoState() bit-math the real build uses,
// instead of a synthetic sweep -- so the demo build shows genuine driving
// behavior (real speed/RPM/gear correlation, real EV transitions, etc.)
// with no vehicle/CAN transceiver connected. Loops back to the start when
// the log ends. Select via `pio run -e demo -t upload`.
namespace {

VehicleState g_demoState;
size_t g_nextIdx = 0;
uint32_t g_playbackStartMs = 0;

} // namespace

namespace CanDecoder {

void begin() {
    Serial.printf("[can_decoder] DEMO_FAKE_DATA build -- replaying %u frames (%lums) from an "
                  "embedded real drive log, TWAI not initialized\n",
                  static_cast<unsigned>(kDemoLogCount), static_cast<unsigned long>(kDemoLogDurationMs));
}

VehicleState getSnapshot() {
    if (g_playbackStartMs == 0) {
        g_playbackStartMs = millis(); // first call sets the replay's t=0
    }
    uint32_t elapsedMs = millis() - g_playbackStartMs;

    if (elapsedMs >= kDemoLogDurationMs) {
        // Loop: rebase playback start so elapsed wraps back to 0, and reset
        // to the log's own starting state so the loop point doesn't carry
        // over stale values from the end of the previous lap.
        g_playbackStartMs = millis();
        g_nextIdx = 0;
        g_demoState = VehicleState{};
        resetSocTrendState();
        elapsedMs = 0;
    }

    while (g_nextIdx < kDemoLogCount && kDemoLog[g_nextIdx].t_ms <= elapsedMs) {
        const DemoLogFrame &f = kDemoLog[g_nextIdx];
        decodeIntoState(g_demoState, f.id, f.data, f.dlc);
        g_nextIdx++;
    }

    return g_demoState;
}

CanDecoderStats getStats() {
    return CanDecoderStats{0.0f, 0, 0, 0};
}

} // namespace CanDecoder

#endif // DEMO_FAKE_DATA
