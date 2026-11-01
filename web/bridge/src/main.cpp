#include "pagecraft/bridge_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket invalid_socket = -1;
#endif

namespace {

constexpr std::size_t max_request_size = 1024U * 1024U;
std::atomic_bool running{true};

void close_socket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void stop_server(int) {
    running.store(false);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::optional<Request> read_request(Socket client) {
    std::string bytes;
    std::array<char, 8192> buffer{};
    std::size_t header_end = std::string::npos;
    std::size_t expected_size = 0;

    while (bytes.size() < max_request_size) {
        const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) return std::nullopt;
        bytes.append(buffer.data(), static_cast<std::size_t>(received));
        if (header_end == std::string::npos) {
            header_end = bytes.find("\r\n\r\n");
            if (header_end == std::string::npos) continue;
            expected_size = header_end + 4U;
            std::istringstream header_stream(bytes.substr(0, header_end));
            std::string line;
            std::getline(header_stream, line);
            while (std::getline(header_stream, line)) {
                const auto colon = line.find(':');
                if (colon != std::string::npos && lower(trim(line.substr(0, colon))) == "content-length") {
                    try {
                        expected_size += static_cast<std::size_t>(std::stoull(trim(line.substr(colon + 1U))));
                    } catch (...) {
                        return std::nullopt;
                    }
                }
            }
            if (expected_size > max_request_size) return std::nullopt;
        }
        if (header_end != std::string::npos && bytes.size() >= expected_size) break;
    }
    if (header_end == std::string::npos || bytes.size() < expected_size) return std::nullopt;

    Request request;
    std::istringstream stream(bytes.substr(0, header_end));
    std::string line;
    if (!std::getline(stream, line)) return std::nullopt;
    line = trim(line);
    std::istringstream request_line(line);
    std::string protocol;
    if (!(request_line >> request.method >> request.path >> protocol) || protocol.rfind("HTTP/", 0) != 0) {
        return std::nullopt;
    }
    const auto query = request.path.find('?');
    if (query != std::string::npos) request.path.resize(query);
    while (std::getline(stream, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1U));
    }
    request.body = bytes.substr(header_end + 4U, expected_size - header_end - 4U);
    return request;
}

std::string reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

std::string allowed_origin(const Request& request) {
    const auto found = request.headers.find("origin");
    if (found == request.headers.end()) return {};
    if (found->second == "http://127.0.0.1:5173" ||
        found->second == "http://localhost:5173") {
        return found->second;
    }
    return {};
}

bool send_all(Socket socket, std::string_view data) {
    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
        const auto remaining = data.size() - sent_total;
        const int chunk = send(socket, data.data() + sent_total,
                               static_cast<int>(std::min<std::size_t>(remaining,
                                   static_cast<std::size_t>(std::numeric_limits<int>::max()))), 0);
        if (chunk <= 0) return false;
        sent_total += static_cast<std::size_t>(chunk);
    }
    return true;
}

void send_response(Socket client, const Request& request, const pagecraft::ApiResponse& response) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << response.status << ' ' << reason_phrase(response.status) << "\r\n"
            << "Content-Type: application/json; charset=utf-8\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n";
    const auto origin = allowed_origin(request);
    if (!origin.empty()) {
        headers << "Access-Control-Allow-Origin: " << origin << "\r\n"
                << "Vary: Origin\r\n";
    }
    headers << "Connection: close\r\n\r\n";
    const auto message = headers.str() + response.body;
    static_cast<void>(send_all(client, message));
}

int parse_port(const char* text) {
    try {
        const int port = std::stoi(text);
        if (port < 1 || port > 65535) return -1;
        return port;
    } catch (...) {
        return -1;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: pagecraft_bridge <database-directory> [port]\n";
        return EXIT_FAILURE;
    }
    const int port = argc == 3 ? parse_port(argv[2]) : 8765;
    if (port < 0) {
        std::cerr << "Invalid TCP port\n";
        return EXIT_FAILURE;
    }

#ifdef _WIN32
    WSADATA socket_data{};
    if (WSAStartup(MAKEWORD(2, 2), &socket_data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return EXIT_FAILURE;
    }
#endif

    auto api_result = pagecraft::BridgeApi::open(std::filesystem::path(argv[1]));
    if (!api_result.ok()) {
        std::cerr << "Unable to open database: " << api_result.status().message() << '\n';
#ifdef _WIN32
        WSACleanup();
#endif
        return EXIT_FAILURE;
    }
    auto api = std::move(api_result).value();

    const Socket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) {
        std::cerr << "Unable to create server socket\n";
        return EXIT_FAILURE;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 16) != 0) {
        std::cerr << "Unable to listen on 127.0.0.1:" << port << '\n';
        close_socket(server);
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, stop_server);
    std::cout << "PageCraft C++ Bridge\n"
              << "Database: " << std::filesystem::absolute(argv[1]).string() << '\n'
              << "Listening: http://127.0.0.1:" << port << '\n'
              << "Press Ctrl+C to stop.\n";

    while (running.load()) {
        sockaddr_in client_address{};
#ifdef _WIN32
        int client_size = sizeof(client_address);
#else
        socklen_t client_size = sizeof(client_address);
#endif
        const Socket client = accept(server, reinterpret_cast<sockaddr*>(&client_address), &client_size);
        if (client == invalid_socket) continue;
        auto request = read_request(client);
        if (!request) {
            const Request empty_request;
            send_response(client, empty_request,
                          {400, "{\"error\":\"Malformed or oversized HTTP request\"}"});
            close_socket(client);
            continue;
        }
        if (request->method == "OPTIONS") {
            std::ostringstream response;
            response << "HTTP/1.1 204 No Content\r\n"
                     << "Content-Length: 0\r\n";
            const auto origin = allowed_origin(*request);
            if (!origin.empty()) {
                response << "Access-Control-Allow-Origin: " << origin << "\r\n"
                         << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                         << "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
                         << "Vary: Origin\r\n";
            }
            response << "Connection: close\r\n\r\n";
            static_cast<void>(send_all(client, response.str()));
        } else {
            send_response(client, *request,
                          api->handle(request->method, request->path, request->body));
        }
        close_socket(client);
    }

    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return EXIT_SUCCESS;
}
