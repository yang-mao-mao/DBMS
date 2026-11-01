#ifndef MINIDB_STORAGE_BPLUS_INDEX_ADAPTER_H
#define MINIDB_STORAGE_BPLUS_INDEX_ADAPTER_H

#include "minidb/common/result.h"
#include "minidb/storage/rid.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace minidb {

// 成员 B 的边界适配器：隐藏成员 A 的全局 RID、BufferPoolManager 和
// BPlusTree 类型。当前物理树是一键一 RID，因此只暴露唯一 INT64 索引。
class BPlusIndexAdapter final {
public:
    [[nodiscard]] static Result<std::unique_ptr<BPlusIndexAdapter>> create(
        const std::filesystem::path& file);
    [[nodiscard]] static Result<std::unique_ptr<BPlusIndexAdapter>> open(
        const std::filesystem::path& file, std::uint32_t header_page_id);

    ~BPlusIndexAdapter();
    BPlusIndexAdapter(const BPlusIndexAdapter&) = delete;
    BPlusIndexAdapter& operator=(const BPlusIndexAdapter&) = delete;

    [[nodiscard]] Status insert(std::int64_t key, RID rid);
    [[nodiscard]] Status erase(std::int64_t key, RID expected_rid);
    [[nodiscard]] Result<std::vector<RID>> lookup(std::int64_t key) const;
    [[nodiscard]] Status flush();
    [[nodiscard]] std::uint32_t header_page_id() const noexcept;

private:
    struct Impl;
    explicit BPlusIndexAdapter(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace minidb

#endif
