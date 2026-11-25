# WERS Powiew: a dynamic air enhancer. A standalone project, built in powiew/build.
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE=<juce> -DFETCHCONTENT_SOURCE_DIR_DOCTEST=<doctest>
cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
cmake_policy(SET CMP0091 NEW)
project(WersPowiew VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# A static runtime means the plugin needs no Visual C++ redistributable on the machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(POWIEW_BUILD_TESTS "Build tests and tools" ON)
option(POWIEW_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(MSVC)
    set(POW_WARNINGS /W4 /permissive- /utf-8)
else()
    set(POW_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

set(WERS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(POWIEW_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

# --- DSP: plain C++, header only, testable without JUCE -------------------------------------------
add_library(pow_dsp INTERFACE)
target_include_directories(pow_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp)

if(POWIEW_BUILD_TESTS)
    add_executable(pow_dsp_tests tests/dsp_tests.cpp)
    target_include_directories(pow_dsp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${WERS_ROOT}/libs/test ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(pow_dsp_tests PRIVATE pow_dsp)
    target_compile_options(pow_dsp_tests PRIVATE ${POW_WARNINGS})
    if(MSVC)
        target_compile_options(pow_dsp_tests PRIVATE /O2)
    endif()
    add_test(NAME pow_dsp_tests COMMAND pow_dsp_tests)
endif()

if(NOT POWIEW_BUILD_PLUGIN)
    return()
endif()

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<folder> to use a local copy.
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The look of the series, shared read-only with the other plugins.
add_subdirectory(${WERS_ROOT}/libs/ui ${CMAKE_CURRENT_BINARY_DIR}/wers_ui)

# --- Plugin ---------------------------------------------------------------------------------------
juce_add_plugin(WersPowiew
    COMPANY_NAME "WERS"
    COMPANY_WEBSITE ""
    BUNDLE_ID "app.wers.powiew"
    PLUGIN_MANUFACTURER_CODE Wers
    PLUGIN_CODE Wpow
    FORMATS VST3
    PRODUCT_NAME "WERS Powiew"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    VERSION ${PROJECT_VERSION})

target_sources(WersPowiew PRIVATE
    src/plugin/Params.cpp
    src/plugin/FactoryPresets.cpp
    src/plugin/PowiewProcessor.cpp
    src/ui/PowLook.cpp
    src/ui/Knob.cpp
    src/ui/AirView.cpp
    src/ui/BandCard.cpp
    src/ui/PowiewEditor.cpp)
target_include_directories(WersPowiew PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(WersPowiew PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    DONT_SET_USING_JUCE_NAMESPACE=1)
if(MSVC)
    target_compile_options(WersPowiew PRIVATE /utf-8)
endif()
target_link_libraries(WersPowiew
    PRIVATE
        pow_dsp
        wers_ui
        WersFonts
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Programs that use the plugin's JUCE modules without compiling them a second time.
function(pow_add_plugin_program name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${doctest_SOURCE_DIR}/doctest
        ${WERS_ROOT}/libs/test
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${JUCE_MODULES_DIR}
        "$<TARGET_PROPERTY:WersPowiew,INCLUDE_DIRECTORIES>")
    target_compile_definitions(${name} PRIVATE "$<TARGET_PROPERTY:WersPowiew,COMPILE_DEFINITIONS>")
    target_link_libraries(${name} PRIVATE WersPowiew)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
endfunction()

if(POWIEW_BUILD_TESTS)
    pow_add_plugin_program(pow_plugin_tests tests/plugin_tests.cpp)
    add_test(NAME pow_plugin_tests COMMAND pow_plugin_tests)
    pow_add_plugin_program(pow_ui_shots tests/ui_shots.cpp)
    pow_add_plugin_program(pow_render tools/pow_render.cpp)
endif()
