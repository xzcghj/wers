# WERS Barwa: a saturator that gives colour to vowels and leaves sibilants clean.
add_library(barwa_dsp STATIC
    dsp/CharacterShaper.cpp
)
target_include_directories(barwa_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(barwa_dsp PUBLIC wers_dsp)
target_compile_options(barwa_dsp PRIVATE ${WERS_WARNINGS})

if(WERS_BUILD_TESTS)
    add_executable(barwa_tests
        tests/test_main.cpp
        tests/test_character.cpp
    )
    target_include_directories(barwa_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)
    target_link_libraries(barwa_tests PRIVATE barwa_dsp wers_test_support)
    target_compile_options(barwa_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME barwa_tests COMMAND barwa_tests)
endif()
