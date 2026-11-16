#include "can_decoder.h"
#include "board_pins.h"
#include "app_config.h"
#include "usb_dump.h"

#include <Arduino.h>

namespace {

inline int16_t signExtend16(uint16_t raw) {
    return static_cast<int16_t>(raw);
}

// HV pack capacity (XP130 hybrid: 120-cell NiMH, 6.5 Ah nominal). Turns the
// battery current of 0x49D into a SOC rate of change, in %/s.
constexpr float kHvPackCapacityAh = 6.5f;
constexpr float kHvCurrentAmpsPerBit = 2.0f;
constexpr float kHvSocPctPerBit = 2.0f;

// 0x247 HSI_ZONE (byte[0]) values -- the sign source for HSI_VALUE, see the
// 0x247 case below.
constexpr uint8_t kHsiZoneZeroInGear = 4;  // in gear, needle at rest
constexpr uint8_t kHsiZoneInactive = 6;    // P/R, gauge inactive
constexpr uint8_t kHsiZoneDrive = 12;      // ECO/PWR side (positive)
constexpr uint8_t kHsiZoneCharge = 15;     // CHG side (negative)

// --- HSI byte[0] survey (temporary instrumentation) ---------------------
// Both capture sessions only ever showed byte[0] taking four values
// (4/6/12/15, n=34109, zero exceptions), which is the model the zone-based
// sign decode below was built on and which dbc/ and re/docs/ still state as
// fact. The car disagrees: a single hard-acceleration drive produced zones
// 8, 9 and 14 as well. So the four-value enum is not the whole encoding,
// and one sample per zone is nowhere near enough to rebuild it.
//
// This surveys the real joint distribution instead -- per byte[0] value,
// how many frames and what raw range -- and dumps the table only when it
// has learned something new, so a 42.5 Hz message costs a handful of lines
// per drive and then goes quiet. Remove this once byte[0] is understood and
// the zones are encoded explicitly.
struct HsiZoneStat {
    uint16_t count;
    uint8_t rawMin;
    uint8_t rawMax;
};
HsiZoneStat g_hsiSurvey[256] = {};
bool g_hsiSurveyDirty = false;
uint32_t g_hsiSurveyLastDumpMs = 0;
constexpr uint32_t kHsiSurveyPeriodMs = 10000;

// Recording side: runs per frame, inside the decoder's critical section, so
// it stays pure bookkeeping -- no printing, no blocking calls.
void surveyHsi(uint8_t zone, uint8_t raw) {
    HsiZoneStat &st = g_hsiSurvey[zone];
    if (st.count == 0) {
        st.rawMin = raw;
        st.rawMax = raw;
        g_hsiSurveyDirty = true;
    } else if (raw < st.rawMin) {
        st.rawMin = raw;
        g_hsiSurveyDirty = true;
    } else if (raw > st.rawMax) {
        st.rawMax = raw;
        g_hsiSurveyDirty = true;
    }
    if (st.count < 0xFFFF) st.count++;
}

// Reporting side: must be called from task context with no lock held.
// Serial is USB CDC here (ARDUINO_USB_CDC_ON_BOOT=1) and its write() blocks
// on a FreeRTOS semaphore until the host drains the buffer, which is illegal
// inside taskENTER_CRITICAL() -- interrupts are off, so nothing can drain it.
// Only the same task that calls surveyHsi() may call this, which is what
// makes the unlocked read of the table safe.
void dumpHsiSurveyIfDue() {
    uint32_t now = millis();
    if (!g_hsiSurveyDirty || now - g_hsiSurveyLastDumpMs < kHsiSurveyPeriodMs) return;
    if (UsbDump::active()) return; // kept for later: 100+ console lines would only bury the dump
    g_hsiSurveyLastDumpMs = now;
    g_hsiSurveyDirty = false;
    Serial.println("[can_decoder] HSI byte[0] survey -- zone: frames raw[min..max]");
    for (int z = 0; z < 256; z++) {
        if (g_hsiSurvey[z].count == 0) continue;
        Serial.printf("[can_decoder]   zone %3d: %5u frames, raw %3u..%3u\n", z,
                      static_cast<unsigned>(g_hsiSurvey[z].count),
                      static_cast<unsigned>(g_hsiSurvey[z].rawMin),
                      static_cast<unsigned>(g_hsiSurvey[z].rawMax));
    }
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
                state.speed_kph = raw * 0.01f * SPEED_CORRECTION;
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
                // HSI_VALUE (byte[1]) alone is ambiguous: the CHG side is
                // two's complement (hard-clamped at -100 = raw 156) while
                // the PWR side is an unsigned magnitude that sweeps well
                // past 155 at full throttle, so the two ranges overlap in
                // raw space. HSI_ZONE (byte[0]) is what disambiguates them,
                // and it does so perfectly: across both capture sessions
                // (n=32779) zone 12 carried raw 0-94 without exception and
                // zone 15 carried raw 156-255 without exception, with zones
                // 4 and 6 always raw 0. Deriving the sign from raw alone
                // (the old raw>=156 => negative rule) is what flipped a
                // full-PWR reading into full CHG on the car.
                uint8_t zone = d[0];
                uint8_t raw = d[1];
                surveyHsi(zone, raw);
                switch (zone) {
                    case kHsiZoneCharge: // CHG, the one confirmed-negative zone
                        // Two's complement, hard-clamped at -100 = raw 156.
                        // raw<128 here has never been observed; keep the sign
                        // the zone asserts rather than reporting a positive
                        // value from the charge side.
                        state.hsi_power = raw >= 128
                            ? static_cast<int16_t>(static_cast<int>(raw) - 256)
                            : static_cast<int16_t>(-static_cast<int>(raw));
                        break;
                    case kHsiZoneZeroInGear:
                    case kHsiZoneInactive:
                        state.hsi_power = 0; // needle parked; raw is always 0 here anyway
                        break;
                    case kHsiZoneDrive: // ECO/PWR: unsigned magnitude, 0..255
                    default:
                        // Every zone but the charge one reads as an unsigned
                        // magnitude, including zones never captured. The old
                        // signed fallback here is what still flipped a hard
                        // PWR reading to CHG: it turned any raw>=128 into a
                        // large negative, so the gauge jumped to full CHG the
                        // moment the bar passed 64% (raw 128 * the display's
                        // 0.5 scale). Only zone 15 is ever negative on this
                        // bus, so treating an unrecognized zone as positive is
                        // both the safer default and the one consistent with
                        // every frame in the captures.
                        state.hsi_power = static_cast<int16_t>(raw);
                        break;
                }
            }
            break;

