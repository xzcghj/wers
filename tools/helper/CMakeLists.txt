# The speech model runs in this separate program, so nothing it does can harm the host.
add_executable(wers_whisper HelperMain.cpp)
target_link_libraries(wers_whisper PRIVATE wers_whisper_lib wers_core)
target_compile_options(wers_whisper PRIVATE ${WERS_WARNINGS})
