add_executable(wers_tests
    test_main.cpp
    test_filters.cpp
    test_monitor.cpp
    test_fonem.cpp
    test_place.cpp
    test_space.cpp
    test_analysis.cpp
    test_link.cpp
)
target_include_directories(wers_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(wers_tests PRIVATE wers_core)
target_compile_options(wers_tests PRIVATE ${WERS_WARNINGS})
add_test(NAME wers_tests COMMAND wers_tests)
