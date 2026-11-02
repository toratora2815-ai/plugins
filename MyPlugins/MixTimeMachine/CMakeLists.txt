cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(MixTimeMachine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を自動ダウンロード
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(MixTimeMachine
    PRODUCT_NAME "Mix Time Machine"
    COMPANY_NAME "MyPlugins"
    BUNDLE_ID com.myplugins.mixtimemachine)

juce_generate_juce_header(MixTimeMachine)

target_sources(MixTimeMachine PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp)

target_compile_definitions(MixTimeMachine PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MixTimeMachine,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MixTimeMachine,JUCE_VERSION>")

target_link_libraries(MixTimeMachine
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
