# Test signals, measurements and the WAV reader of the offline tools.
add_library(wers_test_support INTERFACE)
target_include_directories(wers_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wers_test_support INTERFACE wers_dsp)
