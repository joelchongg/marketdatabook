cmake_minimum_required(VERSION 3.15)

project(MarketDataBook VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set Compiler Flags
set(MarketDataBook_FLAGS 
    # "-O0" # Disable if not debugging
    "-03"
    "-march=native"
    "-fno-strict-aliasing"  # Disable strict aliasing UB
    "-Wall"
    "-Wextra"
    "-Werror"
    # "-g" # Disable if not debugging
)

# Fetch external libraries used
include(FetchContent)
FetchContent_Declare(
    abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
)
FetchContent_MakeAvailable(abseil-cpp)

add_library(market_data_core INTERFACE)
target_include_directories(market_data_core INTERFACE src)
target_link_libraries(market_data_core INTERFACE absl::flat_hash_map)
target_compile_options(market_data_core INTERFACE ${MarketDataBook_FLAGS})

# Create the main executable
add_executable(matching_engine src/main.cpp)

# Link the core library
target_link_libraries(matching_engine PRIVATE market_data_core)

# For testing
enable_testing()
add_subdirectory(tests)