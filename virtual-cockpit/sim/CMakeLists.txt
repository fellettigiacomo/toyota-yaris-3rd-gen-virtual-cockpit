cmake_minimum_required(VERSION 3.16)
project(yaris_cockpit_sim CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(SDL2 REQUIRED)

# --- LVGL 8.3.11, same version platformio.ini pins for the firmware build ---
# LV_CONF_PATH points LVGL's own CMake at this project's real include/lv_conf.h
# (not a separate simulator-only copy) so the simulator renders with the exact
# same widget/color config the firmware ships with -- e.g. LV_COLOR_16_SWAP=1
# matters for correctness here, see lv_port_disp_sdl.cpp's flush_cb for why
# the byte-swap setting specifically needs undoing for SDL.
set(LV_CONF_PATH ${PROJECT_ROOT}/include/lv_conf.h)
set(LV_CONF_INCLUDE_SIMPLE ON)
set(LV_LVGL_H_INCLUDE_SIMPLE ON)

include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v8.3.11
)
FetchContent_MakeAvailable(lvgl)

add_executable(sim
    main_sim.cpp
    lv_port_disp_sdl.cpp
    ${PROJECT_ROOT}/src/can_decoder.cpp
    ${PROJECT_ROOT}/src/demo_log_data.cpp
    ${PROJECT_ROOT}/src/screen_nav.cpp
    ${PROJECT_ROOT}/src/hybrid_stats.cpp
    ${PROJECT_ROOT}/src/ui/bar_gauge.cpp
    ${PROJECT_ROOT}/src/ui/cockpit_ui.cpp
    ${PROJECT_ROOT}/src/ui/energy_flow_ui.cpp
    ${PROJECT_ROOT}/src/ui/efficiency_ui.cpp
    ${PROJECT_ROOT}/src/ui/flow_arrow.cpp
    ${PROJECT_ROOT}/src/ui/app_ui.cpp
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_120_speed.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_13_pct.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_14_chgpwr.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_26_battery.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_26_rpm.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_28_ev.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_30_units.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_40_gear.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_24_label.c
    ${PROJECT_ROOT}/src/ui/fonts/dinnext_28_stat.c
)

target_compile_definitions(sim PRIVATE DEMO_FAKE_DATA)

target_include_directories(sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/arduino_shim # provides a stand-in <Arduino.h>
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

if(TARGET SDL2::SDL2)
    target_link_libraries(sim PRIVATE SDL2::SDL2 lvgl)
else()
    # Older/plain FindSDL2.cmake modules only set variables, no imported target.
    target_include_directories(sim PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(sim PRIVATE ${SDL2_LIBRARIES} lvgl)
endif()
