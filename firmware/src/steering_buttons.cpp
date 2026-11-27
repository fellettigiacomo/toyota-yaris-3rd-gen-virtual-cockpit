#include "steering_buttons.h"
#include "app_config.h"
#include "board_pins.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace SteeringButtons {

namespace {

constexpr int kAdcSamples = 8;
constexpr uint32_t kModeMaxMv = 300;
// TODO: split Other into On Hook / Off Hook once their levels are logged.
constexpr uint32_t kReleasedMinMv = 1500;
constexpr int kStableSamples = 3; // x STEERING_SAMPLE_PERIOD_MS of debounce
constexpr int kPressQueueLen = 4;

QueueHandle_t g_presses = nullptr;

uint32_t readMv() {
    uint32_t sum = 0;
    for (int i = 0; i < kAdcSamples; i++) sum += analogReadMilliVolts(PIN_STEERING_SW2);
    return sum / kAdcSamples;
}

Button classify(uint32_t mv) {
    if (mv < kModeMaxMv) return Button::Mode;
    if (mv < kReleasedMinMv) return Button::Other;
    return Button::None;
}

const char *name(Button button) {
    return button == Button::Mode ? "MODE" : "other";
}

void samplingTask(void *) {
    Button stable = Button::None;
    Button candidate = Button::None;
    int candidateCount = 0;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        uint32_t mv = readMv();
        Button raw = classify(mv);

        if (raw == candidate) {
            if (candidateCount < kStableSamples) candidateCount++;
        } else {
            candidate = raw;
            candidateCount = 1;
        }

        if (candidateCount >= kStableSamples && candidate != stable) {
            stable = candidate;
            if (stable != Button::None) {
                Serial.printf("steering: %s (%u mV)\n", name(stable), static_cast<unsigned>(mv));
                xQueueSend(g_presses, &stable, 0);
            }
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(STEERING_SAMPLE_PERIOD_MS));
    }
}

} // namespace

void begin() {
    analogReadMilliVolts(PIN_STEERING_SW2);
    gpio_pullup_en(static_cast<gpio_num_t>(PIN_STEERING_SW2));

    g_presses = xQueueCreate(kPressQueueLen, sizeof(Button));
    xTaskCreatePinnedToCore(samplingTask, "steering", STACK_SIZE_STEERING, nullptr,
                            TASK_PRIO_STEERING, nullptr, CORE_STEERING);
}

Button pressed() {
    Button button = Button::None;
    if (g_presses) xQueueReceive(g_presses, &button, 0);
    return button;
}

} // namespace SteeringButtons
