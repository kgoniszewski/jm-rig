cmake_minimum_required(VERSION 3.22)

# Apple targets: one universal macOS binary, ARM64 only on iPadOS.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0" CACHE STRING "Minimum iPadOS version")
endif()

project(JMRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(JMRIG_BUILD_TESTS "Build the headless DSP test runner" ON)

add_subdirectory(libs/JUCE)

#==============================================================================
# Headless DSP engine: no GUI, no plugin wrapper, no APVTS. Everything that runs
# on the audio thread lives here so it can be tested and profiled on its own.
# It is an INTERFACE library because JUCE modules are compiled into each final
# target; a static library would compile them twice and clash at link time.
add_library(jmrig_dsp INTERFACE)

target_sources(jmrig_dsp
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/RigEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/AmpStage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/CabStage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/amp/PowerAmp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/amp/Interstage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/amp/Preamp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/amp/ToneStack.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/dsp/amp/TriodeStage.cpp)

target_include_directories(jmrig_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(jmrig_dsp INTERFACE juce::juce_dsp)

#==============================================================================
# Plugin: AU + AUv3 + Standalone on Apple platforms (AUv3 needs the Xcode
# generator). Elsewhere only the Standalone builds, as a compile check.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(JMRIG_FORMATS AUv3 Standalone)
elseif(APPLE)
    set(JMRIG_FORMATS AU AUv3 Standalone)
else()
    set(JMRIG_FORMATS Standalone)
endif()

juce_add_plugin(JMRig
    COMPANY_NAME "Krzysztof Goniszewski"
    BUNDLE_ID "com.kgoniszewski.jmrig"
    PRODUCT_NAME "JM Rig"
    PLUGIN_NAME "JM Rig"
    DESCRIPTION "Circuit-modelled guitar amp and cab"
    PLUGIN_MANUFACTURER_CODE Kgon
    PLUGIN_CODE Jmrg
    FORMATS ${JMRIG_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    # iPadOS standalone app: guitar comes in through the audio interface.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "JM Rig needs audio input to process your guitar."
    BACKGROUND_AUDIO_ENABLED TRUE
    REQUIRES_FULL_SCREEN FALSE
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    TARGETED_DEVICE_FAMILY 2
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(JMRig
    PRIVATE
        source/plugin/ParameterLayout.cpp
        source/plugin/PluginProcessor.cpp
        source/plugin/PluginEditor.cpp
        source/plugin/RigLookAndFeel.cpp)

target_compile_definitions(JMRig
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(JMRig
    PRIVATE
        jmrig_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
if(JMRIG_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_console_app(JMRigTests PRODUCT_NAME "JMRigTests")
    target_sources(JMRigTests PRIVATE tests/EngineTests.cpp)
    target_compile_definitions(JMRigTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(JMRigTests
        PRIVATE
            jmrig_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Null tests against SPICE; reference data from tools/spice/generate.py.
    juce_add_console_app(JMRigSpiceTests PRODUCT_NAME "JMRigSpiceTests")
    target_sources(JMRigSpiceTests PRIVATE tests/SpiceNullTests.cpp)
    target_compile_definitions(JMRigSpiceTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JMRIG_SPICE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/spice")
    target_link_libraries(JMRigSpiceTests
        PRIVATE
            jmrig_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME engine COMMAND JMRigTests)
    add_test(NAME spice-null COMMAND JMRigSpiceTests)
endif()
