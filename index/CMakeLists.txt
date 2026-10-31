cmake_minimum_required(VERSION 3.29)
project(db_test_index LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(db_test_index_storage STATIC
        ../storage/ci_pan_cun_chu.cpp
        ../storage/DiskManager.cpp
        ../buffer_pool/BufferPoolManager.cpp)

add_library(db_test_index STATIC
        RID.h
        BPlusTreePage.h
        BPlusTreePage.cpp
        BPlusTree.h
        BPlusTree.cpp)
target_include_directories(db_test_index PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(db_test_index PUBLIC db_test_index_storage)

include(CTest)
if(BUILD_TESTING)
    add_executable(b_plus_tree_test b_plus_tree_test.cpp)
    target_link_libraries(b_plus_tree_test PRIVATE db_test_index)
    if(MSVC)
        target_compile_options(b_plus_tree_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(b_plus_tree_test PRIVATE -UNDEBUG)
    endif()
    foreach(test_case IN ITEMS
            internal_round_trip leaf_round_trip leaf_rejects_invalid_data malformed_payloads
            header_round_trip node_persistence tree_insert_find
            tree_reopen tree_leaf_split tree_invalid_open
            tree_many_splits tree_shared_handles tree_internal_split
            tree_descending split_failure_rollback
            tree_pinned_pool tree_page_identity tree_corrupt_root tree_corrupt_branch randomized_model)
        add_test(NAME index_${test_case} COMMAND b_plus_tree_test ${test_case})
    endforeach()
    add_executable(b_plus_tree_operations_test b_plus_tree_operations_test.cpp)
    target_link_libraries(b_plus_tree_operations_test PRIVATE db_test_index)
    if(MSVC)
        target_compile_options(b_plus_tree_operations_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(b_plus_tree_operations_test PRIVATE -UNDEBUG)
    endif()
    foreach(test_case IN ITEMS
            scan_boundaries scan_multiple_pages scan_failure_preserves_output
            erase_basics leaf_erase_rebalance erase_reclaim_failure
            erase_failure_preserves_tree erase_all_reuse internal_erase_rebalance
            mixed_randomized_model erase_cascade_reopen operations_pinned_pool)
        add_test(NAME index_${test_case} COMMAND b_plus_tree_operations_test ${test_case})
    endforeach()
endif()
