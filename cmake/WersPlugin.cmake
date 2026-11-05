# One way to declare a plugin of the series, so every plugin gets the same settings.
function(wers_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT;CODE;BUNDLE" "CATEGORIES" ${ARGN})
    juce_add_plugin(${target}
        COMPANY_NAME "WERS"
        COMPANY_WEBSITE ""
        BUNDLE_ID "${ARG_BUNDLE}"
        PLUGIN_MANUFACTURER_CODE Wers
        PLUGIN_CODE ${ARG_CODE}
        FORMATS VST3
        PRODUCT_NAME "${ARG_PRODUCT}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${ARG_CATEGORIES}
        VERSION ${PROJECT_VERSION})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    target_compile_definitions(${target} PUBLIC
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
    target_link_libraries(${target}
        PRIVATE
            wers_ui
            WersFonts
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

# JUCE modules are compiled into the plugin target, so a test program takes its headers and definitions
# without compiling the modules a second time.
function(wers_add_plugin_program plugin name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${doctest_SOURCE_DIR}/doctest
        ${JUCE_MODULES_DIR}
        "$<TARGET_PROPERTY:${plugin},INCLUDE_DIRECTORIES>")
    target_compile_definitions(${name} PRIVATE "$<TARGET_PROPERTY:${plugin},COMPILE_DEFINITIONS>")
    target_link_libraries(${name} PRIVATE ${plugin})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
endfunction()
