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
    src/render/D2DRenderer.cpp
    src/layout/FlexLayout.cpp
    src/theme/Theme.cpp
    src/core/Host.cpp
)
add_library(WinFlux::flux ALIAS flux)

target_include_directories(flux PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(flux PUBLIC UNICODE _UNICODE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(flux PUBLIC d2d1 dwrite windowscodecs)
flux_apply_compiler_options(flux)

add_executable(WinFluxGallery WIN32 samples/Gallery/Gallery.cpp)
target_link_libraries(WinFluxGallery PRIVATE WinFlux::flux)
flux_apply_compiler_options(WinFluxGallery)

enable_testing()
add_executable(WinFluxSmokeTest tests/smoke.cpp)
target_link_libraries(WinFluxSmokeTest PRIVATE WinFlux::flux)
flux_apply_compiler_options(WinFluxSmokeTest)
add_test(NAME WinFluxSmokeTest COMMAND WinFluxSmokeTest)
