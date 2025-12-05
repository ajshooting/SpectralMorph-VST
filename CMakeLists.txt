cmake_minimum_required(VERSION 3.20)

project(SpectralFormantMorpher VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# JUCE Dependency
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0 # Pinning a recent version for stability
)
FetchContent_MakeAvailable(JUCE)

# -----------------------------------------------------------------------------
# Plugin Target
# -----------------------------------------------------------------------------
juce_add_plugin(SpectralFormantMorpher
    COMPANY_NAME "AudioTech"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTED TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE AuTe
    PLUGIN_CODE SpFm
    FORMATS VST3 AU
    PRODUCT_NAME "Spectral Formant Morpher"
)

target_sources(SpectralFormantMorpher
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/SpectralProcessor.cpp
        Source/DSP/SpectralProcessor.h
        Source/DSP/EnvelopeExtractor.h
        Source/DSP/FormantWarper.h
)

target_link_libraries(SpectralFormantMorpher
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------------
# Unit Tests
# -----------------------------------------------------------------------------
enable_testing()

# Create a console app for running tests
juce_add_console_app(Runner
    PRODUCT_NAME "UnitTests"
)

target_sources(Runner
    PRIVATE
        Tests/WarpingTest.cpp
        # Include DSP sources to test them directly
        Source/DSP/FormantWarper.h
)

target_link_libraries(Runner
    PRIVATE
        juce::juce_core
        juce::juce_dsp
)

add_test(NAME WarpingLogicTest COMMAND Runner)
