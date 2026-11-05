# DSP shared by the plugins of the series. No JUCE here, so it can be tested on its own.
add_library(wers_dsp STATIC
    dsp/Loudness.cpp
    dsp/PhonemeClassifier.cpp
)
target_include_directories(wers_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_compile_options(wers_dsp PRIVATE ${WERS_WARNINGS})
