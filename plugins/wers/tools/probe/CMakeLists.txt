# Diagnostic plugin: logs the project time a host reports next to where clicks appear in the audio.
# It answers whether the host adjusts the reported time for the latency of earlier plugins.
juce_add_plugin(WersProbe
    COMPANY_NAME "WERS"
    BUNDLE_ID "app.wers.probe"
    PLUGIN_MANUFACTURER_CODE Wers
    PLUGIN_CODE Wprb
    FORMATS VST3
    PRODUCT_NAME "WERS Probe"
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer
    VERSION ${PROJECT_VERSION})
target_sources(WersProbe PRIVATE Probe.cpp)
target_compile_definitions(WersProbe PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0)
target_link_libraries(WersProbe PRIVATE juce::juce_audio_processors PUBLIC juce::juce_recommended_config_flags)
