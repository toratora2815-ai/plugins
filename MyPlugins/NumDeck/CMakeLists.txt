cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(NumDeck VERSION 1.9.0 LANGUAGES C CXX)
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

juce_add_gui_app(NumDeck
    PRODUCT_NAME "Num Deck"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.numdeck
    PLIST_TO_MERGE [[
<plist><dict>
  <key>NSAppleEventsUsageDescription</key>
  <string>Chrome で今開いているページを、テンキーのタイルに登録するために使います。</string>
  <key>CFBundleAllowMixedLocalizations</key>
  <true/>
  <key>CFBundleDevelopmentRegion</key>
  <string>ja</string>
</dict></plist>
]])

juce_generate_juce_header(NumDeck)

target_sources(NumDeck PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp)

if(APPLE)
    # テンキーの横取り（CGEventTap）と、Logic へのショートカット送信
    target_sources(NumDeck PRIVATE Source/KeyTap_mac.mm)
    target_link_libraries(NumDeck PRIVATE "-framework ApplicationServices" "-framework AppKit")
else()
    target_sources(NumDeck PRIVATE Source/KeyTap_other.cpp)
endif()

target_compile_definitions(NumDeck PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NumDeck,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NumDeck,JUCE_VERSION>")

target_link_libraries(NumDeck
    PRIVATE
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
