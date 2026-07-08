#include "can_id_table.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "app_config.h"

namespace {

// Open-addressing hash table, sized well beyond CAN_ID_TABLE_MAX_ENTRIES to
// keep collision chains short. Slots are cleared (id_used=false) at init.
constexpr size_t TABLE_CAPACITY = 512;

struct Slot {
    bool used = false;
    CanIdEntry entry{};
};

Slot g_table[TABLE_CAPACITY];
size_t g_count = 0;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

// Smoothing time constant for the Hz exponential moving average.
constexpr float HZ_EMA_TAU_US = 500000.0f; // 0.5s

size_t hashId(uint32_t id) {
    return (id * 2654435761u) % TABLE_CAPACITY;
}

} // namespace

namespace CanIdTable {

void init() {
    portENTER_CRITICAL(&g_lock);
    for (auto &slot : g_table) {
        slot.used = false;
    }
    g_count = 0;
    portEXIT_CRITICAL(&g_lock);
}

void update(const CanFrameRecord &rec) {
    portENTER_CRITICAL(&g_lock);
    size_t idx = hashId(rec.id);
    for (size_t probe = 0; probe < TABLE_CAPACITY; ++probe) {
        Slot &slot = g_table[(idx + probe) % TABLE_CAPACITY];
        if (slot.used && slot.entry.id == rec.id) {
            CanIdEntry &e = slot.entry;
            uint64_t deltaUs = (e.count > 0) ? (rec.timestamp_us - e.last_seen_us) : 0;
            e.dlc = rec.dlc;
            memcpy(e.last_data, rec.data, sizeof(e.last_data));
            e.extended = rec.extended;
            e.rtr = rec.rtr;
            e.count++;
            if (deltaUs > 0) {
                float instHz = 1000000.0f / static_cast<float>(deltaUs);
                float alpha = 1.0f - expf(-static_cast<float>(deltaUs) / HZ_EMA_TAU_US);
                e.hz = e.hz + alpha * (instHz - e.hz);
            }
            e.last_seen_us = rec.timestamp_us;
            portEXIT_CRITICAL(&g_lock);
            return;
        }
        if (!slot.used) {
            // New ID. If the table is full, drop it (defensive -- should not
            // happen on a real vehicle bus, which typically has well under
            // CAN_ID_TABLE_MAX_ENTRIES unique IDs).
            if (g_count >= CAN_ID_TABLE_MAX_ENTRIES) {
                portEXIT_CRITICAL(&g_lock);
                return;
            }
            slot.used = true;
            slot.entry.id = rec.id;
            slot.entry.dlc = rec.dlc;
            memcpy(slot.entry.last_data, rec.data, sizeof(slot.entry.last_data));
            slot.entry.extended = rec.extended;
            slot.entry.rtr = rec.rtr;
            slot.entry.count = 1;
            slot.entry.last_seen_us = rec.timestamp_us;
            slot.entry.hz = 0.0f;
            g_count++;
            portEXIT_CRITICAL(&g_lock);
            return;
        }
    }
    // Table completely full of collisions (should not happen at this capacity).
    portEXIT_CRITICAL(&g_lock);
}

size_t snapshot(CanIdEntry *out, size_t maxEntries) {
    size_t n = 0;
    portENTER_CRITICAL(&g_lock);
    for (const auto &slot : g_table) {
        if (slot.used && n < maxEntries) {
            out[n++] = slot.entry;
        }
    }
    portEXIT_CRITICAL(&g_lock);
    std::sort(out, out + n, [](const CanIdEntry &a, const CanIdEntry &b) {
        return a.id < b.id;
    });
    return n;
}

size_t size() {
    portENTER_CRITICAL(&g_lock);
    size_t n = g_count;
    portEXIT_CRITICAL(&g_lock);
    return n;
}

} // namespace CanIdTable
