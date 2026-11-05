# DSP shared by the plugins of the series. No JUCE here, so it can be tested on its own.
add_library(wers_dsp STATIC
    dsp/Crossover.cpp
    dsp/LevelMatch.cpp
    dsp/Loudness.cpp
    dsp/Oversampler.cpp
    dsp/PhonemeClassifier.cpp
)
target_include_directories(wers_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_compile_options(wers_dsp PRIVATE ${WERS_WARNINGS})

if(WERS_BUILD_TESTS)
    add_executable(wers_dsp_tests tests/test_dsp.cpp)
    target_include_directories(wers_dsp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)
    target_link_libraries(wers_dsp_tests PRIVATE wers_dsp wers_test_support)
    target_compile_options(wers_dsp_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME wers_dsp_tests COMMAND wers_dsp_tests)
endif()
