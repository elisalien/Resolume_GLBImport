cmake_minimum_required( VERSION 3.20 )

# Universal build has to be declared before project(), and only on Apple.
if( APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES )
    set( CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build a universal plugin" )
endif()
if( APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET )
    set( CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" )
endif()

project( GlbSource VERSION 1.0 LANGUAGES CXX C )

# ---------------------------------------------------------------------------
# Resolume FFGL SDK
#
# By default the SDK is downloaded next to this project. Point FFGL_SDK_DIR at
# an existing checkout to build offline:
#     cmake -B build -DFFGL_SDK_DIR=/path/to/ffgl
# ---------------------------------------------------------------------------
set( FFGL_SDK_DIR "" CACHE PATH "Path to an existing resolume/ffgl checkout (leave empty to download)" )

if( FFGL_SDK_DIR STREQUAL "" )
    include( FetchContent )
    # SOURCE_SUBDIR points at a directory with no CMakeLists.txt on purpose:
    # that makes FetchContent download the sources without configuring the SDK's
    # own targets, which would drag vcpkg in just to find glew.
    FetchContent_Declare(
        ffgl
        GIT_REPOSITORY https://github.com/resolume/ffgl.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  sources-only-no-cmake
    )
    FetchContent_MakeAvailable( ffgl )
    set( FFGL_SDK_DIR "${ffgl_SOURCE_DIR}" )
endif()

if( NOT EXISTS "${FFGL_SDK_DIR}/source/lib/FFGLSDK.cpp" )
    message( FATAL_ERROR "FFGL SDK not found at ${FFGL_SDK_DIR}" )
endif()
message( STATUS "FFGL SDK: ${FFGL_SDK_DIR}" )

# ---------------------------------------------------------------------------
# The plugin. MODULE, because a host loads it rather than links against it:
# that gives a .dll on Windows and a loadable .bundle on macOS.
# ---------------------------------------------------------------------------
add_library( GlbSource MODULE
    src/GlbSource.cpp
    src/GlbSourceRender.cpp
    src/GltfModel.cpp
    src/GlbSource.h
    src/GltfModel.h
    src/GLHeaders.h
    src/Math3D.h
    src/Shaders.h
    src/ParamIds.h
    src/ColorUtils.h

    # FFGL SDK, single translation unit
    "${FFGL_SDK_DIR}/source/lib/FFGLSDK.cpp"

    # meshoptimizer, only the parts the decimation path needs
    third_party/meshoptimizer/simplifier.cpp
    third_party/meshoptimizer/indexgenerator.cpp
    third_party/meshoptimizer/vcacheoptimizer.cpp
    third_party/meshoptimizer/vfetchoptimizer.cpp
    third_party/meshoptimizer/allocator.cpp
    third_party/meshoptimizer/quantization.cpp
)

set_target_properties( GlbSource PROPERTIES
    OUTPUT_NAME "GlbSource"
    PREFIX ""
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)

target_include_directories( GlbSource PRIVATE
    src
    third_party
    third_party/meshoptimizer
    "${FFGL_SDK_DIR}/source/lib"
)

# ---------------------------------------------------------------------------
# Platform specifics
# ---------------------------------------------------------------------------
if( WIN32 )
    find_package( OpenGL REQUIRED )

    target_include_directories( GlbSource PRIVATE "${FFGL_SDK_DIR}/deps/glew-2.1.0/include" )
    target_compile_definitions( GlbSource PRIVATE
        GLEW_STATIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries( GlbSource PRIVATE
        "${FFGL_SDK_DIR}/deps/glew-2.1.0/lib/Release/x64/glew32s.lib"
        OpenGL::GL
    )
    if( MSVC )
        # plugMain is already __declspec(dllexport), the .def keeps the name clean.
        target_link_options( GlbSource PRIVATE "/DEF:${FFGL_SDK_DIR}/build/windows/FFGLPlugins.def" )
        target_compile_options( GlbSource PRIVATE /MP /W3 )
        # Resolume's own plugins use the dynamic CRT; the bundled glew is built the same way.
        set_property( TARGET GlbSource PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" )
    endif()
    set_target_properties( GlbSource PROPERTIES SUFFIX ".dll" )

elseif( APPLE )
    # No loader needed: Apple's OpenGL framework exposes 4.1 core directly, and
    # every entry point this plugin uses is within that ceiling.
    set_target_properties( GlbSource PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist"
    )
    target_compile_definitions( GlbSource PRIVATE GL_SILENCE_DEPRECATION )
    target_compile_options( GlbSource PRIVATE -Wno-deprecated-declarations )
    target_link_libraries( GlbSource PRIVATE
        "-framework OpenGL"
        "-framework Carbon"
        "-framework AppKit"
    )

else()
    find_package( OpenGL REQUIRED )
    find_package( GLEW REQUIRED )
    target_link_libraries( GlbSource PRIVATE GLEW::GLEW OpenGL::GL )
endif()

# ---------------------------------------------------------------------------
# Copy the built plugin straight into Resolume's plugin folder.
#     cmake --build build --config Release --target install-plugin
# ---------------------------------------------------------------------------
if( WIN32 )
    set( RESOLUME_PLUGIN_DIR "$ENV{USERPROFILE}/Documents/Resolume Arena/Extra Effects"
         CACHE PATH "Where Resolume looks for extra FFGL plugins" )
else()
    set( RESOLUME_PLUGIN_DIR "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
         CACHE PATH "Where Resolume looks for extra FFGL plugins" )
endif()

if( APPLE )
    # A bundle is a directory, so it has to be copied as a tree.
    add_custom_target( install-plugin
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOLUME_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${RESOLUME_PLUGIN_DIR}/GlbSource.bundle"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                $<TARGET_BUNDLE_DIR:GlbSource> "${RESOLUME_PLUGIN_DIR}/GlbSource.bundle"
        DEPENDS GlbSource
        COMMENT "Copying GlbSource.bundle to ${RESOLUME_PLUGIN_DIR}"
    )
else()
    add_custom_target( install-plugin
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOLUME_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:GlbSource> "${RESOLUME_PLUGIN_DIR}"
        DEPENDS GlbSource
        COMMENT "Copying GlbSource to ${RESOLUME_PLUGIN_DIR}"
    )
endif()
