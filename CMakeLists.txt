cmake_minimum_required(VERSION 3.22)
if(APPLE)
    # one binary for Apple Silicon and Intel
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()
project(ChromaticSampler VERSION 1.0.0)

if(APPLE)
    set(CS_FORMATS AU VST3 Standalone)
    set(CS_WEBVIEW2 FALSE)
else()
    set(CS_FORMATS VST3 Standalone)
    set(CS_WEBVIEW2 TRUE)
endif()

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../repos/JUCE" CACHE PATH "Path to the JUCE checkout")
add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_plugin(ChromaticSampler
    COMPANY_NAME "Seleven Audio"
    COMPANY_WEBSITE "https://selevenaudio.com"
    PLUGIN_MANUFACTURER_CODE Slvn
    PLUGIN_CODE Chrm
    FORMATS ${CS_FORMATS}
    PRODUCT_NAME "Chromatic Sampler"
    BUNDLE_ID com.selevenaudio.chromaticsampler
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    NEEDS_WEBVIEW2 ${CS_WEBVIEW2}
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(ChromaticUI SOURCES Resources/ui/index.html)
set_target_properties(ChromaticUI PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

juce_generate_juce_header(ChromaticSampler)

target_sources(ChromaticSampler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Sampler.h
    Source/Effects.h
    Source/SampleLoader.h)

if(WIN32)
    target_compile_definitions(ChromaticSampler PUBLIC JUCE_USE_WIN_WEBVIEW2=1)
endif()

target_compile_definitions(ChromaticSampler PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# Licensing (see seleven-license/README.md). Fill in the ids from the Lemon Squeezy dashboard.
target_include_directories(ChromaticSampler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/seleven-license)
target_compile_definitions(ChromaticSampler PRIVATE
    SELEVEN_PRODUCT="Chromatic Sampler"
    SELEVEN_SLUG="chromatic"
    SELEVEN_SERVER="https://selevenaudio.com"
    SELEVEN_LS_STORE_ID=0
    SELEVEN_LS_PRODUCT_ID=0
    SELEVEN_BUY_URL="https://selevenaudio.com/product.html?id=chromatic")

target_link_libraries(ChromaticSampler PRIVATE
    ChromaticUI
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
