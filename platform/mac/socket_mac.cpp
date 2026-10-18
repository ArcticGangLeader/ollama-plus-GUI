#include "core/socket.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <string>

namespace opg {
namespace {

SocketHandle macCreateTcp() {
    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return kInvalidSocket;
    return reinterpret_cast<SocketHandle>(static_cast<intptr_t>(fd));
}

void macClose(SocketHandle s) {
    if (s == kInvalidSocket) return;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

bool macBindListen(SocketHandle s, unsigned short port) {
    if (s == kInvalidSocket) return false;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    if (::listen(fd, 64) != 0) return false;
    return true;
}

SocketHandle macAccept(SocketHandle listener) {
    if (listener == kInvalidSocket) return kInvalidSocket;
    int lfd = static_cast<int>(reinterpret_cast<intptr_t>(listener));
    int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) return kInvalidSocket;
    return reinterpret_cast<SocketHandle>(static_cast<intptr_t>(cfd));
}

bool macConnect(SocketHandle s, const char* host, unsigned short port) {
    if (s == kInvalidSocket) return false;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) return false;
    return ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

int macSend(SocketHandle s, const char* data, int size) {
    if (s == kInvalidSocket) return -1;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    return static_cast<int>(::send(fd, data, static_cast<size_t>(size), 0));
}

int macRecv(SocketHandle s, char* data, int size) {
    if (s == kInvalidSocket) return -1;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    return static_cast<int>(::recv(fd, data, static_cast<size_t>(size), 0));
}

void macSetTimeout(SocketHandle s, int ms) {
    if (s == kInvalidSocket) return;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    struct timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

std::string macPeerIp(SocketHandle s) {
    if (s == kInvalidSocket) return "unknown";
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(s));
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return "unknown";
    char buf[INET6_ADDRSTRLEN]{};
    if (ss.ss_family == AF_INET) {
        auto* a = reinterpret_cast<sockaddr_in*>(&ss);
        ::inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
    } else if (ss.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        ::inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
    } else {
        return "unknown";
    }
    return buf;
}

const SocketApi kMacApi = {
    macCreateTcp, macClose, macBindListen, macAccept,
    macConnect, macSend, macRecv, macSetTimeout, macPeerIp,
};

} // namespace

const SocketApi& getSocketApi() { return kMacApi; }

} // namespace opg