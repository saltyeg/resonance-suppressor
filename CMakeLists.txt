cmake_minimum_required(VERSION 3.25)

project(ResonanceSuppressor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Dev install folder; add it to FL Studio's plugin search paths once.
set(RS_VST3_DIR "$ENV{USERPROFILE}/VST3-dev" CACHE PATH "Where built VST3 bundles are copied")

# ---------------------------------------------------------------- DSP core
# Plain C++ library shared by the plugin and the command-line tools.
add_library(rs_dsp STATIC
    src/dsp/Placeholder.cpp)
target_include_directories(rs_dsp PUBLIC src)
target_link_libraries(rs_dsp PUBLIC juce::juce_dsp)
target_compile_definitions(rs_dsp PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# ---------------------------------------------------------------- plugin
juce_add_plugin(ResonanceSuppressor
    COMPANY_NAME "mikey"
    PLUGIN_MANUFACTURER_CODE Mkey
    PLUGIN_CODE RsSp
    PRODUCT_NAME "resonance-suppressor"
    FORMATS VST3
    VST3_CATEGORIES Fx Dynamics EQ
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${RS_VST3_DIR}")

target_sources(ResonanceSuppressor PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp)

target_compile_definitions(ResonanceSuppressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ResonanceSuppressor
    PRIVATE
        rs_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- headless host
# rshost: loads any VST3 (ours or a reference plugin) and renders WAV files offline.
juce_add_console_app(rshost PRODUCT_NAME "rshost")
target_sources(rshost PRIVATE tools/rshost/Main.cpp)
target_compile_definitions(rshost PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(rshost
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
