cmake_minimum_required(VERSION 3.16)
project(cockpit_screenshot CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Same LV_CONF wiring as ../CMakeLists.txt, and for the same reason -- these
# MUST be cache entries, because LVGL's own option() calls unset same-named
# plain variables before writing their own. Set plainly, the screenshots would
# render against LV_CONF_SKIP defaults instead of this project's real
# lv_conf.h. See the long comment in ../CMakeLists.txt.
set(LV_CONF_PATH ${PROJECT_ROOT}/include/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v8.3.11
)
FetchContent_MakeAvailable(lvgl)

# No SDL, no display driver, no CAN decoder: this renders the UI layer only,
# fed by a scripted VehicleState. imu.cpp is deliberately absent too --
# screenshot.cpp provides its own Imu:: so the G-meter's calibration runs
# against synthesized motion (see that file).
add_executable(screenshot
    screenshot.cpp
    ${PROJECT_ROOT}/src/g_meter.cpp
    ${PROJECT_ROOT}/src/screen_nav.cpp
    ${PROJECT_ROOT}/src/touch_nav.cpp
    ${PROJECT_ROOT}/src/hybrid_stats.cpp
    ${PROJECT_ROOT}/src/pwr_history.cpp
    ${PROJECT_ROOT}/src/accel_timer.cpp
    ${PROJECT_ROOT}/src/ui/app_ui.cpp
    ${PROJECT_ROOT}/src/ui/glow.cpp
    ${PROJECT_ROOT}/src/ui/cockpit_ui.cpp
    ${PROJECT_ROOT}/src/ui/energy_flow_ui.cpp
    ${PROJECT_ROOT}/src/ui/efficiency_ui.cpp
    ${PROJECT_ROOT}/src/ui/gmeter_ui.cpp
    ${PROJECT_ROOT}/src/ui/summary_ui.cpp
    ${PROJECT_ROOT}/src/ui/boot_ui.cpp
    ${PROJECT_ROOT}/sim/usb_dump_sim.cpp
    ${PROJECT_ROOT}/src/ui/boot_images.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_106_speed.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_13_pct.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_14_chgpwr.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_25_battery.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_25_rpm.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_27_ev.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_29_units.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_39_gear.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_23_label.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_27_stat.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_39_accel_time.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_39_accel_label.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_64_speed.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_20_small.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_16_rpm.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_13_tag.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_25_value.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_35_value.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_19_value.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_12_label.c
    ${PROJECT_ROOT}/src/ui/fonts/spacegrotesk_14_caption.c
)

target_compile_definitions(screenshot PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)

# The Arduino/Wire shims come first so <Arduino.h> resolves to the simulator's
# stand-ins rather than to anything on the system.
target_include_directories(screenshot BEFORE PRIVATE
    ${PROJECT_ROOT}/sim/arduino_shim
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(screenshot PRIVATE lvgl)
find_package(Threads REQUIRED)
target_link_libraries(screenshot PRIVATE Threads::Threads)
