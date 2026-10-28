cmake_minimum_required(VERSION 3.29)
project(db_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(db_test main.cpp
        storage/ci_pan_cun_chu.cpp
        storage/ci_pan_cun_chu.h
        storage/DiskManager.h
        storage/DiskManager.cpp
        buffer_pool/frame.h
        buffer_pool/BufferPoolManager.h
        buffer_pool/BufferPoolManager.cpp)

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
            storage/DiskManager.cpp
    )
    enable_test_assertions(disk_manager_test)
    add_test(NAME disk_manager_test COMMAND disk_manager_test)

    add_executable(buffer_pool_test
            buffer_pool/buffer_pool_test.cpp
            buffer_pool/BufferPoolManager.cpp
            storage/ci_pan_cun_chu.cpp
            storage/DiskManager.cpp)
    enable_test_assertions(buffer_pool_test)
    foreach(test_case IN ITEMS
            constructor_arguments frame_reset cache_hits_and_pins
            page_types_and_invalid_arguments all_pinned_fetch
            full_pool_new_page_noop dirty_eviction clean_eviction
            dirty_flag_is_sticky flush_pinned_page flush_all_pages
            flush_failure_preserves_dirty flush_all_continues_after_failure
            invalid_fetch_preserves_cache corrupt_fetch_preserves_cache
            failed_reads_preserve_capacity failed_eviction_preserves_victim
            failed_new_page_rolls_back delete_pinned_and_reuse
            delete_uncached_and_invalid delete_failure_preserves_cache
            reject_free_page reject_meta_page destructor_persists_pages
            destructor_continues_after_failure randomized_model)
        add_test(NAME buffer_pool_${test_case}
                COMMAND buffer_pool_test ${test_case})
    endforeach()
endif()
