cmake_minimum_required(VERSION 3.22)
project(Y2KDAW VERSION 1.0.0 LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────
#  C++ standard — enforced globally before any target is defined
# ─────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────
#  Build type guard
#  On single-config generators (Makefile, Ninja) an empty build type
#  produces no optimisation flags and confuses some tooling.
# ─────────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "[Y2K] CMAKE_BUILD_TYPE not set — defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ─────────────────────────────────────────────────────────────────
#  Options
# ─────────────────────────────────────────────────────────────────
option(Y2K_BUILD_TESTS  "Build unit tests (requires Catch2)"   OFF)
option(Y2K_USE_ASAN     "Enable AddressSanitizer"              OFF)
option(Y2K_ENABLE_ML    "Enable ML features (requires ONNX)"   OFF)

# ─────────────────────────────────────────────────────────────────
#  System: Threads
#  Required by JUCE audio callbacks and our own lock-free queues.
# ─────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ─────────────────────────────────────────────────────────────────
#  JUCE acquisition
#
#  Priority order:
#   1. -DJUCE_DIR=/path/to/JUCE  (local clone, fastest, works offline)
#   2. FetchContent               (downloads ~300 MB on first configure)
#
#  To use a local JUCE:
#    cmake -B build -DJUCE_DIR=/path/to/JUCE -DCMAKE_BUILD_TYPE=Release
# ─────────────────────────────────────────────────────────────────
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE source tree (optional)")

if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "[Y2K] JUCE: using local copy at ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    message(STATUS "[Y2K] JUCE: fetching 7.0.9 from GitHub (this may take a minute)")
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.9
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ─────────────────────────────────────────────────────────────────
#  Catch2 (tests only)
# ─────────────────────────────────────────────────────────────────
if(Y2K_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# ─────────────────────────────────────────────────────────────────
#  Platform-specific flags
# ─────────────────────────────────────────────────────────────────
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0"
        CACHE STRING "Minimum macOS deployment target" FORCE)
    # Universal binary — comment out if you only need native arch
    # set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    # Silence noisy JUCE/STL warnings on MSVC
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Enable coloured diagnostics where supported
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fcolor-diagnostics)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    endif()
endif()

if(Y2K_USE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────
#  Version header
# ─────────────────────────────────────────────────────────────────
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/y2k_version.h"
    @ONLY
)

# ─────────────────────────────────────────────────────────────────
#  Install prefix default (only set when the user hasn't overridden)
# ─────────────────────────────────────────────────────────────────
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(APPLE)
        set(CMAKE_INSTALL_PREFIX "/Applications" CACHE PATH "" FORCE)
    elseif(WIN32)
        set(CMAKE_INSTALL_PREFIX "$ENV{ProgramFiles}/Y2K DAW" CACHE PATH "" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/opt/y2k-daw" CACHE PATH "" FORCE)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────
#  y2k_subdir  —  add a subdirectory only when its CMakeLists.txt exists.
#  Prevents configure failure during iterative build-out.
# ─────────────────────────────────────────────────────────────────
macro(y2k_subdir dir)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${dir}/CMakeLists.txt")
        add_subdirectory("${dir}")
    else()
        message(STATUS "[Y2K] Skipping ${dir} (no CMakeLists.txt)")
    endif()
endmacro()

# ─────────────────────────────────────────────────────────────────
#  Submodules — order matters: engine before ui before app
#
#  Build order is enforced by target_link_libraries() dependencies:
#    app  →  y2k_ui  →  y2k_dsp  →  JUCE modules
#    app  →  y2k_engine           →  JUCE core
#
#  CMake's dependency graph guarantees y2k_dsp and y2k_engine are
#  compiled before Y2KDAW, regardless of add_subdirectory order.
#  The ordering here is documentation, not enforcement.
#
#  NOTE: juce_add_gui_app(Y2KDAW) lives ONLY in app/CMakeLists.txt.
# ─────────────────────────────────────────────────────────────────
y2k_subdir(engine/y2k_dsp)
y2k_subdir(engine/y2k_engine)
y2k_subdir(ui/y2k_ui)
y2k_subdir(app)

if(Y2K_ENABLE_ML)
    y2k_subdir(ml)
endif()

if(Y2K_BUILD_TESTS)
    enable_testing()
    y2k_subdir(tests)
endif()

# ─────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  ┌──────────────────────────────────────────┐")
message(STATUS "  │  Y2K DAW  v${PROJECT_VERSION}                        │")
message(STATUS "  │  Platform : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  │  Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  │  Build    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  │  Install  : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  └──────────────────────────────────────────┘")
message(STATUS "")
