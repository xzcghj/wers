# WERS Refren: doubles, crews, choirs and effects from one vocal track, with words that stay clear.
if(WERS_BUILD_TESTS)
    add_executable(refren_tests
        tests/test_main.cpp
    )
    target_include_directories(refren_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)
    target_link_libraries(refren_tests PRIVATE wers_test_support)
    target_compile_options(refren_tests PRIVATE ${WERS_WARNINGS})
    add_test(NAME refren_tests COMMAND refren_tests)
endif()
