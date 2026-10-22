cmake_minimum_required(VERSION 3.22)

project(TesseraEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESSERA_BUILD_TESTS "Build the offline DSP test runner" ON)
set(TESSERA_FORMATS "VST3" CACHE STRING "Plugin formats to build (e.g. VST3;Standalone)")

# --- JUCE --------------------------------------------------------------------
# Uses a local checkout if JUCE_DIR is given, otherwise downloads JUCE 8.0.4.
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Shared DSP/UI code ------------------------------------------------------
set(TESSERA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LinearPhaseEngine.cpp
    Source/UI/GraphComponent.cpp
    Source/UI/BandStrip.cpp
    Source/UI/MeterPanel.cpp
    Source/UI/TesseraLookAndFeel.cpp)

juce_add_plugin(TesseraEQ
    COMPANY_NAME "8cee"
    BUNDLE_ID "com.8cee.tesseraeq"
    PLUGIN_MANUFACTURER_CODE Ecee
    PLUGIN_CODE Tsq1
    FORMATS ${TESSERA_FORMATS}
    PRODUCT_NAME "Tessera EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TesseraEQ PRIVATE ${TESSERA_SOURCES})
target_include_directories(TesseraEQ PRIVATE Source)

target_compile_definitions(TesseraEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TesseraEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Offline test runner -----------------------------------------------------
if(TESSERA_BUILD_TESTS)
    juce_add_console_app(TesseraTests PRODUCT_NAME "TesseraTests")
    target_sources(TesseraTests PRIVATE Tests/TestMain.cpp ${TESSERA_SOURCES})
    target_include_directories(TesseraTests PRIVATE Source)
    target_compile_definitions(TesseraTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Tessera EQ")
    target_link_libraries(TesseraTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

