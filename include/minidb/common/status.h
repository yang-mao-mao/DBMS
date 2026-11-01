#ifndef MINIDB_COMMON_STATUS_H
#define MINIDB_COMMON_STATUS_H

#include <string>
#include <utility>

namespace minidb {

// MiniDB 各层统一使用的错误码。调用方应优先判断 Status::ok()，
// 不依赖错误消息文本进行程序分支。
enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    IoError,
    Corruption,
    NoSpace,
    RecordTooLarge,
    Unsupported,
    InternalError,
};

// 表示一次不携带返回值的操作结果；默认构造即成功。
class Status {
public:
    Status() = default;
    Status(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    [[nodiscard]] static Status ok_status() { return {}; }
    [[nodiscard]] static Status invalid_argument(std::string message) {
        return {ErrorCode::InvalidArgument, std::move(message)};
    }
    [[nodiscard]] static Status not_found(std::string message) {
        return {ErrorCode::NotFound, std::move(message)};
    }
    [[nodiscard]] static Status already_exists(std::string message) {
        return {ErrorCode::AlreadyExists, std::move(message)};
    }
    [[nodiscard]] static Status io_error(std::string message) {
        return {ErrorCode::IoError, std::move(message)};
    }
    [[nodiscard]] static Status corruption(std::string message) {
        return {ErrorCode::Corruption, std::move(message)};
    }
    [[nodiscard]] static Status no_space(std::string message) {
        return {ErrorCode::NoSpace, std::move(message)};
    }
    [[nodiscard]] static Status record_too_large(std::string message) {
        return {ErrorCode::RecordTooLarge, std::move(message)};
    }
    [[nodiscard]] static Status unsupported(std::string message) {
        return {ErrorCode::Unsupported, std::move(message)};
    }
    [[nodiscard]] static Status internal_error(std::string message) {
        return {ErrorCode::InternalError, std::move(message)};
    }

    [[nodiscard]] bool ok() const noexcept { return code_ == ErrorCode::Ok; }
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    ErrorCode code_{ErrorCode::Ok};
    std::string message_;
};

} // namespace minidb

#endif // MINIDB_COMMON_STATUS_H
