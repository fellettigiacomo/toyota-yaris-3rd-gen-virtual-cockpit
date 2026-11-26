#include <Arduino.h>
#include <hardware/adc.h>
#include <hardware/gpio.h>

constexpr int ADC_PINS[] = {26, 27, 28};
constexpr int TEMP_SENSOR_INPUT = 4;
constexpr int ADC_MAX = 4095;

int digitalWithPull(int pin, bool pullUp) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, pullUp, !pullUp);
    delay(2);
    return gpio_get(pin);
}

uint16_t adcRaw(int input) {
    adc_select_input(input);
    adc_read();
    return adc_read();
}

void setup() {
    Serial.begin(115200);
    adc_init();
    adc_set_temp_sensor_enabled(true);
}

void loop() {
    for (int pin : ADC_PINS) {
        int withPullDown = digitalWithPull(pin, false);
        int withPullUp = digitalWithPull(pin, true);
        adc_gpio_init(pin);
        uint16_t raw = adcRaw(pin - 26);
        Serial.printf("GP%d: raw=%4u (%4d mV)  dig pd=%d pu=%d   ", pin, raw,
                      static_cast<int>(raw * 3300 / ADC_MAX), withPullDown, withPullUp);
    }

    uint16_t tempRaw = adcRaw(TEMP_SENSOR_INPUT);
    int tempMv = tempRaw * 3300 / ADC_MAX;
    int tempDeciC = 270 - (tempMv - 706) * 10000 / 1721;
    Serial.printf("| temp raw=%u (%d mV, %d.%d C)\n", tempRaw, tempMv, tempDeciC / 10, abs(tempDeciC % 10));

    Serial.print("   GP0-7 pull-up test: ");
    for (int pin = 0; pin <= 7; pin++) Serial.print(digitalWithPull(pin, true));
    Serial.print("  pull-down test: ");
    for (int pin = 0; pin <= 7; pin++) Serial.print(digitalWithPull(pin, false));
    Serial.println();

    delay(500);
}
