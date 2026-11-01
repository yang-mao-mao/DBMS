#include "pagecraft/bridge_api.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void contains(const std::string& text, const std::string& expected) {
    if (text.find(expected) == std::string::npos) {
        std::cerr << "Expected response to contain: " << expected << "\nActual: " << text << '\n';
        std::abort();
    }
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("pagecraft-bridge-test-" + std::to_string(unique));
    std::filesystem::remove_all(directory);

    auto open_result = pagecraft::BridgeApi::open(directory);
    assert(open_result.ok());
    auto api = std::move(open_result).value();

    const auto health = api->handle("GET", "/api/v1/health", "");
    assert(health.status == 200);
    contains(health.body, "\"healthy\":true");

    const auto invalid = api->handle("POST", "/api/v1/query", "{}");
    assert(invalid.status == 400);
    contains(invalid.body, "INVALID_REQUEST");

    const auto create = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"CREATE TABLE students (id INT, name VARCHAR(32));"})");
    assert(create.status == 200);
    contains(create.body, "\"success\":true");

    const auto insert = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"INSERT INTO students VALUES (1, 'Alice');"})");
    contains(insert.body, "\"affectedRows\":1");

    const auto select = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"SELECT * FROM students;"})");
    contains(select.body, "\"name\":\"id\",\"type\":\"INT64\"");
    contains(select.body, "[1,\"Alice\"]");

    const auto create_index = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"CREATE INDEX idx_students_id ON students(id);"})");
    assert(create_index.status == 200);
    contains(create_index.body, "\"success\":true");

    const auto explain = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"EXPLAIN SELECT name FROM students WHERE id = 1;"})");
    contains(explain.body, "formattedPlan");
    contains(explain.body, "IndexScan");

    const auto catalog = api->handle("GET", "/api/v1/catalog", "");
    assert(catalog.status == 200);
    contains(catalog.body, "\"name\":\"students\"");
    contains(catalog.body, "\"maxLength\":32");

    const auto deleted = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"DELETE FROM students WHERE id = 1;"})");
    contains(deleted.body, "\"success\":true");
    contains(deleted.body, "\"affectedRows\":1");

    const auto select_after_delete = api->handle(
        "POST", "/api/v1/query",
        R"({"sql":"SELECT * FROM students;"})");
    contains(select_after_delete.body, "\"rows\":[]");

    api.reset();
    std::filesystem::remove_all(directory);
    std::cout << "Bridge API integration test passed\n";
    return 0;
}
