add_executable(wers_cli wers_cli.cpp)
target_link_libraries(wers_cli PRIVATE wers_core)
target_compile_options(wers_cli PRIVATE ${WERS_WARNINGS})
