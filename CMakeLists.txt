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
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(JUCE)

# -----------------------------------------------------------------------------
# Linux Dependencies (Explicit Check)
# -----------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT webkit2gtk-4.1)
    if(NOT WEBKIT_FOUND)
        pkg_check_modules(WEBKIT webkit2gtk-4.0)
    endif()
endif()

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
    FORMATS VST3 AU Standalone
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

target_compile_definitions(SpectralFormantMorpher
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SpectralFormantMorpher
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(GTK3_FOUND)
        target_include_directories(SpectralFormantMorpher PRIVATE ${GTK3_INCLUDE_DIRS})
        target_link_libraries(SpectralFormantMorpher PRIVATE ${GTK3_LIBRARIES})
    endif()
    if(WEBKIT_FOUND)
        target_include_directories(SpectralFormantMorpher PRIVATE ${WEBKIT_INCLUDE_DIRS})
        target_link_libraries(SpectralFormantMorpher PRIVATE ${WEBKIT_LIBRARIES})
    endif()
endif()

# -----------------------------------------------------------------------------
# Unit Tests
# -----------------------------------------------------------------------------
enable_testing()

juce_add_console_app(Runner
    PRODUCT_NAME "UnitTests"
)

target_sources(Runner
    PRIVATE
        Tests/WarpingTest.cpp
        Source/DSP/FormantWarper.h
)

target_link_libraries(Runner
    PRIVATE
        juce::juce_core
        juce::juce_dsp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(CURL REQUIRED)
    target_link_libraries(Runner PRIVATE CURL::libcurl)
endif()

add_test(NAME WarpingLogicTest COMMAND Runner)
