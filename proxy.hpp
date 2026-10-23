#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "process.hpp"

namespace opg {

struct ProxyStats {
    std::atomic<unsigned long long> bytesUp{0};
    std::atomic<unsigned long long> bytesDown{0};
    std::atomic<unsigned long long> totalRequests{0};
    std::atomic<unsigned long long> activeRequests{0};
    std::atomic<unsigned long long> blockedRequests{0};
    std::atomic<unsigned long long> promptTokens{0};
    std::atomic<unsigned long long> completionTokens{0};
    std::atomic<unsigned long long> totalTokens{0};
};

class ProxyServer {
public:
    std::atomic<bool> running{false};
    SOCKET listener = INVALID_SOCKET;
    unsigned short port = 11435;
    unsigned short upstreamPort = 11434;
    bool requireKey = false;
    std::wstring apiKey;
    ProxyStats stats;

    bool isBlocked(const std::wstring& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        return blockedIps_.count(ip) != 0;
    }

    void setBlocked(const std::vector<std::wstring>& ips) {
        std::lock_guard<std::mutex> lock(mutex_);
        blockedIps_.clear();
        for (const auto& ip : ips) {
            if (!ip.empty()) blockedIps_.insert(ip);
        }
    }

    std::vector<std::wstring> blockedList() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::wstring>(blockedIps_.begin(), blockedIps_.end());
    }

    bool addBlocked(const std::wstring& ip) {
        if (ip.empty()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return blockedIps_.insert(ip).second;
    }

    bool removeBlocked(const std::wstring& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        return blockedIps_.erase(ip) != 0;
    }

private:
    std::mutex mutex_;
    std::set<std::wstring> blockedIps_;
};

inline void closeSocket(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

inline std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return value;
}

inline std::string headerValue(const std::string& headers, const std::string& name) {
    size_t pos = 0;
    while ((pos = headers.find(name + ":", pos)) != std::string::npos) {
        size_t lineStart = headers.rfind('\n', pos);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        if (lineStart != pos) break;
        pos += name.size() + 1;
        while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) ++pos;
        size_t end = headers.find("\r\n", pos);
        if (end == std::string::npos) end = headers.size();
        std::string value = headers.substr(pos, end - pos);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        return value;
    }
    return {};
}

inline std::string wideToUtf8(const std::wstring& value);
inline std::wstring utf8ToWide(const std::string& value);

