#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

constexpr int PIN_SW2 = 2;
constexpr int PIN_RGB_LED = 29;

constexpr int STABLE_READS = 3;
constexpr uint32_t LOOP_INTERVAL_MS = 10;
constexpr uint32_t PRINT_INTERVAL_MS = 500;

Adafruit_NeoPixel led(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

void showPressed(bool pressed) {
    led.setPixelColor(0, pressed ? led.Color(0, 0, 255) : 0);
    led.show();
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_SW2, INPUT_PULLUP);
    led.begin();
    led.setBrightness(60);

    const uint32_t selfTestColors[] = {led.Color(255, 0, 0), led.Color(0, 255, 0), led.Color(0, 0, 255)};
    for (uint32_t color : selfTestColors) {
        led.setPixelColor(0, color);
        led.show();
        delay(300);
    }
    showPressed(false);
}

void loop() {
    static bool stable = false;
    static bool candidate = false;
    static int candidateCount = 0;
    static uint32_t lastPrint = 0;

    bool pressed = digitalRead(PIN_SW2) == LOW;

    if (pressed == candidate) {
        if (candidateCount < STABLE_READS) candidateCount++;
    } else {
        candidate = pressed;
        candidateCount = 1;
    }

    if (candidateCount >= STABLE_READS && candidate != stable) {
        stable = candidate;
        showPressed(stable);
        Serial.println(stable ? ">> MODE pressed" : ">> released");
    }

    if (millis() - lastPrint >= PRINT_INTERVAL_MS) {
        lastPrint = millis();
        Serial.printf("GP%d = %s  [%s]\n", PIN_SW2, digitalRead(PIN_SW2) ? "HIGH" : "LOW ",
                      stable ? "MODE" : "none");
    }

    delay(LOOP_INTERVAL_MS);
}
