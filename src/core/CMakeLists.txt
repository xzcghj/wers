add_library(wers_core STATIC
    dsp/Loudness.cpp
    dsp/DeviceProfiles.cpp
    dsp/Monitor.cpp
    dsp/PhonemeClassifier.cpp
    dsp/Fonem.cpp
    dsp/Place.cpp
    dsp/Space.cpp
    dsp/VocalFix.cpp
    analysis/Sii.cpp
    analysis/Frames.cpp
    analysis/FixPlanner.cpp
    analysis/LyricAligner.cpp
    analysis/SongSession.cpp
    analysis/WhisperRecognizer.cpp
    link/TimelineStore.cpp
    link/LinkHub.cpp
)
target_include_directories(wers_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(wers_core PRIVATE ${WERS_WARNINGS})
find_package(Threads REQUIRED)
target_link_libraries(wers_core PUBLIC Threads::Threads)
if(WERS_WITH_WHISPER)
    target_compile_definitions(wers_core PRIVATE WERS_HAS_WHISPER=1)
    target_link_libraries(wers_core PRIVATE whisper)
else()
    target_compile_definitions(wers_core PRIVATE WERS_HAS_WHISPER=0)
endif()
