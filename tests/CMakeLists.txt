set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 internal tests" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable Catch2 docs" FORCE)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Create the test executable
add_executable(test_hierarchical_bitset test_hierarchical_bitset.cpp)
add_executable(test_limit_order_book test_limit_order_book.cpp)

# Link Catch2 and your core library
target_link_libraries(test_hierarchical_bitset PRIVATE Catch2::Catch2WithMain market_data_core)
target_link_libraries(test_limit_order_book PRIVATE Catch2::Catch2WithMain market_data_core)

# Register tests
include(ParseAndAddCatchTests)
ParseAndAddCatchTests(test_hierarchical_bitset)
ParseAndAddCatchTests(test_limit_order_book)