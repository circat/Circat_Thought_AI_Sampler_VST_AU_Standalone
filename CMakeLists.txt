# Circat Thought – JUCE Standalone MVP
cmake_minimum_required(VERSION 3.22)

project(CircatThought VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CIRCAT_BUILD_VST3 "Build the VST3 target in addition to Standalone" OFF)
option(CIRCAT_BUILD_AU "Build the AU target on macOS in addition to Standalone" OFF)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# JUCE is fetched at configure time, matching the established S612 project.
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.9
    GIT_SHALLOW TRUE
)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(juce)

set(CIRCAT_FORMATS Standalone)
if(CIRCAT_BUILD_VST3)
    list(APPEND CIRCAT_FORMATS VST3)
endif()
if(APPLE AND CIRCAT_BUILD_AU)
    list(APPEND CIRCAT_FORMATS AU)
endif()

juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME "CIRCAT"
    PRODUCT_NAME "Circat Thought"
    VERSION "${PROJECT_VERSION}"
    BUNDLE_ID media.circat.thought
    PLUGIN_MANUFACTURER_CODE CIRC
    PLUGIN_CODE CT01
    FORMATS ${CIRCAT_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler Synth
)

# Sources are added by the implementation agents. CONFIGURE_DEPENDS keeps the
# target current as new engine/UI files land in Source/.
file(GLOB_RECURSE CIRCAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)
if(NOT CIRCAT_SOURCES)
    message(FATAL_ERROR "No source files found. Add the Circat Thought sources under Source/.")
endif()
target_sources(${PROJECT_NAME} PRIVATE ${CIRCAT_SOURCES})

# Reuse the proven Akai-S612 inspired WULF input path as an optional local
# development integration. The public project must also build without it.
if(WIN32 AND EXISTS "D:/VSTPluginsDev/WULF-AD/Source/dsp/WulfAD.cpp")
    target_sources(${PROJECT_NAME} PRIVATE "D:/VSTPluginsDev/WULF-AD/Source/dsp/WulfAD.cpp")
    target_include_directories(${PROJECT_NAME} PRIVATE "D:/VSTPluginsDev/WULF-AD/Source")
    target_compile_definitions(${PROJECT_NAME} PRIVATE CIRCAT_HAS_WULF_INPUT=1)
endif()

# Shared Circat "About" tile + diagnostic log (identical in every Circat plugin).
# Canonical source: d:/VSTPluginsDev/Releases/Shared/Circat
set(CIRCAT_SHARED "D:/VSTPluginsDev/Releases/Shared/Circat")
if(EXISTS "${CIRCAT_SHARED}/CircatAbout.cmake")
    include("${CIRCAT_SHARED}/CircatAbout.cmake")
    target_link_libraries(${PROJECT_NAME} PRIVATE CircatAboutData)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CIRCAT_SHARED}"
        "${CMAKE_BINARY_DIR}/juce_binarydata_CircatAboutData/JuceLibraryCode")
    target_compile_definitions(${PROJECT_NAME} PRIVATE CIRCAT_HAS_ABOUT=1)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_IS_SYNTH=1
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

add_executable(CircatThoughtSamplerSmoke
    tests/SamplerSmoke.cpp
    Source/ThoughtSampler.cpp
)
target_include_directories(CircatThoughtSamplerSmoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)
target_link_libraries(CircatThoughtSamplerSmoke PRIVATE
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_events
    juce::juce_core
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /EHsc)
endif()

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)
    target_compile_definitions(CircatThoughtSamplerSmoke PRIVATE UNICODE _UNICODE)
endif()
