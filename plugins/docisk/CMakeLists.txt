# WERS Docisk: a clean, precise compressor. A standalone project that shares JUCE, doctest and libs/ui.
cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
cmake_policy(SET CMP0091 NEW)
project(WersDocisk VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# A static runtime means the plugin needs no Visual C++ redistributable on the machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DOC_BUILD_TESTS "Build tests" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(MSVC)
    set(DOC_WARNINGS /W4 /permissive- /utf-8)
else()
    set(DOC_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<folder> and -DFETCHCONTENT_SOURCE_DIR_DOCTEST=<folder> to use local copies.
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(DOC_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

set(DOC_REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# --- DSP: plain C++, header only, testable without JUCE -------------------------------------------
add_library(doc_dsp INTERFACE)
target_include_directories(doc_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp)

if(DOC_BUILD_TESTS)
    add_executable(doc_dsp_tests tests/dsp_tests.cpp)
    target_include_directories(doc_dsp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${DOC_REPO}/libs/test)
    target_link_libraries(doc_dsp_tests PRIVATE doc_dsp)
    target_compile_options(doc_dsp_tests PRIVATE ${DOC_WARNINGS})
    if(MSVC)
        target_compile_options(doc_dsp_tests PRIVATE /O2)
    endif()
    add_test(NAME doc_dsp_tests COMMAND doc_dsp_tests)
endif()

# --- Plugin ---------------------------------------------------------------------------------------
add_subdirectory(${DOC_REPO}/libs/ui ${CMAKE_CURRENT_BINARY_DIR}/wers_ui)

juce_add_plugin(WersDocisk
    COMPANY_NAME "WERS"
    COMPANY_WEBSITE ""
    BUNDLE_ID "app.wers.docisk"
    PLUGIN_MANUFACTURER_CODE Wers
    PLUGIN_CODE Wdoc
    FORMATS VST3
    PRODUCT_NAME "WERS Docisk"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    VERSION ${PROJECT_VERSION})

target_sources(WersDocisk PRIVATE
    src/plugin/Params.cpp
    src/plugin/FactoryPresets.cpp
    src/plugin/DociskProcessor.cpp
    src/ui/DocLook.cpp
    src/ui/Knob.cpp
    src/ui/History.cpp
    src/ui/Curve.cpp
    src/ui/Meters.cpp
    src/ui/DociskEditor.cpp)
target_include_directories(WersDocisk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(WersDocisk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    DONT_SET_USING_JUCE_NAMESPACE=1)
if(MSVC)
    target_compile_options(WersDocisk PRIVATE /utf-8)
endif()
target_link_libraries(WersDocisk
    PRIVATE
        doc_dsp
        wers_ui
        WersFonts
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Programs that use the plugin's JUCE modules without compiling them a second time.
function(doc_add_plugin_program name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${doctest_SOURCE_DIR}/doctest
        ${DOC_REPO}/libs/test
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${JUCE_MODULES_DIR}
        "$<TARGET_PROPERTY:WersDocisk,INCLUDE_DIRECTORIES>")
    target_compile_definitions(${name} PRIVATE "$<TARGET_PROPERTY:WersDocisk,COMPILE_DEFINITIONS>")
    target_link_libraries(${name} PRIVATE WersDocisk)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
endfunction()

if(DOC_BUILD_TESTS)
    doc_add_plugin_program(doc_plugin_tests tests/plugin_tests.cpp)
    add_test(NAME doc_plugin_tests COMMAND doc_plugin_tests)
    doc_add_plugin_program(doc_ui_shots tests/ui_shots.cpp)
    doc_add_plugin_program(doc_render tools/doc_render.cpp)
endif()
