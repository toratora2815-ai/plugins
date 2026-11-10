cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(AIOGuitar VERSION 0.3.0)

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

# AU 音源 ＋ 単体アプリ（Standalone）。ALSO_APP … build.yml が単体アプリ版（AIOGuitar-App.zip）も作る印
juce_add_plugin(AIOGuitar
    COMPANY_NAME "MyPlugins"
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE Aiog
    FORMATS AU Standalone
    PRODUCT_NAME "AIO Guitar"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The RECORD GUIDE listens to your guitar through the audio interface."
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

juce_generate_juce_header(AIOGuitar)

target_sources(AIOGuitar PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(AIOGuitar PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AIOGuitar
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# 自作プラグイン共通の部品（Plugin Link・プリセット）
target_include_directories(AIOGuitar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Shared
    # EFFECTS のペダルは単体プラグインと同じ音の部品を使う
    ${CMAKE_CURRENT_SOURCE_DIR}/../WoolFuzz/Source ${CMAKE_CURRENT_SOURCE_DIR}/../PorchDrive/Source)

# ---- オフラインの数値テスト（開発用・任意。-DAIO_BUILD_RENDER_TEST=ON） ----
option(AIO_BUILD_RENDER_TEST "Build offline render test tool" OFF)
if(AIO_BUILD_RENDER_TEST)
    juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
    juce_generate_juce_header(RenderTest)
    target_sources(RenderTest PRIVATE
        tools/RenderTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(RenderTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(RenderTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Shared
        ${CMAKE_CURRENT_SOURCE_DIR}/../WoolFuzz/Source ${CMAKE_CURRENT_SOURCE_DIR}/../PorchDrive/Source)
    target_link_libraries(RenderTest PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
