cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(HumKeys VERSION 1.0.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を自動ダウンロード
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(HumKeys
    PRODUCT_NAME "Hum Keys"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.humkeys
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "マイクに歌った声や口ドラムを MIDI に変えるために使います。"
    PLIST_TO_MERGE [[
<plist><dict>
  <key>CFBundleAllowMixedLocalizations</key>
  <true/>
  <key>CFBundleDevelopmentRegion</key>
  <string>ja</string>
</dict></plist>
]])

juce_generate_juce_header(HumKeys)

target_sources(HumKeys PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MicPermission_mac.mm)
target_link_libraries(HumKeys PRIVATE "-framework AVFoundation")

target_compile_definitions(HumKeys PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HumKeys,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:HumKeys,JUCE_VERSION>")

target_link_libraries(HumKeys
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
