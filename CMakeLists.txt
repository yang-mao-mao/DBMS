cmake_minimum_required(VERSION 3.29)
project(db_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(db_test main.cpp
        storage/ci_pan_cun_chu.cpp
        storage/ci_pan_cun_chu.h
        storage/DiskManager.h
        storage/DiskManager.cpp)

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
            storage/page_test.cpp
            storage/ci_pan_cun_chu.cpp
            storage/ci_pan_cun_chu.h
            storage/DiskManager.h
            storage/DiskManager.cpp)
    enable_test_assertions(page_test)
    add_test(NAME page_test COMMAND page_test)

    add_executable(disk_page_test
            storage/disk_page_test.cpp
            storage/ci_pan_cun_chu.cpp
            storage/ci_pan_cun_chu.h
            storage/DiskManager.h
            storage/DiskManager.cpp)
    enable_test_assertions(disk_page_test)
    add_test(NAME disk_page_test COMMAND disk_page_test)

    add_executable(disk_manager_test
            storage/disk_manager_test.cpp
            storage/ci_pan_cun_chu.cpp
            storage/ci_pan_cun_chu.h
            storage/DiskManager.h
            storage/DiskManager.cpp)
    enable_test_assertions(disk_manager_test)
    add_test(NAME disk_manager_test COMMAND disk_manager_test)
endif()
