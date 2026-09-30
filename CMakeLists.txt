cmake_minimum_required(VERSION 3.15)

project(MarketDataBook VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_HW_TELEMETRY "Enable physical PMU hardware counters" OFF)
if(ENABLE_HW_TELEMETRY)
    add_compile_definitions(ENABLE_HW_TELEMETRY)
endif()

set(MARKET_DATA_BOOK_COMPILE_OPTIONS
    "-O3"
    "-march=native"
    "-fno-strict-aliasing"
    "-Wall"
    "-Wextra"
    "-Werror"
)

find_package(Threads REQUIRED)

file(GLOB_RECURSE MARKET_DATA_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER MARKET_DATA_CORE_SOURCES EXCLUDE REGEX "/main\\.cpp$")

add_library(market_data_core STATIC ${MARKET_DATA_CORE_SOURCES})
target_include_directories(market_data_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_options(market_data_core PUBLIC ${MARKET_DATA_BOOK_COMPILE_OPTIONS})
target_link_libraries(market_data_core PUBLIC Threads::Threads)

add_executable(matching_engine src/main.cpp)
target_link_libraries(matching_engine PRIVATE market_data_core)

include(CTest)
add_subdirectory(tests)