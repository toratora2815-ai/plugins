cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(SoundSketch VERSION 1.0.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を自動ダウンロード
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(SoundSketch
    PRODUCT_NAME "Sound Sketch"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.soundsketch
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "口で言った音（口マネ）を聞いて、似たサンプルを探すために使います。録った声は保存しません。"
    PLIST_TO_MERGE [[
<plist><dict>
  <key>CFBundleAllowMixedLocalizations</key>
  <true/>
  <key>CFBundleDevelopmentRegion</key>
  <string>ja</string>
</dict></plist>
]])

juce_generate_juce_header(SoundSketch)

target_sources(SoundSketch PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MacSupport_mac.mm)
target_link_libraries(SoundSketch PRIVATE "-framework AVFoundation")

target_compile_definitions(SoundSketch PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SoundSketch,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SoundSketch,JUCE_VERSION>")

target_link_libraries(SoundSketch
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
