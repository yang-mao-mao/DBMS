#include "minidb/execution/operator.h"

#include <compare>
#include <cstdint>
#include <string>
#include <utility>

namespace minidb::execution {
namespace {

Status not_open_status() {
    return Status::internal_error("operator is not open");
}

template <typename T>
bool apply_comparison(const T& left, const T& right,
                      sql::ComparisonOperator operation) {
    switch (operation) {
        case sql::ComparisonOperator::Equal: return left == right;
        case sql::ComparisonOperator::NotEqual: return left != right;
        case sql::ComparisonOperator::Less: return left < right;
        case sql::ComparisonOperator::LessEqual: return left <= right;
        case sql::ComparisonOperator::Greater: return left > right;
        case sql::ComparisonOperator::GreaterEqual: return left >= right;
    }
    return false;
}

Result<bool> evaluate(const query::BoundPredicate& predicate, const Row& row) {
    if (predicate.column_index >= row.size()) {
        return Status::corruption("filter column is missing from input row");
    }
    const Value& value = row[predicate.column_index];
    if (std::holds_alternative<std::monostate>(value)) return false;

    switch (predicate.column_type) {
        case DataType::Int64: {
            const auto* left = std::get_if<std::int64_t>(&value);
            const auto* right = std::get_if<std::int64_t>(&predicate.value);
            if (left == nullptr || right == nullptr) {
                return Status::corruption("INT filter received a non-INT value");
            }
            return apply_comparison(*left, *right, predicate.operation);
        }
        case DataType::Varchar: {
            const auto* left = std::get_if<std::string>(&value);
            const auto* right = std::get_if<std::string>(&predicate.value);
            if (left == nullptr || right == nullptr) {
                return Status::corruption(
                    "VARCHAR filter received a non-VARCHAR value");
            }
            return apply_comparison(*left, *right, predicate.operation);
        }
    }
    return Status::internal_error("unknown filter data type");
}

} // namespace

Status TableScanOperator::open() {
    if (open_) return Status::internal_error("TableScan is already open");
    auto cursor = storage_.scan_rows(table_id_);
    if (!cursor.ok()) return cursor.status();
    cursor_ = std::move(cursor).value();
    open_ = true;
    return Status::ok_status();
}

Result<std::optional<RowRecord>> TableScanOperator::next() {
    if (!open_ || !cursor_) return not_open_status();
    return cursor_->next();
}

Status TableScanOperator::close() {
    cursor_.reset();
    open_ = false;
    return Status::ok_status();
}

Status IndexScanOperator::open() {
    if (open_) return Status::internal_error("IndexScan is already open");
    auto lookup = storage_.index_lookup(table_id_, index_name_, key_);
    if (!lookup.ok()) return lookup.status();
    rids_ = std::move(lookup).value();
    position_ = 0;
    open_ = true;
    return Status::ok_status();
}

Result<std::optional<RowRecord>> IndexScanOperator::next() {
    if (!open_) return not_open_status();
    if (position_ >= rids_.size()) return std::optional<RowRecord>{};

    const RID rid = rids_[position_++];
    auto record = storage_.get_row_by_rid(table_id_, rid);
    if (!record.ok()) return record.status();
    if (!record.value()) {
        return Status::corruption("index refers to a missing record");
    }
    return std::move(record).value();
}

Status IndexScanOperator::close() {
    rids_.clear();
    position_ = 0;
    open_ = false;
    return Status::ok_status();
}

Status FilterOperator::open() {
    if (open_) return Status::internal_error("Filter is already open");
    const Status status = child_->open();
    if (!status.ok()) return status;
    open_ = true;
    return Status::ok_status();
}

Result<std::optional<RowRecord>> FilterOperator::next() {
    if (!open_) return not_open_status();
    while (true) {
        auto record = child_->next();
        if (!record.ok()) return record.status();
        if (!record.value()) return std::optional<RowRecord>{};
        auto matches = evaluate(predicate_, record.value()->row);
        if (!matches.ok()) return matches.status();
        if (matches.value()) return std::move(record).value();
    }
}

Status FilterOperator::close() {
    const Status status = child_->close();
    open_ = false;
    return status;
}

Status ProjectOperator::open() {
    if (open_) return Status::internal_error("Project is already open");
    const Status status = child_->open();
    if (!status.ok()) return status;
    open_ = true;
    return Status::ok_status();
}

Result<std::optional<RowRecord>> ProjectOperator::next() {
    if (!open_) return not_open_status();
    auto input = child_->next();
    if (!input.ok()) return input.status();
    if (!input.value()) return std::optional<RowRecord>{};

    Row projected;
    projected.reserve(column_indices_.size());
    for (const std::uint32_t index : column_indices_) {
        if (index >= input.value()->row.size()) {
            return Status::corruption("project column is missing from input row");
        }
        projected.push_back(input.value()->row[index]);
    }
    return std::optional<RowRecord>{
        RowRecord{input.value()->rid, std::move(projected)}};
}

Status ProjectOperator::close() {
    const Status status = child_->close();
    open_ = false;
    return status;
}

} // namespace minidb::execution
