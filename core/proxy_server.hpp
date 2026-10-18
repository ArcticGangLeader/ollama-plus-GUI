#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/socket.hpp"
#include "core/proxy_protocol.hpp"

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
    SocketHandle listener = kInvalidSocket;
    unsigned short port = 11435;
    unsigned short upstreamPort = 11434;
    bool requireKey = false;
    std::string apiKey;
    ProxyStats stats;

    bool isBlocked(const std::string& ip);
    void setBlocked(const std::vector<std::string>& ips);
    std::vector<std::string> blockedList();
    bool addBlocked(const std::string& ip);
    bool removeBlocked(const std::string& ip);

private:
    std::mutex mutex_;
    std::set<std::string> blockedIps_;
};

bool startProxyServer(ProxyServer& server, unsigned short port, std::string& error);
void stopProxyServer(ProxyServer& server);

} // namespace opg