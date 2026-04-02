cmake_minimum_required(VERSION 3.15)

project(MarketDataBook VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set Compiler Flags
set(MarketDataBook_FLAGS 
    "-O3"
    "-march=native"
    "-fno-strict-aliasing"  # Disable strict aliasing UB
    "-Wall"
    "-Wextra"
    "-Werror"
)

# Fetch external libraries used
include(FetchContent)
FetchContent_Declare(
    abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
)
FetchContent_MakeAvailable(abseil-cpp)

# Create the main executable
add_executable(matching_engine src/main.cpp)

# Apply the flags to our executable
target_compile_options(matching_engine PRIVATE ${MarketDataBook_FLAGS})

# Tell CMake where to find our header files so we can use clean #include paths
target_include_directories(matching_engine PRIVATE src)
target_link_libraries(matching_engine PRIVATE absl::flat_hash_map)