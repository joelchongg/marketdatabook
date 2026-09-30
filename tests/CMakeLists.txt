set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 internal tests" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable Catch2 docs" FORCE)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

include(Catch)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
)

foreach(TEST_SOURCE IN LISTS TEST_SOURCES)
  get_filename_component(TEST_TARGET "${TEST_SOURCE}" NAME_WE)

  add_executable(${TEST_TARGET} "${TEST_SOURCE}")
  target_link_libraries(${TEST_TARGET} PRIVATE
    Catch2::Catch2WithMain
    market_data_core
  )

  catch_discover_tests(${TEST_TARGET}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()