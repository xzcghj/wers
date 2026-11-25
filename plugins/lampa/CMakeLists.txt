# WERS Lampa: a warm character compressor (optical cell and variable-mu tube), standalone project.
cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
cmake_policy(SET CMP0091 NEW)
project(WersLampa VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# A static runtime means the plugin needs no Visual C++ redistributable on the machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LAMPA_BUILD_TESTS "Build tests and tools" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(MSVC)
    set(LAMPA_WARNINGS /W4 /permissive- /utf-8)
else()
    set(LAMPA_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<folder> and -DFETCHCONTENT_SOURCE_DIR_DOCTEST=<folder> to use local copies.
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(LAMPA_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

set(WERS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# --- DSP: plain C++, header only, testable without JUCE -------------------------------------------
add_library(lampa_dsp INTERFACE)
target_include_directories(lampa_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp)

if(LAMPA_BUILD_TESTS)
    add_executable(lampa_dsp_tests tests/dsp_tests.cpp)
    target_include_directories(lampa_dsp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${WERS_ROOT}/libs/test)
    target_link_libraries(lampa_dsp_tests PRIVATE lampa_dsp)
    target_compile_options(lampa_dsp_tests PRIVATE ${LAMPA_WARNINGS})
    if(MSVC)
        target_compile_options(lampa_dsp_tests PRIVATE /O2)
    endif()
    add_test(NAME lampa_dsp_tests COMMAND lampa_dsp_tests)
endif()

# --- Plugin ---------------------------------------------------------------------------------------
add_subdirectory(${WERS_ROOT}/libs/ui ${CMAKE_CURRENT_BINARY_DIR}/wers_ui)

juce_add_plugin(WersLampa
    COMPANY_NAME "WERS"
    COMPANY_WEBSITE ""
    BUNDLE_ID "app.wers.lampa"
    PLUGIN_MANUFACTURER_CODE Wers
    PLUGIN_CODE Wlam
    FORMATS VST3
    PRODUCT_NAME "WERS Lampa"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    VERSION ${PROJECT_VERSION})

target_sources(WersLampa PRIVATE
    src/plugin/Params.cpp
    src/plugin/FactoryPresets.cpp
    src/plugin/LampaProcessor.cpp
    src/ui/LampaLook.cpp
    src/ui/Knob.cpp
    src/ui/VuMeter.cpp
    src/ui/LampaPresetBar.cpp
    src/ui/LampaEditor.cpp)
target_include_directories(WersLampa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(WersLampa PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    DONT_SET_USING_JUCE_NAMESPACE=1)
if(MSVC)
    target_compile_options(WersLampa PRIVATE /utf-8)
endif()
target_link_libraries(WersLampa
    PRIVATE
        lampa_dsp
        wers_ui
        WersFonts
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Programs that use the plugin's JUCE modules without compiling them a second time.
function(lampa_add_plugin_program name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${doctest_SOURCE_DIR}/doctest
        ${WERS_ROOT}/libs/test
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${JUCE_MODULES_DIR}
        "$<TARGET_PROPERTY:WersLampa,INCLUDE_DIRECTORIES>")
    target_compile_definitions(${name} PRIVATE "$<TARGET_PROPERTY:WersLampa,COMPILE_DEFINITIONS>")
    target_link_libraries(${name} PRIVATE WersLampa)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
endfunction()

if(LAMPA_BUILD_TESTS)
    lampa_add_plugin_program(lampa_plugin_tests tests/plugin_tests.cpp)
    add_test(NAME lampa_plugin_tests COMMAND lampa_plugin_tests)
    lampa_add_plugin_program(lampa_ui_shots tests/ui_shots.cpp)
    lampa_add_plugin_program(lampa_render tools/lampa_render.cpp)
endif()
