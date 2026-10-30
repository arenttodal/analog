cmake_minimum_required(VERSION 3.25)

# ANDOR ANALOG: a virtual-analog polyphonic synthesizer in the ANDOR family.
#
# Built together with ANDOR OSP (the external/osp submodule), whose shared interface, shell,
# effects and test harness it uses:
#   dsp/        the synth engine, pure C++20 (no JUCE): analog_dsp
#   product/    the plugin, made with andor_add_instrument (AU / VST3 / Standalone)
#   tests/      the engine's tests (Catch2): analog_dsp_tests
#   tools/      analog-render, an offline renderer (WAV) for listening and plots
#
#   cmake -B build -G Ninja                              # engine, tests, renderer
#   cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON # + the plugin and its tests

project(ANDOR_ANALOG
    VERSION 0.1.0
    DESCRIPTION "ANDOR ANALOG: a living analog instrument"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(ANALOG_OSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/osp" CACHE PATH "ANDOR OSP checkout (the submodule)")
if(NOT EXISTS "${ANALOG_OSP_DIR}/shared/andor-shell/AndorInstrument.cmake")
    message(FATAL_ERROR "ANDOR OSP not found at ${ANALOG_OSP_DIR}: run `git submodule update --init`")
endif()

# ANDOR OSP builds the plugin products it is told about (shared/andor-shell) inside its own
# project, so the product can use its CMake functions and sources.
set(ANDOR_EXTERNAL_PRODUCTS "${CMAKE_CURRENT_SOURCE_DIR}/product" CACHE STRING "" FORCE)
enable_testing()
add_subdirectory(${ANALOG_OSP_DIR} osp)

add_library(analog_dsp STATIC
    dsp/FactoryPatches.cpp
    dsp/HalfBand.cpp
    dsp/JunoChorus.cpp
    dsp/LiveGranulator.cpp
    dsp/Synth.cpp)
target_include_directories(analog_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(analog_dsp PUBLIC osp_dsp)
set_target_properties(analog_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
osp_set_warnings(analog_dsp)

add_executable(analog-render tools/analog-render.cpp tools/Wav.cpp)
target_link_libraries(analog-render PRIVATE analog_dsp)
osp_set_warnings(analog-render)

if(OSP_BUILD_TESTS)
    add_executable(analog_dsp_tests
        tests/OscillatorTests.cpp
        tests/FilterTests.cpp
        tests/EnvelopeTests.cpp
        tests/VoiceTests.cpp
        tests/RenderTests.cpp
        tests/RealtimeTests.cpp
        tests/LiveTests.cpp
        tests/ModulationTests.cpp
        tests/LegacyTests.cpp
        tests/OtaFilterTests.cpp
        tests/ChorusTests.cpp
        tests/ModelTests.cpp
        tools/Wav.cpp)
    target_include_directories(analog_dsp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_link_libraries(analog_dsp_tests PRIVATE analog_dsp Catch2::Catch2WithMain)
    target_compile_definitions(analog_dsp_tests PRIVATE ANALOG_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    osp_set_warnings(analog_dsp_tests)
    add_test(NAME analog_dsp_tests COMMAND analog_dsp_tests)
endif()
