#include "pagecraft/bridge_api.h"

#include "minidb/catalog/catalog.h"
#include "minidb/catalog/metadata.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace pagecraft {
namespace {

std::string escape_json(std::string_view input) {
    std::ostringstream out;
    for (const unsigned char character : input) {
        switch (character) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (character < 0x20U) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(character) << std::dec;
            } else {
                out << static_cast<char>(character);
            }
        }
    }
    return out.str();
}

std::string json_string(std::string_view value) {
    return "\"" + escape_json(value) + "\"";
}

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

class QueryJsonParser {
public:
    explicit QueryJsonParser(std::string_view text) : text_(text) {}

    std::optional<std::string> parse() {
        skip_space();
        if (!consume('{')) return std::nullopt;
        skip_space();
        std::optional<std::string> sql;
        if (consume('}')) return std::nullopt;
        while (true) {
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_space();
            if (!consume(':')) return std::nullopt;
            skip_space();
            auto value = parse_string();
            if (!value) return std::nullopt;
            if (*key == "sql") sql = std::move(*value);
            skip_space();
            if (consume('}')) break;
            if (!consume(',')) return std::nullopt;
            skip_space();
        }
        skip_space();
        if (position_ != text_.size() || !sql || sql->empty()) return std::nullopt;
        return sql;
    }

private:
    void skip_space() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ >= text_.size() || text_[position_] != expected) return false;
        ++position_;
        return true;
    }

    std::optional<std::string> parse_string() {
        skip_space();
        if (!consume('"')) return std::nullopt;
        std::string value;
        while (position_ < text_.size()) {
            const char current = text_[position_++];
            if (current == '"') return value;
            if (current != '\\') {
                if (static_cast<unsigned char>(current) < 0x20U) return std::nullopt;
                value.push_back(current);
                continue;
            }
            if (position_ >= text_.size()) return std::nullopt;
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                if (position_ + 4U > text_.size()) return std::nullopt;
                std::uint32_t code_point = 0;
                for (int index = 0; index < 4; ++index) {
                    const char digit = text_[position_++];
                    code_point <<= 4U;
                    if (digit >= '0' && digit <= '9') code_point += digit - '0';
                    else if (digit >= 'a' && digit <= 'f') code_point += digit - 'a' + 10;
                    else if (digit >= 'A' && digit <= 'F') code_point += digit - 'A' + 10;
                    else return std::nullopt;
                }
                if (code_point >= 0xD800U && code_point <= 0xDFFFU) return std::nullopt;
                append_utf8(value, code_point);
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::string_view text_;
    std::size_t position_{0};
};

std::string error_code_name(minidb::ErrorCode code) {
    switch (code) {
    case minidb::ErrorCode::Ok: return "OK";
    case minidb::ErrorCode::InvalidArgument: return "INVALID_ARGUMENT";
    case minidb::ErrorCode::NotFound: return "NOT_FOUND";
    case minidb::ErrorCode::AlreadyExists: return "ALREADY_EXISTS";
    case minidb::ErrorCode::IoError: return "IO_ERROR";
    case minidb::ErrorCode::Corruption: return "CORRUPTION";
    case minidb::ErrorCode::NoSpace: return "NO_SPACE";
    case minidb::ErrorCode::RecordTooLarge: return "RECORD_TOO_LARGE";
    case minidb::ErrorCode::Unsupported: return "UNSUPPORTED";
    case minidb::ErrorCode::InternalError: return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

std::string value_json(const minidb::Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "null";
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    return json_string(std::get<std::string>(value));
}

std::string inferred_type(std::size_t column, const std::vector<minidb::Row>& rows) {
    for (const auto& row : rows) {
        if (column >= row.size()) continue;
        if (std::holds_alternative<std::int64_t>(row[column])) return "INT64";
        if (std::holds_alternative<std::string>(row[column])) return "VARCHAR";
    }
    return "UNKNOWN";
}

ApiResponse json_error(int status, std::string_view code, std::string_view message) {
    return {status,
            "{\"success\":false,\"columns\":[],\"rows\":[],"
            "\"affectedRows\":0,\"elapsedMs\":0,\"message\":" + json_string(message) +
            ",\"error\":{\"code\":" + json_string(code) +
            ",\"message\":" + json_string(message) + "}}"};
}

std::string index_state(minidb::IndexState state) {
    switch (state) {
    case minidb::IndexState::Available: return "available";
    case minidb::IndexState::Building: return "building";
    case minidb::IndexState::Failed: return "unsupported";
    }
    return "unknown";
}

} // namespace

minidb::Result<std::unique_ptr<BridgeApi>> BridgeApi::open(
    const std::filesystem::path& database_directory) {
    auto database_result = minidb::Database::open(database_directory);
    if (!database_result.ok()) return database_result.status();
    return std::unique_ptr<BridgeApi>(new BridgeApi(
        database_directory, std::move(database_result).value()));
}

BridgeApi::BridgeApi(std::filesystem::path database_directory,
                     std::unique_ptr<minidb::Database> database)
    : database_directory_(std::move(database_directory)),
      database_(std::move(database)) {}

ApiResponse BridgeApi::handle(std::string_view method,
                              std::string_view path,
                              std::string_view body) {
    if (method == "GET" && path == "/api/v1/health") return health();
    if (method == "GET" && path == "/api/v1/catalog") return catalog();
    if (method == "POST" && path == "/api/v1/query") return query(body);
    return {404, "{\"error\":\"Not found\"}"};
}

ApiResponse BridgeApi::health() const {
    return {200, "{\"healthy\":true,\"service\":\"PageCraft C++ Bridge\","
                 "\"version\":\"1.0\"}"};
}

ApiResponse BridgeApi::catalog() const {
    auto catalog_result = minidb::Catalog::open(database_directory_);
    if (!catalog_result.ok()) {
        return {500, "{\"tables\":[],\"unavailableReason\":" +
                     json_string(catalog_result.status().message()) + "}"};
    }
    auto catalog = std::move(catalog_result).value();
    const auto tables = catalog->list_tables();
    std::ostringstream json;
    json << "{\"tables\":[";
    for (std::size_t table_index = 0; table_index < tables.size(); ++table_index) {
        if (table_index != 0) json << ',';
        const auto& table = tables[table_index];
        json << "{\"id\":" << table.id
             << ",\"name\":" << json_string(table.schema.name)
             << ",\"columns\":[";
        for (std::size_t column_index = 0; column_index < table.schema.columns.size(); ++column_index) {
            if (column_index != 0) json << ',';
            const auto& column = table.schema.columns[column_index];
            json << "{\"name\":" << json_string(column.name)
                 << ",\"type\":" << json_string(column.type == minidb::DataType::Int64 ? "INT64" : "VARCHAR")
                 << ",\"nullable\":" << (column.nullable ? "true" : "false");
            if (column.type == minidb::DataType::Varchar) {
                json << ",\"maxLength\":" << column.max_length;
            }
            json << '}';
        }
        json << "],\"indexes\":[";
        const auto indexes = catalog->list_indexes(table.id);
        for (std::size_t index_position = 0; index_position < indexes.size(); ++index_position) {
            if (index_position != 0) json << ',';
            const auto& index = indexes[index_position];
            json << "{\"name\":" << json_string(index.name) << ",\"columns\":[";
            for (std::size_t key_position = 0; key_position < index.key_columns.size(); ++key_position) {
                if (key_position != 0) json << ',';
                const auto column_number = index.key_columns[key_position];
                if (column_number < table.schema.columns.size()) {
                    json << json_string(table.schema.columns[column_number].name);
                } else {
                    json << json_string("<invalid-column>");
                }
            }
            json << "],\"state\":" << json_string(index_state(index.state)) << '}';
        }
        json << "]}";
    }
    json << "]}";
    return {200, json.str()};
}

ApiResponse BridgeApi::query(std::string_view body) {
    auto sql = QueryJsonParser(body).parse();
    if (!sql) return json_error(400, "INVALID_REQUEST", "Expected a non-empty JSON string field named sql");

    const auto started = std::chrono::steady_clock::now();
    const auto result = database_->execute(*sql);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();

    std::ostringstream json;
    json << "{\"success\":" << (result.ok() ? "true" : "false")
         << ",\"columns\":[";
    for (std::size_t index = 0; index < result.columns.size(); ++index) {
        if (index != 0) json << ',';
        json << "{\"name\":" << json_string(result.columns[index])
             << ",\"type\":" << json_string(inferred_type(index, result.rows)) << '}';
    }
    json << "],\"rows\":[";
    for (std::size_t row_index = 0; row_index < result.rows.size(); ++row_index) {
        if (row_index != 0) json << ',';
        json << '[';
        const auto& row = result.rows[row_index];
        for (std::size_t column_index = 0; column_index < row.size(); ++column_index) {
            if (column_index != 0) json << ',';
            json << value_json(row[column_index]);
        }
        json << ']';
    }
    json << "],\"affectedRows\":" << result.affected_rows
         << ",\"elapsedMs\":" << elapsed
         << ",\"message\":" << json_string(result.ok() ? result.message : result.error_message());
    if (!result.ok()) {
        json << ",\"error\":{\"code\":" << json_string(error_code_name(result.status.code()))
             << ",\"message\":" << json_string(result.error_message()) << '}';
    }
    if (result.plan) json << ",\"formattedPlan\":" << json_string(*result.plan);
    json << '}';
    return {200, json.str()};
}

} // namespace pagecraft
