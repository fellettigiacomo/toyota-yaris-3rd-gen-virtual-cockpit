#include "usb_dump.h"
#include "app_config.h"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

struct Frame {
    uint64_t t_us; // esp_timer_get_time() at receipt
    uint32_t id;
    uint8_t dlc;
    bool extended;
    bool rtr;
    uint8_t data[8];
};

QueueHandle_t g_queue = nullptr;
volatile bool g_active = false;
volatile uint32_t g_dropped = 0;

// Time base: candump timestamps are host epoch + time since SETTIME when the
// host sent one, otherwise seconds since STREAM ON.
bool g_haveEpoch = false;
uint64_t g_epochUs = 0;      // host epoch, in us, at g_epochBaseUs
uint64_t g_epochBaseUs = 0;  // esp_timer_get_time() when SETTIME arrived
uint64_t g_streamBaseUs = 0; // esp_timer_get_time() at STREAM ON

char g_cmd[48];
size_t g_cmdLen = 0;

uint32_t g_writeStallSinceMs = 0; // 0 = writes are going through

// A USB host is on the other end of the cable (it is sending start-of-frame
// packets). With the default ARDUINO_USB_MODE=1, Serial is the S3's built-in
// USB-Serial/JTAG (HWCDC), which tracks exactly that; TinyUSB's CDC has no
// equivalent, so there fall back to its "port opened" state.
bool hostPresent() {
#if ARDUINO_USB_MODE
    return HWCDC::isPlugged();
#else
    return static_cast<bool>(Serial);
#endif
}

void start() {
    xQueueReset(g_queue);
    g_dropped = 0;
    g_streamBaseUs = esp_timer_get_time();
    g_writeStallSinceMs = 0;
    Serial.println("[usb_dump] STREAM ON -- streaming candump lines");
    Serial.flush();
    g_active = true;
}

void stop(const char *why) {
    if (!g_active) return;
    g_active = false;
    Serial.printf("\n[usb_dump] STREAM OFF (%s), %lu frames dropped\n", why,
                  static_cast<unsigned long>(g_dropped));
}

void handleCommand(const char *cmd) {
    if (strcmp(cmd, "STREAM ON") == 0) {
        start();
    } else if (strcmp(cmd, "STREAM OFF") == 0) {
        stop("host request");
    } else if (strncmp(cmd, "SETTIME ", 8) == 0) {
        long long epoch = atoll(cmd + 8);
        if (epoch > 0) {
            g_epochUs = static_cast<uint64_t>(epoch) * 1000000ULL;
            g_epochBaseUs = esp_timer_get_time();
            g_haveEpoch = true;
            Serial.printf("[usb_dump] SETTIME %lld\n", epoch);
        }
    }
}

void pollCommands() {
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            g_cmd[g_cmdLen] = '\0';
            if (g_cmdLen > 0) handleCommand(g_cmd);
            g_cmdLen = 0;
        } else if (g_cmdLen < sizeof(g_cmd) - 1) {
            g_cmd[g_cmdLen++] = static_cast<char>(c);
        }
    }
}

// Worst case is an extended ID with 8 data bytes: 51 chars, hence the 64-byte
// headroom streamBatch() keeps.
size_t format(char *out, size_t outLen, const Frame &f) {
    uint64_t absUs = g_haveEpoch ? g_epochUs + (f.t_us - g_epochBaseUs) : f.t_us - g_streamBaseUs;
    int n = snprintf(out, outLen, "(%lu.%06lu) can0 %0*lx#",
                     static_cast<unsigned long>(absUs / 1000000ULL),
                     static_cast<unsigned long>(absUs % 1000000ULL), f.extended ? 8 : 3,
                     static_cast<unsigned long>(f.id));
    if (n < 0 || static_cast<size_t>(n) >= outLen) return 0;
    size_t pos = static_cast<size_t>(n);
    static const char kHex[] = "0123456789abcdef";
    if (f.rtr) {
        out[pos++] = 'R';
    } else {
        for (int i = 0; i < f.dlc && i < 8; i++) {
            out[pos++] = kHex[f.data[i] >> 4];
            out[pos++] = kHex[f.data[i] & 0x0F];
        }
    }
    out[pos++] = '\n';
    return pos;
}

// Lines are batched into one write, so a busy bus costs a USB transfer per
// batch rather than per frame.
void streamBatch() {
    static char buf[1024];
    size_t used = 0;
    Frame f;
    if (xQueueReceive(g_queue, &f, pdMS_TO_TICKS(20)) != pdTRUE) return;
    do {
        used += format(buf + used, sizeof(buf) - used, f);
    } while (used + 64 <= sizeof(buf) && xQueueReceive(g_queue, &f, 0) == pdTRUE);

    size_t written = Serial.write(reinterpret_cast<const uint8_t *>(buf), used);
    uint32_t now = millis();
    if (written == used) {
        g_writeStallSinceMs = 0;
    } else if (g_writeStallSinceMs == 0) {
        g_writeStallSinceMs = now;
    } else if (now - g_writeStallSinceMs > USB_DUMP_STALL_TIMEOUT_MS) {
        // Cable still in, but nobody is reading (logger killed without
        // STREAM OFF, laptop asleep): give up rather than show
        // "CAN DUMP USB" for a dump that is going nowhere.
        stop("host stopped reading");
    }
}

void usbDumpTask(void *) {
    for (;;) {
        pollCommands();
        if (g_active && !hostPresent()) {
            stop("USB unplugged"); // printed into the void, but resets the state
        }
        if (g_active) {
            streamBatch();
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

} // namespace

namespace UsbDump {

void begin() {
    g_queue = xQueueCreate(USB_DUMP_QUEUE_LEN, sizeof(Frame));
    xTaskCreatePinnedToCore(usbDumpTask, "usbDump", STACK_SIZE_USB_DUMP, nullptr,
                             TASK_PRIO_USB_DUMP, nullptr, CORE_USB_DUMP);
}

bool active() { return g_active; }

void push(uint32_t id, bool extended, bool rtr, uint8_t dlc, const uint8_t *data) {
    if (!g_active || g_queue == nullptr) return;
    Frame f;
    f.t_us = esp_timer_get_time();
    f.id = id;
    f.extended = extended;
    f.rtr = rtr;
    f.dlc = dlc > 8 ? 8 : dlc;
    memcpy(f.data, data, f.dlc);
    if (xQueueSend(g_queue, &f, 0) != pdTRUE) g_dropped++;
}

} // namespace UsbDump
