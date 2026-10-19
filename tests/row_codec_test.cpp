#include "minidb/storage/row_codec.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

minidb::TableSchema make_schema() {
    return minidb::TableSchema{
        "users",
        {
            {"id", minidb::DataType::Int64, 0, false},
            {"name", minidb::DataType::Varchar, 16, false},
            {"nickname", minidb::DataType::Varchar, 32, true},
        }};
}

void test_round_trip() {
    const auto schema = make_schema();
    const minidb::Row input{
        std::int64_t{-42}, std::string("Alice"), std::monostate{}};
    auto encoded = minidb::RowCodec::encode(schema, input);
    assert(encoded.ok());

    auto decoded = minidb::RowCodec::decode(schema, encoded.value());
    assert(decoded.ok());
    assert(decoded.value() == input);
}

void test_type_and_length_validation() {
    const auto schema = make_schema();

    const minidb::Row wrong_type{
        std::string("not an int"), std::string("Alice"), std::monostate{}};
    auto encoded = minidb::RowCodec::encode(schema, wrong_type);
    assert(!encoded.ok());
    assert(encoded.status().code() == minidb::ErrorCode::InvalidArgument);

    const minidb::Row too_long{
        std::int64_t{1}, std::string(17, 'x'), std::monostate{}};
    encoded = minidb::RowCodec::encode(schema, too_long);
    assert(!encoded.ok());

    const minidb::Row unexpected_null{
        std::monostate{}, std::string("Alice"), std::monostate{}};
    encoded = minidb::RowCodec::encode(schema, unexpected_null);
    assert(!encoded.ok());

    const minidb::Row wrong_count{std::int64_t{1}};
    encoded = minidb::RowCodec::encode(schema, wrong_count);
    assert(!encoded.ok());
}

void test_corrupt_record_is_rejected() {
    const auto schema = make_schema();
    const minidb::Row input{
        std::int64_t{7}, std::string("Bob"), std::string("B")};
    auto encoded = minidb::RowCodec::encode(schema, input);
    assert(encoded.ok());

    std::vector<std::byte> truncated = encoded.value();
    truncated.pop_back();
    auto decoded = minidb::RowCodec::decode(schema, truncated);
    assert(!decoded.ok());
    assert(decoded.status().code() == minidb::ErrorCode::Corruption);

    std::vector<std::byte> wrong_magic = encoded.value();
    wrong_magic[0] ^= std::byte{1};
    decoded = minidb::RowCodec::decode(schema, wrong_magic);
    assert(!decoded.ok());
}

} // namespace

int main() {
    test_round_trip();
    test_type_and_length_validation();
    test_corrupt_record_is_rejected();
}
