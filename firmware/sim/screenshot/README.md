# Screenshot renderer

Regenerates the screenshots the top-level README shows, straight from the real
LVGL UI — no SDL, no window, no board. Every pixel comes out of the same
`build()` / `update()` the firmware calls, so these frames are the cluster,
not a mockup of it.

```sh
cd firmware/sim/screenshot
./render.sh
```

That builds, plays the scenes, and writes `screenshots/*.png` at the panel's
native 640×172.

**It takes a couple of minutes**, most of it the efficiency scene. The UI
modules integrate against `millis()`, so the scenes run in real time rather
than fast-forwarded — and the efficiency screen's EV share is weighted by
distance, so the only way to reach a given split with a plausible trip behind
it is to actually drive the distance.

## What gets rendered

| File | Scene |
|---|---|
| `01_cockpit.png` | 64 km/h in D, engine running, 68% SOC, 19% PWR |
| `02_energy_flow.png` | Hybrid drive — engine to the wheels, pack assisting through the motor, one link idle |
| `03_efficiency.png` | ~2 km covered, 72% of it electric |
| `04_gmeter.png` | Trail-braking into a right-hander, after a drive that fills the peaks |

## Notes on the scenes

The scenes are scripted `VehicleState` sequences, not canned frames, so they
have to satisfy the same logic the car does:

- **The cockpit is rendered first, and never from a standstill.** `AccelTimer`
  arms itself at zero speed, so a scene that jumped from 0 to 64 km/h would
  latch a bogus 0–50 result and replace the gear letter with it.
- **The G-meter scene has to earn its axes.** `screenshot.cpp` provides its own
  `Imu::` rather than linking `src/imu.cpp`, and it deliberately does *not*
  hand the calibration pre-aligned axes: it synthesizes what a real
  accelerometer would have measured and rotates it through an arbitrary
  mounting (yaw 35°, pitch 20°, roll 10°). The scene then drives a standstill,
  a launch, braking and both corners so `g_meter.cpp` can find level, learn
  which way is forward, and fill the peak tiles — exactly as it would in the
  car.
- **Screens are cycled through the real BOOT-button path**, debounce included,
  rather than by reaching into `AppUi`'s internals.

This is why the renderer is worth having beyond documentation: it exercises
the parts that are otherwise only testable in a moving car. It has already
caught a real bug — the G-meter's level capture was being tilted by braking to
a halt, which showed up here long before the code reached a vehicle.

## Requirements

CMake ≥ 3.16, a C++17 compiler, and Python 3 (standard library only — the
converter uses `zlib` and `struct`). LVGL 8.3.11 is fetched automatically,
the same version `platformio.ini` pins for the firmware and `../CMakeLists.txt`
uses for the simulator.
