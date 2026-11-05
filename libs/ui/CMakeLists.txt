# Look and controls of the series. The sources compile inside each plugin, with its JUCE settings.
juce_add_binary_data(WersFonts HEADER_NAME "WersFontsData.h" NAMESPACE WersFontsData SOURCES
    fonts/Bricolage-300.ttf
    fonts/Bricolage-400.ttf
    fonts/Bricolage-500.ttf
    fonts/Bricolage-600.ttf
    fonts/Bricolage-700.ttf
    fonts/Bricolage-800.ttf
    fonts/Instrument-400.ttf
    fonts/Instrument-500.ttf
    fonts/Instrument-600.ttf
)

add_library(wers_ui INTERFACE)
target_sources(wers_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Controls.cpp)
target_include_directories(wers_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui)
