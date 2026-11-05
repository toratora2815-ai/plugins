cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(IdeaCatcher VERSION 1.1.1 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を自動ダウンロード
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# メニューバーだけに出るアプリ（Dock に出ない: LSUIElement）
juce_add_gui_app(IdeaCatcher
    PRODUCT_NAME "Idea Catcher"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.ideacatcher
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "思いついた鼻歌や口ドラムを録って MIDI にするために使います。"
    PLIST_TO_MERGE [[
<plist><dict>
  <key>LSUIElement</key>
  <true/>
  <key>CFBundleAllowMixedLocalizations</key>
  <true/>
  <key>CFBundleDevelopmentRegion</key>
  <string>ja</string>
</dict></plist>
]])

juce_generate_juce_header(IdeaCatcher)

target_sources(IdeaCatcher PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MacSupport_mac.mm)
target_link_libraries(IdeaCatcher PRIVATE
    "-framework AVFoundation" "-framework Carbon" "-framework ServiceManagement" "-framework UserNotifications" "-framework ApplicationServices")

target_compile_definitions(IdeaCatcher PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IdeaCatcher,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IdeaCatcher,JUCE_VERSION>")

target_link_libraries(IdeaCatcher
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
