#ifndef MINIDB_COMMON_RESULT_H
#define MINIDB_COMMON_RESULT_H

#include "minidb/common/status.h"

#include <optional>
#include <stdexcept>
#include <utility>

namespace minidb {

// 同时承载“成功值”或“失败状态”的轻量结果类型。
// 成功时 value_ 有值；失败时 status_ 保存具体错误，二者不会同时有效。
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}

    Result(Status status) : status_(std::move(status)) {
        if (status_.ok()) {
            status_ = Status::internal_error(
                "an error Result cannot contain an OK status");
        }
    }

    [[nodiscard]] bool ok() const noexcept { return value_.has_value(); }
    [[nodiscard]] const Status& status() const noexcept { return status_; }

    // value() 仅能在 ok() 为 true 时调用；误用会抛出 logic_error，
    // 以便尽早暴露上层遗漏的错误检查。
    [[nodiscard]] T& value() & {
        if (!value_) throw std::logic_error("Result has no value");
        return *value_;
    }

    [[nodiscard]] const T& value() const& {
        if (!value_) throw std::logic_error("Result has no value");
        return *value_;
    }

    [[nodiscard]] T&& value() && {
        if (!value_) throw std::logic_error("Result has no value");
        return std::move(*value_);
    }

private:
    Status status_{};
    std::optional<T> value_;
};

} // namespace minidb

#endif // MINIDB_COMMON_RESULT_H
