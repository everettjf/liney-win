# Standalone unit-test build for liney-win's platform-independent logic.
#
# Deliberately separate from the top-level CMakeLists (which is Windows-only and
# pulls in Zig + libghostty): these tests cover pure pieces (Json), so they
# configure and run on any toolchain — `cmake -S tests -B build-tests`.
cmake_minimum_required(VERSION 3.20)
project(liney_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(liney_tests
  test_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/vt/OscParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/workspace/GitStatusParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/util/Base64.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/KeyBinding.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/SshProfiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/Update.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/Ai.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/util/Json.cpp)
target_include_directories(liney_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

enable_testing()
add_test(NAME unit COMMAND liney_tests)
