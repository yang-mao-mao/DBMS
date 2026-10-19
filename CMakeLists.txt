cmake_minimum_required(VERSION 3.29)
project(db_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(minidb_storage STATIC
        storage/ci_pan_cun_chu.cpp
        storage/ci_pan_cun_chu.h
        storage/DiskManager.h
        storage/DiskManager.cpp)
target_include_directories(minidb_storage PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/storage)
target_compile_features(minidb_storage PUBLIC cxx_std_20)

add_library(minidb_catalog_storage STATIC
        src/catalog/catalog.cpp
        src/storage/row_codec.cpp
        src/storage/real_storage_adapter.cpp)
target_include_directories(minidb_catalog_storage PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(minidb_catalog_storage PUBLIC cxx_std_20)
target_link_libraries(minidb_catalog_storage PUBLIC minidb_storage)

add_library(minidb_sql STATIC
        src/sql/lexer.cpp
        src/sql/parser.cpp)
target_include_directories(minidb_sql PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(minidb_sql PUBLIC cxx_std_20)

add_executable(db_test main.cpp)
target_link_libraries(db_test PRIVATE minidb_storage)

include(CTest)
if(BUILD_TESTING)
    function(enable_test_assertions target)
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(page_test
            storage/page_test.cpp)
    target_link_libraries(page_test PRIVATE minidb_storage)
    enable_test_assertions(page_test)
    add_test(NAME page_test COMMAND page_test)

    add_executable(disk_page_test
            storage/disk_page_test.cpp)
    target_link_libraries(disk_page_test PRIVATE minidb_storage)
    enable_test_assertions(disk_page_test)
    add_test(NAME disk_page_test COMMAND disk_page_test)

    add_executable(disk_manager_test
            storage/disk_manager_test.cpp)
    target_link_libraries(disk_manager_test PRIVATE minidb_storage)
    enable_test_assertions(disk_manager_test)
    add_test(NAME disk_manager_test COMMAND disk_manager_test)

    add_executable(row_codec_test tests/row_codec_test.cpp)
    target_link_libraries(row_codec_test PRIVATE minidb_catalog_storage)
    enable_test_assertions(row_codec_test)
    add_test(NAME row_codec_test COMMAND row_codec_test)

    add_executable(catalog_storage_test tests/catalog_storage_test.cpp)
    target_link_libraries(catalog_storage_test PRIVATE minidb_catalog_storage)
    enable_test_assertions(catalog_storage_test)
    add_test(NAME catalog_storage_test COMMAND catalog_storage_test)

    add_executable(sql_lexer_test tests/sql_lexer_test.cpp)
    target_link_libraries(sql_lexer_test PRIVATE minidb_sql)
    enable_test_assertions(sql_lexer_test)
    add_test(NAME sql_lexer_test COMMAND sql_lexer_test)

    add_executable(sql_parser_test tests/sql_parser_test.cpp)
    target_link_libraries(sql_parser_test PRIVATE minidb_sql)
    enable_test_assertions(sql_parser_test)
    add_test(NAME sql_parser_test COMMAND sql_parser_test)
endif()
