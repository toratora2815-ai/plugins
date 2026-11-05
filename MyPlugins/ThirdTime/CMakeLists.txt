cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(ThirdTime VERSION 1.0.0 LANGUAGES C CXX OBJCXX)

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
juce_add_gui_app(ThirdTime
    PRODUCT_NAME "Third Time"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.thirdtime
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

juce_generate_juce_header(ThirdTime)

target_sources(ThirdTime PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ThirdMac_mac.mm)
target_link_libraries(ThirdTime PRIVATE
    "-framework AppKit" "-framework ApplicationServices" "-framework Carbon" "-framework ServiceManagement")

target_compile_definitions(ThirdTime PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ThirdTime,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ThirdTime,JUCE_VERSION>")

target_link_libraries(ThirdTime
    PRIVATE
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
