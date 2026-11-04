FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(WERS
    COMPANY_NAME "WERS"
    COMPANY_WEBSITE ""
    BUNDLE_ID "app.wers.vocal"
    PLUGIN_MANUFACTURER_CODE Wers
    PLUGIN_CODE Wrs1
    FORMATS VST3
    PRODUCT_NAME "WERS"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    VERSION ${PROJECT_VERSION})

juce_add_binary_data(WersAssets SOURCES
    ../../assets/fonts/Bricolage-300.ttf
    ../../assets/fonts/Bricolage-400.ttf
    ../../assets/fonts/Bricolage-500.ttf
    ../../assets/fonts/Bricolage-600.ttf
    ../../assets/fonts/Bricolage-700.ttf
    ../../assets/fonts/Bricolage-800.ttf
    ../../assets/fonts/Instrument-400.ttf
    ../../assets/fonts/Instrument-500.ttf
    ../../assets/fonts/Instrument-600.ttf)

target_sources(WERS PRIVATE
    Params.cpp
    PluginProcessor.cpp
    ModelManager.cpp
    ProcessRecognizer.cpp
    ../ui/Theme.cpp
    ../ui/Controls.cpp
    ../ui/WordMapView.cpp
    ../ui/WordPanel.cpp
    ../ui/Modules.cpp
    ../ui/RoleViews.cpp
    ../ui/PluginEditor.cpp)

target_include_directories(WERS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. )
if(MSVC)
    target_compile_options(WERS PRIVATE /utf-8)
endif()

target_compile_definitions(WERS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_USE_XRANDR=0
    JUCE_USE_XINERAMA=0
    JUCE_USE_XCURSOR=0
    JUCE_USE_XRENDER=0)

target_link_libraries(WERS
    PRIVATE
        WersAssets
        wers_core
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(WERS_WITH_WHISPER)
    # The recognition process travels inside the plugin bundle.
    add_dependencies(WERS_VST3 wers_whisper)
    add_custom_command(TARGET WERS_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:wers_whisper> $<TARGET_FILE_DIR:WERS_VST3>/$<TARGET_FILE_NAME:wers_whisper>
        VERBATIM)
endif()

if(WERS_BUILD_TESTS)
    # JUCE modules are compiled into the WERS target, so the test programs take its headers and
    # definitions without compiling the modules a second time.
    function(wers_add_plugin_program name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE
            ${doctest_SOURCE_DIR}/doctest
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${JUCE_MODULES_DIR}
            "$<TARGET_PROPERTY:WERS,INCLUDE_DIRECTORIES>")
        target_compile_definitions(${name} PRIVATE "$<TARGET_PROPERTY:WERS,COMPILE_DEFINITIONS>")
        target_link_libraries(${name} PRIVATE WERS)
        if(MSVC)
            target_compile_options(${name} PRIVATE /utf-8)
        endif()
    endfunction()

    wers_add_plugin_program(wers_plugin_tests ../../tests/plugin/plugin_tests.cpp)
    if(WERS_WITH_WHISPER)
        add_dependencies(wers_plugin_tests wers_whisper)
        target_compile_definitions(wers_plugin_tests PRIVATE "WERS_TEST_HELPER=\"$<TARGET_FILE:wers_whisper>\"")
    endif()
    wers_add_plugin_program(wers_ui_shots ../../tests/plugin/ui_shots.cpp)
endif()
