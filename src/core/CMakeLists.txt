add_library(wers_core STATIC
    dsp/Loudness.cpp
    dsp/DeviceProfiles.cpp
    dsp/Monitor.cpp
    dsp/PhonemeClassifier.cpp
    dsp/Fonem.cpp
    dsp/Place.cpp
    dsp/Space.cpp
)
target_include_directories(wers_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(wers_core PRIVATE ${WERS_WARNINGS})
