cmake_minimum_required(VERSION 3.22)
# Logic・ミュージックの音を直接受け取る仕組み（プロセスタップ）が macOS 14.2 からなので、このアプリだけ 14.2
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "")
project(ReferenceMonitor VERSION 1.4.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を自動ダウンロード
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(ReferenceMonitor
    PRODUCT_NAME "Reference Monitor"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.referencemonitor
    PLIST_TO_MERGE [[
<plist><dict>
  <key>NSAudioCaptureUsageDescription</key>
  <string>Logic Pro とミュージックの音を受け取って聴き比べるために使います。</string>
  <key>NSAppleEventsUsageDescription</key>
  <string>ミュージックの再生位置をキューに移動するために使います。</string>
</dict></plist>
]])

juce_generate_juce_header(ReferenceMonitor)

target_sources(ReferenceMonitor PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Engine.cpp)

if(APPLE)
    target_sources(ReferenceMonitor PRIVATE
        Source/Capture_mac.mm
        Source/MusicControl_mac.mm
        Source/RemoteCommands_mac.mm)
    target_link_libraries(ReferenceMonitor PRIVATE "-framework CoreAudio" "-framework Foundation" "-framework AppKit")
else()
    target_sources(ReferenceMonitor PRIVATE Source/Stubs_other.cpp)
endif()

target_compile_definitions(ReferenceMonitor PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ReferenceMonitor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ReferenceMonitor,JUCE_VERSION>")

target_link_libraries(ReferenceMonitor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
