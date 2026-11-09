cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(HushField VERSION 1.2.0)

option(COPY_AFTER_BUILD "Copy plugin to system folder after build" ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を自動ダウンロード
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(HushField
    COMPANY_NAME "MyPlugins"
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE Hshf
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Hush Field"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

juce_generate_juce_header(HushField)

target_sources(HushField PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(HushField PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(HushField
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# FFT とベクトル計算（HushEngine.h が使う）
target_link_libraries(HushField PRIVATE "-framework Accelerate")

# 自作プラグイン共通の部品（Plugin Link・プリセット）
target_include_directories(HushField PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)