        case 0x49D: // HV_BATTERY (SOC + pack current)
            if (dlc >= 7) {
                // SOC is the 6-bit field straddling byte[5] bits 1..0 and
                // byte[6] bits 7..4. It is a coulomb counter: across both
                // drive logs every step of it lines up with a fixed amount
                // of integrated pack current (byte[2], signed, positive =
                // discharge), it rises under regen and falls in EV drive,
                // and it differs between cold starts (60% / 60% / 68%).
                //
                // This replaces 0x4A7 byte[2], which the first decode took
                // for the SOC: that value climbs whenever the engine runs
                // and falls whenever it is stopped -- including through
                // hard regenerative braking, where a SOC has to rise -- and
                // reads raw 108 ("54%") at every single start. It is an
                // engine-side quantity, not the battery.
                //
                // Scales: 2 A/bit comes from the energy balance against the
                // car's kinetic energy (1 A/bit would have the pack deliver
                // less energy than the car gains in EV acceleration), and
                // with it one SOC step is 0.13 Ah = 2% of the 6.5 Ah pack.
                // Not yet checked against a diagnostic-tool reading.
                uint8_t socRaw = static_cast<uint8_t>(((d[5] & 0x03) << 4) | (d[6] >> 4));
                state.battery_soc_pct = socRaw * kHvSocPctPerBit;
                state.battery_current_a = static_cast<int8_t>(d[2]) * kHvCurrentAmpsPerBit;
                // SOC only ticks every 20 s or so, far too slowly to tell
                // charging from discharging; the current says it directly.
                state.soc_trend_pct_per_s =
                    -state.battery_current_a * 100.0f / (kHvPackCapacityAh * 3600.0f);
            }
            break;

        case 0x3B6: // ENERGY_MONITOR (the cluster's energy-flow flags)
            if (dlc >= 7) {
                // byte[6] takes one fixed pattern per driving state on both
                // drive logs (0x75 EV, 0xAD regen, 0x6D/0x6C engine driving
                // and charging, 0x0C engine only, 0x01 standstill); the bit
                // meanings are read off those patterns -- see
                // re/docs/capture_survey.md. Not yet compared with the car's
                // own display.
                state.ef_engine_running = (d[5] & 0x04) != 0;
                state.ef_engine_to_wheels = (d[5] & 0x20) != 0;
                state.ef_batt_charging = (d[6] & 0x08) != 0;
                state.ef_batt_discharging = (d[6] & 0x10) != 0;
                state.ef_motor_to_wheels = (d[6] & 0x40) != 0;
                state.ef_wheels_to_motor = (d[6] & 0x80) != 0;
                state.energy_flags_seen = true;
                state.energy_flags_ms = millis();
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
        if (err == ESP_OK) {
            // Every frame, RTR included: the dump is the raw bus, not just
            // what this decoder understands.
            UsbDump::push(msg.identifier, msg.extd, msg.rtr, msg.data_length_code, msg.data);
        }
        if (err == ESP_OK && !msg.rtr) {
            taskENTER_CRITICAL(&g_stateMux);
            decodeIntoState(g_state, msg.identifier, msg.data, msg.data_length_code);
            taskEXIT_CRITICAL(&g_stateMux);
            framesInWindow++;
        }

        dumpHsiSurveyIfDue(); // outside the critical section: it prints
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
    copy.usb_dump = UsbDump::active();
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
        elapsedMs = 0;
    }

    while (g_nextIdx < kDemoLogCount && kDemoLog[g_nextIdx].t_ms <= elapsedMs) {
        const DemoLogFrame &f = kDemoLog[g_nextIdx];
        decodeIntoState(g_demoState, f.id, f.data, f.dlc);
        // The replayed frames go out over the USB dump too, so the whole
        // capture path can be tried on the bench (timestamps are replay
        // time, bunched at the UI sync rate).
        UsbDump::push(f.id, f.id > 0x7FF, false, f.dlc, f.data);
        g_nextIdx++;
    }
    dumpHsiSurveyIfDue();

    g_demoState.usb_dump = UsbDump::active();
    return g_demoState;
}

CanDecoderStats getStats() {
    return CanDecoderStats{0.0f, 0, 0, 0};
}

} // namespace CanDecoder

#endif // DEMO_FAKE_DATA
