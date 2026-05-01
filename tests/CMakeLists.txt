set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 internal tests" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable Catch2 docs" FORCE)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

include(ParseAndAddCatchTests)

# Glob all test files. 
# CONFIGURE_DEPENDS is mandatory to prevent stale builds when adding new test files.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "test_*.cpp")

foreach(TEST_SOURCE IN LISTS TEST_SOURCES)
  get_filename_component(TEST_TARGET ${TEST_SOURCE} NAME_WE)

  # Create the test executable
  add_executable(${TEST_TARGET} ${TEST_SOURCE})

  # Link Catch2 and your core library
  target_link_libraries(${TEST_TARGET} PRIVATE Catch2::Catch2WithMain market_data_core)

  # Register tests
  ParseAndAddCatchTests(${TEST_TARGET})
endforeach()