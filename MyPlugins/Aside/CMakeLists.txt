cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(Aside VERSION 1.1.0 LANGUAGES C CXX OBJCXX)

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
juce_add_gui_app(Aside
    PRODUCT_NAME "Aside"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.aside
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ひとりごとを聞いて、やることをリマインダーに入れるために使います。声は保存せず、Mac の中だけで文字にします。"
    PLIST_TO_MERGE [[
<plist><dict>
  <key>LSUIElement</key>
  <true/>
  <key>NSSpeechRecognitionUsageDescription</key>
  <string>ひとりごとを文字にするために使います。Mac の中だけで処理し、外には送りません。</string>
  <key>NSRemindersUsageDescription</key>
  <string>聞き取ったやることを、リマインダーに入れるために使います。</string>
  <key>NSRemindersFullAccessUsageDescription</key>
  <string>聞き取ったやることを、リマインダーに入れる（取り消すときは消す）ために使います。</string>
  <key>NSCalendarsUsageDescription</key>
  <string>時刻のある打ち合わせなどを、カレンダーに入れるために使います。</string>
  <key>NSCalendarsFullAccessUsageDescription</key>
  <string>時刻のある打ち合わせなどを、カレンダーに入れる（取り消すときは消す）ために使います。</string>
  <key>CFBundleAllowMixedLocalizations</key>
  <true/>
  <key>CFBundleDevelopmentRegion</key>
  <string>ja</string>
</dict></plist>
]])

juce_generate_juce_header(Aside)

target_sources(Aside PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AsideMac_mac.mm)
target_link_libraries(Aside PRIVATE
    "-framework AppKit" "-framework AVFoundation" "-framework Speech" "-framework EventKit"
    "-framework Carbon" "-framework ServiceManagement")

target_compile_definitions(Aside PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Aside,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Aside,JUCE_VERSION>")

target_link_libraries(Aside
    PRIVATE
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
