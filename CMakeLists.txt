cmake_minimum_required(VERSION 3.12)
project(usdcat)

# --- Project Settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization Flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Use -mcpu on ARM64, -march on x86
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(NATIVE_OPT_FLAG "-mcpu=native")
else()
    set(NATIVE_OPT_FLAG "-march=native")
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -funroll-loops -flto ${NATIVE_OPT_FLAG}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG ${NATIVE_OPT_FLAG}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# macOS specific configuration
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.1" CACHE STRING "Minimum macOS version" FORCE)
endif()

# --- Dependencies ---

# 1. Eigen3
find_package(Eigen3 REQUIRED NO_MODULE)

# 2. OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# 3. OpenUSD (pxr)
# If pxr_DIR isn't set via command line, default to your local path
if(NOT pxr_DIR AND NOT CMAKE_PREFIX_PATH)
    set(USD_ROOT "/opt/usd")
    list(APPEND CMAKE_PREFIX_PATH "${USD_ROOT}")
endif()

# Bypass pxrConfig's exact Python version check (USD built against 3.14.2, we have 3.14.3)
set(Python3_VERSION "3.14.3")
find_package(pxr REQUIRED)

# pxrConfig bakes in the SDK used at build time; if the SDK has been updated,
# patch any imported target whose include dirs reference a now-missing SDK.
if(APPLE)
    execute_process(COMMAND xcrun --show-sdk-version OUTPUT_VARIABLE _sdk_ver OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_new_sdk "MacOSX${_sdk_ver}.sdk")
    get_cmake_property(_all_targets IMPORTED_TARGETS)
    foreach(_t IN LISTS _all_targets)
        if(NOT TARGET ${_t})
            continue()
        endif()
        get_target_property(_dirs ${_t} INTERFACE_INCLUDE_DIRECTORIES)
        if(_dirs)
            string(REGEX REPLACE "MacOSX[0-9]+\\.[0-9]+\\.sdk" "${_new_sdk}" _dirs_fixed "${_dirs}")
            if(NOT "${_dirs_fixed}" STREQUAL "${_dirs}")
                set_property(TARGET ${_t} PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_dirs_fixed}")
            endif()
        endif()
    endforeach()
endif()

# --- Executable ---

add_executable(usdcat 
    src/main.cpp
    src/camera_controller.cpp
    src/screen.cpp
    src/renderer.cpp
    src/mesh.cpp
    src/material.cpp
    src/delegate.cpp
    src/render_pass.cpp
    src/frame_timer.cpp
)

# --- Linking & Includes ---

target_include_directories(usdcat PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${PXR_INCLUDE_DIRS}
)

# Use PRIVATE for libraries to prevent leaking internal dependencies
target_link_libraries(usdcat PRIVATE 
    Eigen3::Eigen
    ${PXR_LIBRARIES}
    OpenGL::GL
)

# macOS Post-Build
if(APPLE)
    add_custom_command(TARGET usdcat POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:usdcat>
        COMMENT "Generating dSYM file for usdcat"
    )
endif()
