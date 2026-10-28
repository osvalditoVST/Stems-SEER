cmake_minimum_required(VERSION 3.22)
project(StemSeer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(STEMSEER_CORE_ONLY "Compilar solo el nucleo y su prueba (sin JUCE ni plugin)" OFF)

# ---------------------------------------------------------------- Nucleo (sin JUCE): BPM, escala y separacion
add_library(stemseer_core STATIC
    Source/Core/Analysis.cpp
    Source/Core/Separator.cpp)
target_include_directories(stemseer_core PUBLIC Source)
if(MSVC)
    target_compile_options(stemseer_core PRIVATE /utf-8 /O2)
endif()

add_executable(stemseer_core_test Tests/core_test.cpp)
target_link_libraries(stemseer_core_test PRIVATE stemseer_core)
if(MSVC)
    target_compile_options(stemseer_core_test PRIVATE /utf-8)
endif()

if(STEMSEER_CORE_ONLY)
    return()
endif()

# ---------------------------------------------------------------- JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(StemSeer
    COMPANY_NAME                "StemSeer"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    PLUGIN_MANUFACTURER_CODE    Stsr
    PLUGIN_CODE                 Ss01
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "StemSeer"
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(StemSeer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(StemSeer PRIVATE Source)

target_compile_definitions(StemSeer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

if(MSVC)
    target_compile_options(StemSeer PRIVATE /utf-8)
endif()

target_link_libraries(StemSeer
    PRIVATE
        stemseer_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
