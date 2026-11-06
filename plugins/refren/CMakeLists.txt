# WERS Refren: doubles, crews, choirs and effects from one vocal track, with words that stay clear.
add_library(refren_dsp STATIC
    dsp/Layer.cpp
    dsp/Psola.cpp
    dsp/RefrenEngine.cpp
    dsp/SubVoice.cpp
    dsp/VoiceAnalysis.cpp
    dsp/VoiceMask.cpp
    dsp/WhisperVoice.cpp
)
target_include_directories(refren_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(refren_dsp PUBLIC wers_dsp)
target_compile_options(refren_dsp PRIVATE ${WERS_WARNINGS})

# Listening samples from the user's own vocals, on the user's computer.
add_library(refren_tools_lib STATIC tools/RenderProbes.cpp)
target_include_directories(refren_tools_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(refren_tools_lib PUBLIC refren_dsp wers_test_support)
target_compile_options(refren_tools_lib PRIVATE ${WERS_WARNINGS})
add_executable(refren_render tools/refren_render.cpp)
target_link_libraries(refren_render PRIVATE refren_tools_lib)
target_compile_options(refren_render PRIVATE ${WERS_WARNINGS})

if(WERS_BUILD_TESTS)
    add_executable(refren_tests
        tests/test_main.cpp
        tests/test_pitch.cpp
        tests/test_psola.cpp
        tests/test_sources.cpp
        tests/test_layers.cpp
    )
    target_include_directories(refren_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(refren_tests PRIVATE refren_dsp refren_tools_lib wers_test_support)
    target_compile_options(refren_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME refren_tests COMMAND refren_tests)
endif()
