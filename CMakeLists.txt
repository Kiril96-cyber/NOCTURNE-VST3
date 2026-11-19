cmake_minimum_required(VERSION 3.22)
project(NOCTURNE VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(NOCTURNE
    COMPANY_NAME "NOCTURNE Audio"
    PLUGIN_MANUFACTURER_CODE Nctn
    PLUGIN_CODE Nct1
    FORMATS VST3 Standalone
    PRODUCT_NAME "NOCTURNE"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(NOCTURNE PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(NOCTURNE PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(NOCTURNE PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp ole32 shell32)
juce_generate_juce_header(NOCTURNE)
