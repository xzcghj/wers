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
    analysis/WordTiming.cpp
    analysis/SongSession.cpp
    analysis/CpuSupport.cpp
    analysis/HelperProtocol.cpp
    link/TimelineStore.cpp
    link/LinkHub.cpp
)
target_include_directories(wers_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(wers_core PRIVATE ${WERS_WARNINGS})
find_package(Threads REQUIRED)
target_link_libraries(wers_core PUBLIC Threads::Threads)
# The model itself is kept out of the core, so the plugin binary contains none of it.
if(WERS_WITH_WHISPER)
    add_library(wers_whisper_lib STATIC analysis/WhisperRecognizer.cpp)
    target_link_libraries(wers_whisper_lib PUBLIC wers_core PRIVATE whisper)
    target_compile_options(wers_whisper_lib PRIVATE ${WERS_WARNINGS})
endif()