inline bool sendAll(SOCKET socket, const char* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        int chunk = static_cast<int>(std::min<size_t>(size - offset, 1024 * 1024));
        int sent = send(socket, data + offset, chunk, 0);
        if (sent == SOCKET_ERROR) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

inline bool sendHttpResponse(SOCKET client, unsigned status, const std::wstring& message) {
    std::string body = "{\"error\":{\"message\":\"" + wideToUtf8(message) + "\"}}";
    std::string response;
    response += "HTTP/1.1 " + std::to_string(status) + " Error\r\n";
    response += "Content-Type: application/json; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return sendAll(client, response.data(), response.size());
}

inline std::wstring clientIp(SOCKET socket) {
    sockaddr_storage address{};
    int length = sizeof(address);
    if (getpeername(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) return L"unknown";
    wchar_t buffer[INET6_ADDRSTRLEN]{};
    if (address.ss_family == AF_INET) {
        auto* addr = reinterpret_cast<sockaddr_in*>(&address);
        InetNtopW(AF_INET, &addr->sin_addr, buffer, INET6_ADDRSTRLEN);
    } else if (address.ss_family == AF_INET6) {
        auto* addr = reinterpret_cast<sockaddr_in6*>(&address);
        InetNtopW(AF_INET6, &addr->sin6_addr, buffer, INET6_ADDRSTRLEN);
    } else {
        return L"unknown";
    }
    return buffer;
}

inline bool receiveHttpRequest(SOCKET client, std::string& firstLine, std::string& headers,
                               std::string& body, std::wstring& error) {
    std::string request;
    char buffer[64 * 1024];
    size_t headerEnd = std::string::npos;
    while (request.size() < 1024 * 1024) {
        int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received == SOCKET_ERROR || received == 0) {
            error = L"读取客户端请求失败或连接关闭。";
            return false;
        }
        request.append(buffer, buffer + received);
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    if (headerEnd == std::string::npos) {
        error = L"请求头过大。";
        return false;
    }

    size_t firstLineEnd = request.find("\r\n");
    if (firstLineEnd == std::string::npos) {
        error = L"请求格式无效。";
        return false;
    }
    firstLine = request.substr(0, firstLineEnd);
    headers = request.substr(firstLineEnd + 2, headerEnd - firstLineEnd - 2);
    body = request.substr(headerEnd + 4);

    std::string contentLength = headerValue(headers, "Content-Length");
    if (!contentLength.empty()) {
        unsigned long long expected = strtoull(contentLength.c_str(), nullptr, 10);
        if (expected > 1024ULL * 1024ULL * 1024ULL) {
            error = L"请求体过大。";
            return false;
        }
        while (body.size() < expected) {
            int received = recv(client, buffer, static_cast<int>(std::min<size_t>(sizeof(buffer), static_cast<size_t>(expected - body.size()))), 0);
            if (received == SOCKET_ERROR || received == 0) {
                error = L"读取请求体失败或连接关闭。";
                return false;
            }
            body.append(buffer, buffer + received);
        }
        if (body.size() > expected) body.resize(static_cast<size_t>(expected));
    }
    return true;
}

inline void scanTokenCounts(const std::string& text, ProxyServer& server) {
    auto countKey = [&](const std::string& key, std::atomic<unsigned long long>& target) {
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            pos += key.size();
            size_t colon = text.find(':', pos);
            if (colon == std::string::npos || colon > pos + 8) continue;
            ++colon;
            while (colon < text.size() && isspace(static_cast<unsigned char>(text[colon]))) ++colon;
            if (colon >= text.size() || !isdigit(static_cast<unsigned char>(text[colon]))) continue;
            unsigned long long value = strtoull(text.c_str() + colon, nullptr, 10);
            unsigned long long previous = target.exchange(value);
            unsigned long long delta = value > previous ? value - previous : value;
            server.stats.totalTokens.fetch_add(delta);
        }
    };
    countKey("\"prompt_eval_count\"", server.stats.promptTokens);
    countKey("\"eval_count\"", server.stats.completionTokens);
}

inline bool connectUpstream(SOCKET& upstream, unsigned short port, std::wstring& error) {
    upstream = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (upstream == INVALID_SOCKET) {
        error = L"创建上游连接失败。";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonW(AF_INET, L"127.0.0.1", &address.sin_addr) != 1) {
        error = L"上游地址无效。";
        return false;
    }
    if (connect(upstream, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        error = L"无法连接本机 Ollama 服务（127.0.0.1:11434）。请先启动 Ollama。";
        return false;
    }
    int timeout = 300000;
    setsockopt(upstream, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(upstream, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    return true;
}

inline std::string rebuildRequest(const std::string& firstLine, const std::string& headers,
                                  const std::string& body) {
    std::string result = firstLine + "\r\n";
    size_t start = 0;
    while (start < headers.size()) {
        size_t end = headers.find("\r\n", start);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(start, end - start);
        std::string lowerLine = lowerAscii(line.substr(0, line.find(':')));
        if (lowerLine != "host" && lowerLine != "connection" && lowerLine != "content-length") {
            result += line + "\r\n";
        }
        start = end + 2;
    }
    result += "Host: 127.0.0.1:" + std::to_string(11434) + "\r\n";
    result += "Connection: close\r\n";
    result += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    result += body;
    return result;
}

inline bool hasApiKey(const std::string& headers, const std::wstring& expected) {
    std::string authorization = headerValue(headers, "Authorization");
    std::string xApiKey = headerValue(headers, "x-api-key");
    std::string apiKeyHeader = headerValue(headers, "api-key");
    std::string expectedUtf8 = wideToUtf8(expected);
    const std::string prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) == 0) authorization = authorization.substr(prefix.size());
    while (!authorization.empty() && authorization.front() == ' ') authorization.erase(authorization.begin());
    while (!xApiKey.empty() && xApiKey.front() == ' ') xApiKey.erase(xApiKey.begin());
    while (!apiKeyHeader.empty() && apiKeyHeader.front() == ' ') apiKeyHeader.erase(apiKeyHeader.begin());
    return authorization == expectedUtf8 || xApiKey == expectedUtf8 || apiKeyHeader == expectedUtf8;
}

inline void handleProxyClient(SOCKET client, ProxyServer& server) {
    server.stats.activeRequests.fetch_add(1);
    server.stats.totalRequests.fetch_add(1);
    std::string firstLine, headers, body;
    std::wstring error;
    std::string rawRequest;
    if (!receiveHttpRequest(client, firstLine, headers, body, error)) {
        server.stats.activeRequests.fetch_sub(1);
        sendHttpResponse(client, 400, error);
        closesocket(client);
        return;
    }

    std::wstring ip = clientIp(client);
    if (server.isBlocked(ip)) {
        server.stats.blockedRequests.fetch_add(1);
        server.stats.activeRequests.fetch_sub(1);
        sendHttpResponse(client, 403, L"该 IP 已被拒绝访问。");
        closesocket(client);
        return;
    }

    if (server.requireKey && !hasApiKey(headers, server.apiKey)) {
        server.stats.blockedRequests.fetch_add(1);
        server.stats.activeRequests.fetch_sub(1);
        sendHttpResponse(client, 401, L"API Key 无效或缺失。");
        closesocket(client);
        return;
    }

    SOCKET upstream = INVALID_SOCKET;
    if (!connectUpstream(upstream, server.upstreamPort, error)) {
        server.stats.activeRequests.fetch_sub(1);
        sendHttpResponse(client, 502, error);
        closeSocket(upstream);
        closesocket(client);
        return;
    }

    std::string request = rebuildRequest(firstLine, headers, body);
    if (!sendAll(upstream, request.data(), request.size())) {
        server.stats.activeRequests.fetch_sub(1);
        sendHttpResponse(client, 502, L"发送请求到 Ollama 失败。");
        closeSocket(upstream);
        closesocket(client);
        return;
    }

    char buffer[256 * 1024];
    std::string scanBuffer;
    for (;;) {
        int received = recv(upstream, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received == SOCKET_ERROR || received == 0) break;
        server.stats.bytesDown.fetch_add(static_cast<unsigned long long>(received));
        if (!sendAll(client, buffer, static_cast<size_t>(received))) break;
        scanBuffer.append(buffer, buffer + received);
        if (scanBuffer.size() > 128 * 1024) {
            scanBuffer.erase(0, scanBuffer.size() - 4096);
        }
        scanTokenCounts(scanBuffer, server);
    }

    closeSocket(upstream);
    closesocket(client);
    server.stats.activeRequests.fetch_sub(1);
}

inline bool startProxyServer(ProxyServer& server, unsigned short port, std::wstring& error) {
    if (server.running) {
        error = L"服务端已经在运行。";
        return false;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = L"初始化 Winsock 失败。";
        return false;
    }
    server.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.listener == INVALID_SOCKET) {
        error = L"创建监听 Socket 失败。";
        WSACleanup();
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(server.listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    if (bind(server.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(server.listener, 64) == SOCKET_ERROR) {
        int code = WSAGetLastError();
        wchar_t buffer[160];
        swprintf(buffer, 160, L"监听端口 %u 失败（Winsock 错误 %d）。", static_cast<unsigned>(port), code);
        error = buffer;
        closeSocket(server.listener);
        WSACleanup();
        return false;
    }
    server.port = port;
    server.running = true;
    std::thread([&server]() {
        while (server.running) {
            SOCKET client = accept(server.listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                if (!server.running) break;
                continue;
            }
            int timeout = 300000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            std::thread(handleProxyClient, client, std::ref(server)).detach();
        }
        closeSocket(server.listener);
        WSACleanup();
    }).detach();
    return true;
}

inline void stopProxyServer(ProxyServer& server) {
    server.running = false;
    closeSocket(server.listener);
}

inline unsigned short getProxyPort(const ProxyServer& server) { return server.port; }

} // namespace opg





