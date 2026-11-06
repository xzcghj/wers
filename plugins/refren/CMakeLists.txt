# WERS Refren: doubles, crews, choirs and effects from one vocal track, with words that stay clear.
add_library(refren_dsp STATIC
    dsp/Echo.cpp
    dsp/Grain.cpp
    dsp/Layer.cpp
    dsp/MotionFx.cpp
    dsp/Psola.cpp
    dsp/RefrenEngine.cpp
    dsp/Reverb.cpp
    dsp/SubVoice.cpp
    dsp/VoiceAnalysis.cpp
    dsp/VoiceMask.cpp
    dsp/WhisperVoice.cpp
)
target_include_directories(refren_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(refren_dsp PUBLIC wers_dsp)
target_compile_options(refren_dsp PRIVATE ${WERS_WARNINGS})

# Listening samples and measurements from the user's own vocals, on the user's computer.
add_library(refren_tools_lib STATIC tools/RenderProbes.cpp tools/MeasureVoice.cpp)
target_include_directories(refren_tools_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(refren_tools_lib PUBLIC refren_dsp wers_test_support)
target_compile_options(refren_tools_lib PRIVATE ${WERS_WARNINGS})
add_executable(refren_render tools/refren_render.cpp)
target_link_libraries(refren_render PRIVATE refren_tools_lib)
target_compile_options(refren_render PRIVATE ${WERS_WARNINGS})
add_executable(refren_measure tools/refren_measure.cpp)
target_link_libraries(refren_measure PRIVATE refren_tools_lib)
target_compile_options(refren_measure PRIVATE ${WERS_WARNINGS})

if(WERS_BUILD_TESTS)
    add_executable(refren_tests
        tests/test_main.cpp
        tests/test_pitch.cpp
        tests/test_psola.cpp
        tests/test_sources.cpp
        tests/test_layers.cpp
        tests/test_bus.cpp
    )
    target_include_directories(refren_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(refren_tests PRIVATE refren_dsp refren_tools_lib wers_test_support)
    target_compile_options(refren_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME refren_tests COMMAND refren_tests)
endif()

if(WERS_BUILD_PLUGIN)
    wers_add_plugin(WersRefren PRODUCT "WERS Refren" CODE Wrfn BUNDLE "app.wers.refren" CATEGORIES Fx Modulation)
    target_sources(WersRefren PRIVATE
        plugin/Params.cpp
        plugin/RefrenProcessor.cpp)
    target_include_directories(WersRefren PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
    target_link_libraries(WersRefren PRIVATE refren_dsp)
    if(WERS_BUILD_TESTS)
        wers_add_plugin_program(WersRefren refren_plugin_tests tests/plugin/refren_plugin_tests.cpp)
        target_link_libraries(refren_plugin_tests PRIVATE refren_tools_lib wers_test_support)
        add_test(NAME refren_plugin_tests COMMAND refren_plugin_tests)
    endif()
endif()
