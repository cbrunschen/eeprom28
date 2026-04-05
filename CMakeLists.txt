cmake_minimum_required(VERSION 3.10.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(CXX_STANDARD 20)

project(X28C VERSION 0.1.0 LANGUAGES C CXX)

# eeprom28 is header-only; otherwise there would have been something like:
# add_library(eeprom28 eeprom28.cpp)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.13.0 # or a later release
)

FetchContent_MakeAvailable(Catch2)

add_executable(tests test.cpp env.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(tests)

