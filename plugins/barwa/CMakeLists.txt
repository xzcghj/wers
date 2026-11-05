# WERS Barwa: a saturator that gives colour to vowels and leaves sibilants clean.
add_library(barwa_dsp STATIC
    dsp/BarwaEngine.cpp
    dsp/CharacterShaper.cpp
)
target_include_directories(barwa_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(barwa_dsp PUBLIC wers_dsp)
target_compile_options(barwa_dsp PRIVATE ${WERS_WARNINGS})

if(WERS_BUILD_TESTS)
    add_executable(barwa_tests
        tests/test_main.cpp
        tests/test_character.cpp
        tests/test_engine.cpp
    )
    target_include_directories(barwa_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)
    target_link_libraries(barwa_tests PRIVATE barwa_dsp wers_test_support)
    target_compile_options(barwa_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME barwa_tests COMMAND barwa_tests)
endif()

if(WERS_BUILD_PLUGIN)
    wers_add_plugin(WersBarwa PRODUCT "WERS Barwa" CODE Wbrw BUNDLE "app.wers.barwa" CATEGORIES Fx Distortion)
    target_sources(WersBarwa PRIVATE
        plugin/Params.cpp
        plugin/BarwaProcessor.cpp
        ui/BarwaEditor.cpp
        ui/SpectrumView.cpp
        ui/MaskStrip.cpp)
    target_include_directories(WersBarwa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(WersBarwa PRIVATE barwa_dsp)
    if(WERS_BUILD_TESTS)
        wers_add_plugin_program(WersBarwa barwa_plugin_tests tests/plugin/barwa_plugin_tests.cpp)
        add_test(NAME barwa_plugin_tests COMMAND barwa_plugin_tests)
        wers_add_plugin_program(WersBarwa barwa_ui_shots tests/plugin/barwa_ui_shots.cpp)
        target_link_libraries(barwa_ui_shots PRIVATE wers_test_support)
    endif()
endif()
