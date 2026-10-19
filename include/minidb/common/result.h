#ifndef MINIDB_COMMON_RESULT_H
#define MINIDB_COMMON_RESULT_H

#include "minidb/common/status.h"

#include <optional>
#include <stdexcept>
#include <utility>

namespace minidb {

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
