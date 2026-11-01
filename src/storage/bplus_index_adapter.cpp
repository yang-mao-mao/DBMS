#include "minidb/storage/bplus_index_adapter.h"

#include "BPlusTree.h"
#include "BufferPoolManager.h"
#include "DiskManager.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace minidb {
namespace {

constexpr std::size_t INDEX_BUFFER_POOL_SIZE = 16;

} // namespace

struct BPlusIndexAdapter::Impl {
    std::unique_ptr<BufferPoolManager> buffer_pool;
    std::unique_ptr<BPlusTree> tree;
};

BPlusIndexAdapter::BPlusIndexAdapter(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

BPlusIndexAdapter::~BPlusIndexAdapter() = default;

Result<std::unique_ptr<BPlusIndexAdapter>> BPlusIndexAdapter::create(
    const std::filesystem::path& file) {
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        if (error) {
            return Status::io_error("failed to inspect index file: " + error.message());
        }
        return Status::already_exists("index file already exists");
    }
    auto disk = DiskManager::open(file.string());
    if (!disk) return Status::io_error("failed to create index file");
    auto impl = std::make_unique<Impl>();
    impl->buffer_pool = std::make_unique<BufferPoolManager>(
        INDEX_BUFFER_POOL_SIZE, std::move(disk));
    impl->tree = BPlusTree::create(*impl->buffer_pool);
    if (!impl->tree) return Status::io_error("failed to initialize B+ tree");
    return std::unique_ptr<BPlusIndexAdapter>(
        new BPlusIndexAdapter(std::move(impl)));
}

Result<std::unique_ptr<BPlusIndexAdapter>> BPlusIndexAdapter::open(
    const std::filesystem::path& file, std::uint32_t header_page_id) {
    if (header_page_id == 0) {
        return Status::corruption("index metadata has no B+ tree header page");
    }
    std::error_code error;
    if (!std::filesystem::exists(file, error) || error) {
        return error
            ? Result<std::unique_ptr<BPlusIndexAdapter>>(Status::io_error(
                  "failed to inspect index file: " + error.message()))
            : Result<std::unique_ptr<BPlusIndexAdapter>>(Status::not_found(
                  "index file does not exist"));
    }
    auto disk = DiskManager::open(file.string());
    if (!disk) return Status::corruption("failed to open index file");
    auto impl = std::make_unique<Impl>();
    impl->buffer_pool = std::make_unique<BufferPoolManager>(
        INDEX_BUFFER_POOL_SIZE, std::move(disk));
    impl->tree = BPlusTree::open(*impl->buffer_pool, header_page_id);
    if (!impl->tree) return Status::corruption("index B+ tree header is invalid");
    return std::unique_ptr<BPlusIndexAdapter>(
        new BPlusIndexAdapter(std::move(impl)));
}

Status BPlusIndexAdapter::insert(std::int64_t key, RID rid) {
    const ::RID physical{rid.page_id, rid.slot_id};
    switch (impl_->tree->insert(key, physical)) {
        case BPlusTree::InsertResult::INSERTED:
            return Status::ok_status();
        case BPlusTree::InsertResult::DUPLICATE_KEY:
            return Status::already_exists(
                "B+ tree unique index contains a duplicate key");
        case BPlusTree::InsertResult::INVALID_RID:
            return Status::invalid_argument("B+ tree rejected an invalid RID");
        case BPlusTree::InsertResult::PAGE_ERROR:
            return Status::io_error("B+ tree failed to persist an index entry");
    }
    return Status::internal_error("unknown B+ tree insertion result");
}

Status BPlusIndexAdapter::erase(std::int64_t key, RID expected_rid) {
    ::RID physical{};
    switch (impl_->tree->find(key, physical)) {
        case BPlusTree::FindResult::NOT_FOUND:
            return Status::corruption("B+ tree index is missing a row key");
        case BPlusTree::FindResult::PAGE_ERROR:
            return Status::io_error("B+ tree lookup failed before deletion");
        case BPlusTree::FindResult::FOUND:
            break;
    }
    if (physical.page_id != expected_rid.page_id
        || physical.slot_id != expected_rid.slot_id) {
        return Status::corruption("B+ tree key refers to an unexpected RID");
    }

    switch (impl_->tree->erase(key)) {
        case BPlusTree::EraseResult::REMOVED:
            return Status::ok_status();
        case BPlusTree::EraseResult::RECLAIM_FAILED:
            // 键已经从树中提交删除，只是空页回收失败。对关系层而言删除
            // 已生效，继续执行可避免留下指向已删除记录的索引项。
            return Status::ok_status();
        case BPlusTree::EraseResult::NOT_FOUND:
            return Status::corruption("B+ tree key disappeared during deletion");
        case BPlusTree::EraseResult::PAGE_ERROR:
            return Status::io_error("B+ tree failed to delete an index entry");
    }
    return Status::internal_error("unknown B+ tree deletion result");
}

Result<std::vector<RID>> BPlusIndexAdapter::lookup(std::int64_t key) const {
    ::RID physical{};
    switch (impl_->tree->find(key, physical)) {
        case BPlusTree::FindResult::FOUND:
            return std::vector<RID>{RID{physical.page_id, physical.slot_id}};
        case BPlusTree::FindResult::NOT_FOUND:
            return std::vector<RID>{};
        case BPlusTree::FindResult::PAGE_ERROR:
            return Status::io_error("B+ tree lookup failed");
    }
    return Status::internal_error("unknown B+ tree lookup result");
}

Status BPlusIndexAdapter::flush() {
    return impl_->buffer_pool->flush_all_pages()
        ? Status::ok_status()
        : Status::io_error("failed to flush B+ tree index pages");
}

std::uint32_t BPlusIndexAdapter::header_page_id() const noexcept {
    return impl_->tree->header_page_id();
}

} // namespace minidb
