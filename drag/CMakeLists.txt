cmake_minimum_required(VERSION 3.22)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()
# Static MSVC runtime: no VC++ Redistributable needed on a fresh PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(DragSampler VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(DS_FORMATS AU VST3 Standalone)
else()
    set(DS_FORMATS VST3 Standalone)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DragSampler
    COMPANY_NAME "Seleven Audio"
    PLUGIN_MANUFACTURER_CODE Slvn
    PLUGIN_CODE DrSm
    FORMATS ${DS_FORMATS}
    PRODUCT_NAME "Drag Sampler"
    PLUGIN_NAME "Drag Sampler"
    DESCRIPTION "Invisible always-on recorder"
    VST3_CATEGORIES Fx Tools
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DragSampler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DiskRecorder.cpp)

target_compile_definitions(DragSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Licensing (see seleven-license/README.md). Fill in the ids from the Lemon Squeezy dashboard.
target_include_directories(DragSampler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/seleven-license)
target_compile_definitions(DragSampler PRIVATE
    SELEVEN_PRODUCT="Drag Sampler"
    SELEVEN_SLUG="drag"
    SELEVEN_SERVER="https://selevenaudio.com"
    SELEVEN_LS_STORE_ID=0
    SELEVEN_LS_PRODUCT_ID=0
    SELEVEN_BUY_URL="https://selevenaudio.com/product.html?id=drag")

target_link_libraries(DragSampler PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_opengl
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
