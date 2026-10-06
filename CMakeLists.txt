cmake_minimum_required(VERSION 3.25)
project(WinFlux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "WinFlux targets x64 only.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WinFluxCompilerOptions)

add_library(flux STATIC
    src/platform/AppHost.cpp
    src/platform/Clipboard.cpp
    src/platform/Settings.cpp
    src/render/D2DRenderer.cpp
    src/render/DCompPresenter.cpp
    src/render/HwndPresenter.cpp
    src/render/Damage.cpp
    src/layout/FlexLayout.cpp
    src/theme/Theme.cpp
    src/core/Host.cpp
    src/components/Navigation.cpp
    src/components/Overlays.cpp
    src/components/Charts.cpp
    src/components/Menus.cpp
    src/components/Layout.cpp
    src/components/Data.cpp
    src/components/Pickers.cpp
    src/components/DataViz.cpp
)
add_library(WinFlux::flux ALIAS flux)

target_include_directories(flux PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(flux PUBLIC UNICODE _UNICODE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(flux PUBLIC d2d1 dwrite windowscodecs dcomp d3d11 dxgi)
flux_apply_compiler_options(flux)

add_executable(WinFluxGallery WIN32 samples/Gallery/Gallery.cpp)
target_link_libraries(WinFluxGallery PRIVATE WinFlux::flux)
flux_apply_compiler_options(WinFluxGallery)

# Round 5 dogfooding target: a realistic workbench built only from
# flux::ui components — proves the library composes into real apps.
add_executable(WinFluxDashboard WIN32 samples/Dashboard/Dashboard.cpp)
target_link_libraries(WinFluxDashboard PRIVATE WinFlux::flux)
flux_apply_compiler_options(WinFluxDashboard)

enable_testing()
add_executable(WinFluxSmokeTest tests/smoke.cpp)
target_link_libraries(WinFluxSmokeTest PRIVATE WinFlux::flux)
flux_apply_compiler_options(WinFluxSmokeTest)
add_test(NAME WinFluxSmokeTest COMMAND WinFluxSmokeTest)

# Harness finding #2: Host lifecycle / presenter selection is not headless,
# so it lives in its own CTest target. Failure is isolated from the always-
# fast smoke test (which stays purely CPU).
add_executable(WinFluxHostLifecycleTest tests/host_lifecycle.cpp)
target_link_libraries(WinFluxHostLifecycleTest PRIVATE WinFlux::flux)
flux_apply_compiler_options(WinFluxHostLifecycleTest)
add_test(NAME WinFluxHostLifecycleTest COMMAND WinFluxHostLifecycleTest)

# Token contract: DESIGN.md <-> include/flux/Theme.hpp must stay in sync
# (review 2026-10-06, 06-R7 — the third copy lives in the sibling nfui
# library and is only enforceable by discipline).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME TokenContract
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_tokens.py)
endif()
