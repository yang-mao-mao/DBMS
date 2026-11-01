cmake_minimum_required(VERSION 3.29)
project(pagecraft_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MINIDB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
    "Path to the MiniDB C++ source tree")

if(NOT EXISTS "${MINIDB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "MiniDB was not found at ${MINIDB_SOURCE_DIR}. "
        "Set -DMINIDB_SOURCE_DIR=<path-to-DBMS>.")
endif()

# MiniDB's own tests remain controlled by its root build. The bridge links the
# existing query engine and does not replace or copy any storage implementation.
set(BUILD_TESTING OFF CACHE BOOL "Build MiniDB tests" FORCE)
add_subdirectory("${MINIDB_SOURCE_DIR}" minidb-kernel EXCLUDE_FROM_ALL)

add_library(pagecraft_bridge_api STATIC
    src/bridge_api.cpp)
target_include_directories(pagecraft_bridge_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pagecraft_bridge_api PUBLIC cxx_std_20)
target_link_libraries(pagecraft_bridge_api PUBLIC minidb_query_engine)

add_executable(pagecraft_bridge
    src/main.cpp)
target_compile_features(pagecraft_bridge PRIVATE cxx_std_20)
target_link_libraries(pagecraft_bridge PRIVATE pagecraft_bridge_api)
if(WIN32)
    target_link_libraries(pagecraft_bridge PRIVATE ws2_32)
endif()

option(PAGECRAFT_BRIDGE_BUILD_TESTS "Build bridge integration tests" ON)
if(PAGECRAFT_BRIDGE_BUILD_TESTS)
    enable_testing()
    add_executable(pagecraft_bridge_test tests/bridge_api_test.cpp)
    target_compile_features(pagecraft_bridge_test PRIVATE cxx_std_20)
    target_link_libraries(pagecraft_bridge_test PRIVATE pagecraft_bridge_api)
    add_test(NAME pagecraft_bridge_api COMMAND pagecraft_bridge_test)
endif()
