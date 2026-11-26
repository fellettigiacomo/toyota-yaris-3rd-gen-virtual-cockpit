# mode-button-test

Bench/in-car test for reading the steering-wheel **MODE** button without the
CAN bus, on a **Tiny RP2040** (silkscreen "Tiny RP2040 RGB (29)", WisdPi-style). The onboard RGB LED turns **blue**
while MODE is held.

> **Digital input, not ADC.** This board's ADC is dead: with nothing
> connected, GP26–28 and even the internal temperature sensor all read 4095,
> and the GP26–28 pads ignore their own pull-ups while GP0–7 respond normally
> (`pio run -e diag -t upload` reproduces it). That points at the ADC_AVDD
> supply, which also powers the GP26–29 pads. MODE is a 0 Ω short, so a
> digital pin is enough to detect it; measuring the On/Off Hook levels needs
> an ADC and is left for the ESP32-S3.

## How the button reaches the radio

From the factory wiring diagram (*Audio System, Navigation System and Parking
Assist*, YARIS HV EM20CAE) and the radio pinout:

| Signal | Radio 28-pin connector | Wire colour (radio side) | Spiral cable F4 | Steering pad z2 |
|---|---|---|---|---|
| SW1 (Seek±, Volume±) | 21 | L (blue) | 6 | 10, BR |
| **SW2 (MODE, On/Off Hook)** | **22** | **P (pink)** | 5 | 9, GR |
| SWG (switch ground) | 23 | R (red) | 4 | 8, W |

MODE shorts SW2 to SWG directly (0 Ω). On Hook / Off Hook go through
resistors, and a fixed resistor is always across SW2–SWG so the radio can
tell the pad is connected.

## Wiring

For this test the **pink wire is removed from the radio connector** (de-pin
the terminal, don't cut it), so the RP2040 supplies the pull-up the radio
used to.

```
  Tiny RP2040 3V3 ──[10k]──┐
                           │
  SW2 pink wire ───────────┼──[1k]──┬── Tiny RP2040 GP2
  (steering-wheel side,             │
   out of radio pin 22)          [1µF] 
                                    │
  SWG red wire ─────────────────────┴── Tiny RP2040 GND
  (radio pin 23, stays on the radio — tap into it)
```

- 10 kΩ from 3V3 to the pink wire is the pull-up. Together with the button
  resistances in the steering pad it forms the voltage divider read by the ADC.
- 1 kΩ in series and 1 µF from GP2 to GND (+ leg on GP2) protect the pin and filter noise on the long
  wire from the steering wheel.
- RP2040 GND goes to SWG. Check with a multimeter that SWG is at chassis
  ground (≈0 Ω to bare metal) — it should be, since it's the radio's switch
  ground.
- Power the board from USB. If you use a laptop in the car, keep it on battery
  (not on a car charger) to avoid ground loops.

The onboard WS2812 LED is on GP29 (the "RGB (29)" on the silkscreen).
At power-up it flashes red, green, blue as a self-test; if red and green come
out swapped, the LED uses RGB order instead of GRB — MODE is blue either way.

## Build and flash

```sh
cd re/mode-button-test
pio run -t upload
pio device monitor
```

If upload doesn't find the board, hold **BOOT** while plugging USB in: it
mounts as `RPI-RP2` and PlatformIO (or a drag-and-drop of
`.pio/build/rp2040-tiny/firmware.uf2`) can flash it.

## Reading the output

```
GP2 = HIGH  [none]
>> MODE pressed
GP2 = LOW   [MODE]
>> released
```

The idle level depends on the fixed resistor across SW2–SWG in the steering
pad: with the 10 kΩ pull-up it reads HIGH only if that resistor is above
≈15 kΩ. If GP2 reads LOW with nothing pressed, measure that resistor and
lower the pull-up accordingly.

## Result in the car

MODE pulls GP2 LOW and lights the LED, and so do the hands-free On Hook and
Off Hook buttons: their resistors are low enough to read LOW too. Idle reads
HIGH, so the fixed resistor across the pad is above the threshold. The
cockpit firmware reads the same line on GPIO4 by voltage to tell MODE apart
from the phone buttons — see `firmware/src/steering_buttons.h`.

