#include "core/proxy_server.hpp"
#include "core/socket.hpp"
#include "core/proxy_protocol.hpp"

#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace opg {

// ---------- ProxyServer 的成员实现 ----------
bool ProxyServer::isBlocked(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    return blockedIps_.count(ip) != 0;
}
void ProxyServer::setBlocked(const std::vector<std::string>& ips) {
    std::lock_guard<std::mutex> lock(mutex_);
    blockedIps_.clear();
    for (const auto& ip : ips) if (!ip.empty()) blockedIps_.insert(ip);
}
std::vector<std::string> ProxyServer::blockedList() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(blockedIps_.begin(), blockedIps_.end());
}
bool ProxyServer::addBlocked(const std::string& ip) {
    if (ip.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return blockedIps_.insert(ip).second;
}
bool ProxyServer::removeBlocked(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    return blockedIps_.erase(ip) != 0;
}

// ---------- 内部工具 ----------
namespace {

bool sendAll(SocketHandle s, const std::string& data) {
    const SocketApi& api = getSocketApi();
    size_t offset = 0;
    while (offset < data.size()) {
        int chunk = static_cast<int>(std::min<size_t>(data.size() - offset, 1024 * 1024));
        int sent = api.send(s, data.data() + offset, chunk);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool receiveHttpRequest(SocketHandle client,
                        std::string& firstLine,
                        std::string& headers,
                        std::string& body,
                        std::string& error) {
    const SocketApi& api = getSocketApi();
    std::string buf;
    char tmp[65536];
    size_t headerEnd = std::string::npos;
    while (buf.size() < 1024 * 1024) {
        int n = api.recv(client, tmp, sizeof(tmp));
        if (n <= 0) { error = "读取客户端请求失败或连接关闭"; return false; }
        buf.append(tmp, tmp + n);
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    if (headerEnd == std::string::npos) { error = "请求头过大"; return false; }

    size_t firstLineEnd = buf.find("\r\n");
    if (firstLineEnd == std::string::npos) { error = "请求格式无效"; return false; }
    firstLine = buf.substr(0, firstLineEnd);
    headers = buf.substr(firstLineEnd + 2, headerEnd - firstLineEnd - 2);
    body = buf.substr(headerEnd + 4);

    std::string cl = headerValue(headers, "Content-Length");
    if (!cl.empty()) {
        unsigned long long expected = std::strtoull(cl.c_str(), nullptr, 10);
        if (expected > 1024ULL * 1024ULL * 1024ULL) { error = "请求体过大"; return false; }
        while (body.size() < expected) {
            int n = api.recv(client, tmp,
                             static_cast<int>(std::min<size_t>(sizeof(tmp),
                                                               static_cast<size_t>(expected - body.size()))));
            if (n <= 0) { error = "读取请求体失败或连接关闭"; return false; }
            body.append(tmp, tmp + n);
        }
        if (body.size() > expected) body.resize(static_cast<size_t>(expected));
    }
    return true;
}

void scanTokenCounts(const std::string& text, ProxyStats& stats) {
    auto countKey = [&](const std::string& key, std::atomic<unsigned long long>& target) {
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            pos += key.size();
            size_t colon = text.find(':', pos);
            if (colon == std::string::npos || colon > pos + 8) continue;
            ++colon;
            while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) ++colon;
            if (colon >= text.size() || !std::isdigit(static_cast<unsigned char>(text[colon]))) continue;
            unsigned long long value = std::strtoull(text.c_str() + colon, nullptr, 10);
            unsigned long long prev = target.exchange(value);
            unsigned long long delta = value > prev ? value - prev : value;
            stats.totalTokens.fetch_add(delta);
        }
    };
    countKey("\"prompt_eval_count\"", stats.promptTokens);
    countKey("\"eval_count\"", stats.completionTokens);
}

void handleClient(SocketHandle client, ProxyServer& server) {
    server.stats.activeRequests.fetch_add(1);
    server.stats.totalRequests.fetch_add(1);

    std::string firstLine, headers, body, error;
    if (!receiveHttpRequest(client, firstLine, headers, body, error)) {
        sendAll(client, buildHttpErrorResponse(400, error));
        server.stats.activeRequests.fetch_sub(1);
        getSocketApi().close(client);
        return;
    }

    std::string ip = getSocketApi().peerIp(client);
    if (server.isBlocked(ip)) {
        server.stats.blockedRequests.fetch_add(1);
        sendAll(client, buildHttpErrorResponse(403, "该 IP 已被拒绝访问"));
        server.stats.activeRequests.fetch_sub(1);
        getSocketApi().close(client);
        return;
    }

    if (server.requireKey && !hasApiKey(headers, server.apiKey)) {
        server.stats.blockedRequests.fetch_add(1);
        sendAll(client, buildHttpErrorResponse(401, "API Key 无效或缺失"));
        server.stats.activeRequests.fetch_sub(1);
        getSocketApi().close(client);
        return;
    }

    // 连接上游
    const SocketApi& api = getSocketApi();
    SocketHandle upstream = api.createTcp();
    if (upstream == kInvalidSocket ||
        !api.connect(upstream, "127.0.0.1", server.upstreamPort)) {
        if (upstream != kInvalidSocket) api.close(upstream);
        sendAll(client, buildHttpErrorResponse(502,
            "无法连接本机 Ollama 服务（127.0.0.1:" +
            std::to_string(server.upstreamPort) + "）。请先启动 Ollama。"));
        server.stats.activeRequests.fetch_sub(1);
        api.close(client);
        return;
    }
    api.setTimeout(upstream, 300000);

    std::string request = rebuildRequest(firstLine, headers, body, server.upstreamPort);
    if (!sendAll(upstream, request)) {
        sendAll(client, buildHttpErrorResponse(502, "发送请求到 Ollama 失败"));
        api.close(upstream);
        server.stats.activeRequests.fetch_sub(1);
        api.close(client);
        return;
    }

    char buf[256 * 1024];
    std::string scanBuf;
    for (;;) {
        int n = api.recv(upstream, buf, sizeof(buf));
        if (n <= 0) break;
        server.stats.bytesDown.fetch_add(static_cast<unsigned long long>(n));
        if (!sendAll(client, std::string(buf, buf + n))) break;
        scanBuf.append(buf, buf + n);
        if (scanBuf.size() > 128 * 1024)
            scanBuf.erase(0, scanBuf.size() - 4096);
        scanTokenCounts(scanBuf, server.stats);
    }

    api.close(upstream);
    api.close(client);
    server.stats.activeRequests.fetch_sub(1);
}

} // namespace

bool startProxyServer(ProxyServer& server, unsigned short port, std::string& error) {
    if (server.running) { error = "服务端已在运行"; return false; }
    const SocketApi& api = getSocketApi();

    server.listener = api.createTcp();
    if (server.listener == kInvalidSocket) { error = "创建监听 Socket 失败"; return false; }
    if (!api.bindListen(server.listener, port)) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "监听端口 %u 失败", static_cast<unsigned>(port));
        error = buf;
        api.close(server.listener);
        server.listener = kInvalidSocket;
        return false;
    }
    server.port = port;
    server.running = true;

    std::thread([&server]() {
        const SocketApi& api = getSocketApi();
        while (server.running) {
            SocketHandle client = api.accept(server.listener);
            if (client == kInvalidSocket) {
                if (!server.running) break;
                continue;
            }
            api.setTimeout(client, 300000);
            std::thread(handleClient, client, std::ref(server)).detach();
        }
        api.close(server.listener);
        server.listener = kInvalidSocket;
    }).detach();
    return true;
}

void stopProxyServer(ProxyServer& server) {
    server.running = false;
    if (server.listener != kInvalidSocket) {
        getSocketApi().close(server.listener);
        server.listener = kInvalidSocket;
    }
}

} // namespace opg