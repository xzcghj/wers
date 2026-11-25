# WERS Pogłos: algorithmic reverb. A standalone project that shares libs/ui and the test helpers of the repo.
cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
cmake_policy(SET CMP0091 NEW)
project(WersPoglos VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# A static runtime means the plugin needs no Visual C++ redistributable on the machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(POG_BUILD_TESTS "Build tests" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(MSVC)
    set(POG_WARNINGS /W4 /permissive- /utf-8)
else()
    set(POG_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

set(WERS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<folder> and -DFETCHCONTENT_SOURCE_DIR_DOCTEST=<folder> to use local copies.
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(POG_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

# --- DSP: plain C++, header only, testable without JUCE -------------------------------------------
add_library(pog_dsp INTERFACE)
target_include_directories(pog_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp)

if(POG_BUILD_TESTS)
    add_executable(pog_dsp_tests tests/dsp_tests.cpp)
    target_include_directories(pog_dsp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${WERS_ROOT}/libs/test)
    target_link_libraries(pog_dsp_tests PRIVATE pog_dsp)
    target_compile_options(pog_dsp_tests PRIVATE ${POG_WARNINGS})
    if(MSVC)
        target_compile_options(pog_dsp_tests PRIVATE /O2)
    endif()
    add_test(NAME pog_dsp_tests COMMAND pog_dsp_tests)
endif()

# --- Plugin ---------------------------------------------------------------------------------------
add_subdirectory(${WERS_ROOT}/libs/ui ${CMAKE_CURRENT_BINARY_DIR}/wers_ui)

juce_add_plugin(WersPoglos
    COMPANY_NAME "WERS"
    COMPANY_WEBSITE ""
    BUNDLE_ID "app.wers.poglos"
    PLUGIN_MANUFACTURER_CODE Wers
    PLUGIN_CODE Wpog
    FORMATS VST3
    # ASCII on purpose: the name reaches the binary through the compiler's command line in the ANSI code
    # page, so "ł" would come out as broken UTF-8 and hosts could not read the plugin's description.
    PRODUCT_NAME "WERS Poglos"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Reverb
    VERSION ${PROJECT_VERSION})

target_sources(WersPoglos PRIVATE
    src/plugin/Params.cpp
    src/plugin/FactoryPresets.cpp
    src/plugin/PoglosProcessor.cpp
    src/ui/PogLook.cpp
    src/ui/Knob.cpp
    src/ui/DecayView.cpp
    src/ui/SpaceView.cpp
    src/ui/Pickers.cpp
    src/ui/PogPresetBar.cpp
    src/ui/PoglosEditor.cpp)
target_include_directories(WersPoglos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(WersPoglos PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    DONT_SET_USING_JUCE_NAMESPACE=1)
if(MSVC)
    target_compile_options(WersPoglos PRIVATE /utf-8 /fp:fast)
endif()
target_link_libraries(WersPoglos
    PRIVATE
        pog_dsp
        wers_ui
        WersFonts
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Programs that use the plugin's JUCE modules without compiling them a second time.
function(pog_add_plugin_program name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${doctest_SOURCE_DIR}/doctest
        ${WERS_ROOT}/libs/test
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${JUCE_MODULES_DIR}
        "$<TARGET_PROPERTY:WersPoglos,INCLUDE_DIRECTORIES>")
    target_compile_definitions(${name} PRIVATE "$<TARGET_PROPERTY:WersPoglos,COMPILE_DEFINITIONS>")
    target_link_libraries(${name} PRIVATE WersPoglos)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
endfunction()

if(POG_BUILD_TESTS)
    pog_add_plugin_program(pog_plugin_tests tests/plugin_tests.cpp)
    add_test(NAME pog_plugin_tests COMMAND pog_plugin_tests)
    pog_add_plugin_program(pog_ui_shots tests/ui_shots.cpp)
    pog_add_plugin_program(pog_render tools/pog_render.cpp)
endif()
