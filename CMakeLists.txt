cmake_minimum_required(VERSION 3.10)
project(usdcat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mcpu=native -DNDEBUG -funroll-loops -ffast-math -flto")

# Additional warnings and optimizations
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find Eigen3
find_package(Eigen3 REQUIRED NO_MODULE)

# Find Curses
find_package(Curses REQUIRED)

# Add executable
add_executable(usdcat 
    src/main.cpp
    src/screen.cpp
    src/renderer.cpp
)

# Link libraries
target_link_libraries(usdcat 
    Eigen3::Eigen
    ${CURSES_LIBRARIES}
)

# Include directories
target_include_directories(usdcat PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CURSES_INCLUDE_DIRS}
)

# Generate dSYM file on macOS
if(APPLE)
    add_custom_command(TARGET usdcat POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:usdcat>
        COMMENT "Generating dSYM file for usdcat"
    )
endif()