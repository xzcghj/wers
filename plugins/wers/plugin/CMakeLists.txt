wers_add_plugin(WERS PRODUCT "WERS" CODE Wrs1 BUNDLE "app.wers.vocal" CATEGORIES Fx Dynamics)

target_sources(WERS PRIVATE
    Params.cpp
    PluginProcessor.cpp
    ModelManager.cpp
    ProcessRecognizer.cpp
    ReaperBridge.cpp
    ../ui/WordMapView.cpp
    ../ui/WordPanel.cpp
    ../ui/Modules.cpp
    ../ui/RoleViews.cpp
    ../ui/PluginEditor.cpp)

target_include_directories(WERS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. )
target_link_libraries(WERS PRIVATE wers_core juce::juce_audio_formats)

if(WERS_WITH_WHISPER)
    # The recognition process travels inside the plugin bundle.
    add_dependencies(WERS_VST3 wers_whisper)
    add_custom_command(TARGET WERS_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:wers_whisper> $<TARGET_FILE_DIR:WERS_VST3>/$<TARGET_FILE_NAME:wers_whisper>
        VERBATIM)
endif()

if(WERS_BUILD_TESTS)
    wers_add_plugin_program(WERS wers_plugin_tests ../tests/plugin/plugin_tests.cpp)
    target_compile_definitions(wers_plugin_tests PRIVATE "WERS_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../tests/plugin/data\"")
    if(WERS_WITH_WHISPER)
        add_dependencies(wers_plugin_tests wers_whisper)
        target_compile_definitions(wers_plugin_tests PRIVATE "WERS_TEST_HELPER=\"$<TARGET_FILE:wers_whisper>\"")
    endif()
    add_test(NAME wers_plugin_tests COMMAND wers_plugin_tests)
    wers_add_plugin_program(WERS wers_ui_shots ../tests/plugin/ui_shots.cpp)
endif()
