#ifndef INDEX_B_PLUS_TREE_H
#define INDEX_B_PLUS_TREE_H

#include "BPlusTreePage.h"
#include "../buffer_pool/BufferPoolManager.h"

#include <memory>
#include <limits>
#include <optional>

class BPlusTree {
public:
    enum class InsertResult {
        INSERTED,
        DUPLICATE_KEY,
        INVALID_RID,
        PAGE_ERROR
    };

    enum class FindResult {
        FOUND,
        NOT_FOUND,
        PAGE_ERROR
    };
    //删除
    enum class EraseResult {
        REMOVED,
        NOT_FOUND,
        PAGE_ERROR,
        RECLAIM_FAILED
    };
    //扫描
    enum class ScanResult {
        SUCCESS,
        INVALID_RANGE,
        PAGE_ERROR
    };

    [[nodiscard]] static std::unique_ptr<BPlusTree> create(BufferPoolManager& buffer_pool);
    [[nodiscard]] static std::unique_ptr<BPlusTree> open(BufferPoolManager& buffer_pool,
                                                       page_id_t header_page_id);

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    [[nodiscard]] InsertResult insert(IndexKey key, const RID& rid);
    [[nodiscard]] FindResult find(IndexKey key, RID& rid) const;
    [[nodiscard]] EraseResult erase(IndexKey key);
    [[nodiscard]] ScanResult scan(std::optional<IndexKey> lower,
                                  std::optional<IndexKey> upper,
                                  std::vector<BPlusTreeLeafEntry>& entries,
                                  std::size_t limit = std::numeric_limits<std::size_t>::max()) const;

    [[nodiscard]] page_id_t header_page_id() const { return header_page_id_; }
    [[nodiscard]] page_id_t root_page_id() const { return root_page_id_; }

private:
    BPlusTree(BufferPoolManager& buffer_pool, page_id_t header_page_id, page_id_t root_page_id);
    [[nodiscard]] page_id_t refresh_root() const;

    BufferPoolManager& buffer_pool_;
    page_id_t header_page_id_;
    mutable page_id_t root_page_id_;
};

#endif
