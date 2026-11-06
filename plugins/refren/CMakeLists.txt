# WERS Refren: doubles, crews, choirs and effects from one vocal track, with words that stay clear.
add_library(refren_dsp STATIC
    dsp/VoiceAnalysis.cpp
)
target_include_directories(refren_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(refren_dsp PUBLIC wers_dsp)
target_compile_options(refren_dsp PRIVATE ${WERS_WARNINGS})

if(WERS_BUILD_TESTS)
    add_executable(refren_tests
        tests/test_main.cpp
        tests/test_pitch.cpp
    )
    target_include_directories(refren_tests PRIVATE ${doctest_SOURCE_DIR}/doctest ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(refren_tests PRIVATE refren_dsp wers_test_support)
    target_compile_options(refren_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME refren_tests COMMAND refren_tests)
endif()
