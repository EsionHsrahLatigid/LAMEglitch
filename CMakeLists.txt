cmake_minimum_required(VERSION 3.22)

project(LAMEglitch VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LAMEGLITCH_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(LAMEGLITCH_BUILD_TESTS "Build plug-in smoke tests" ON)
set(LAMEGLITCH_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH
    "Directory for final VST3, AU, and Standalone products")

if(LAMEGLITCH_BUILD_PLUGIN)
include(FetchContent)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(LAMEGLITCH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LAMEGLITCH_FORMATS AU)
endif()

juce_add_plugin(LAMEglitch
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "EsionHsrahLatigid"
    BUNDLE_ID "jp.ehl.lameglitch"
    PRODUCT_NAME "LAMEglitch"
    DESCRIPTION "Realtime MP3 encode/decode corruption effect"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE EHL_
    PLUGIN_CODE LmGl
    FORMATS ${LAMEGLITCH_FORMATS}
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
)

juce_generate_juce_header(LAMEglitch)

target_sources(LAMEglitch
    PRIVATE
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/MP3Codec.cpp
        Source/MP3Codec.h
        libs/shine-3.1.1/src/lib/bitstream.c
        libs/shine-3.1.1/src/lib/huffman.c
        libs/shine-3.1.1/src/lib/l3bitstream.c
        libs/shine-3.1.1/src/lib/l3loop.c
        libs/shine-3.1.1/src/lib/l3mdct.c
        libs/shine-3.1.1/src/lib/l3subband.c
        libs/shine-3.1.1/src/lib/layer3.c
        libs/shine-3.1.1/src/lib/reservoir.c
        libs/shine-3.1.1/src/lib/tables.c
)

target_include_directories(LAMEglitch
    PRIVATE
        Source
        libs/shine-3.1.1/src/lib
        libs/dr_libs-mp3-0.7.2
)

target_compile_definitions(LAMEglitch
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(LAMEglitch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(APPLE)
    target_compile_options(LAMEglitch PRIVATE
        -Wno-deprecated-declarations
    )
endif()

set(lameglitch_juce_products "${CMAKE_CURRENT_BINARY_DIR}/LAMEglitch_artefacts/$<CONFIG>")
set(lameglitch_staged_products "${LAMEGLITCH_ARTIFACT_DIR}/$<CONFIG>")
set(lameglitch_artifact_dependencies LAMEglitch_VST3 LAMEglitch_Standalone)
set(lameglitch_artifact_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${lameglitch_staged_products}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${lameglitch_staged_products}/VST3"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${lameglitch_staged_products}/Standalone"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${lameglitch_juce_products}/VST3" "${lameglitch_staged_products}/VST3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${lameglitch_juce_products}/Standalone" "${lameglitch_staged_products}/Standalone")

if(TARGET LAMEglitch_AU)
    list(APPEND lameglitch_artifact_dependencies LAMEglitch_AU)
    list(APPEND lameglitch_artifact_commands
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${lameglitch_staged_products}/AU"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${lameglitch_juce_products}/AU" "${lameglitch_staged_products}/AU")
endif()

if(APPLE)
    list(APPEND lameglitch_artifact_commands
        COMMAND codesign --force --deep --sign -
                "${lameglitch_staged_products}/VST3/LAMEglitch.vst3"
        COMMAND codesign --force --deep --sign -
                "${lameglitch_staged_products}/Standalone/LAMEglitch.app")
    if(TARGET LAMEglitch_AU)
        list(APPEND lameglitch_artifact_commands
            COMMAND codesign --force --deep --sign -
                    "${lameglitch_staged_products}/AU/LAMEglitch.component")
    endif()
endif()

add_custom_target(LAMEglitch_Artifacts
    ${lameglitch_artifact_commands}
    DEPENDS ${lameglitch_artifact_dependencies}
    COMMENT "Staging LAMEglitch products in ${LAMEGLITCH_ARTIFACT_DIR}/$<CONFIG>")

if(LAMEGLITCH_BUILD_TESTS)
    enable_testing()
    add_executable(LAMEglitchSmokeTests Tests/SmokeTests.cpp)
    target_include_directories(LAMEglitchSmokeTests
        PRIVATE
            Source
            "${CMAKE_CURRENT_BINARY_DIR}/LAMEglitch_artefacts/JuceLibraryCode"
            libs/shine-3.1.1/src/lib
            libs/dr_libs-mp3-0.7.2)
    target_link_libraries(LAMEglitchSmokeTests PRIVATE LAMEglitch juce::juce_audio_utils)
    add_test(NAME LAMEglitchSmokeTests COMMAND LAMEglitchSmokeTests)
endif()
endif()
