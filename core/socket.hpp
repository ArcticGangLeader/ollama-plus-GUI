#pragma once

#include <string>

namespace opg {

// 各平台 socket 句柄类型不同：
//   Windows: SOCKET (UINT_PTR)
//   POSIX:   int
// 统一用 void* 指针封装，由平台层提供实际实现
using SocketHandle = void*;
const SocketHandle kInvalidSocket = nullptr;

// 平台层需实现下面这些函数（跨平台签名一致）
//   SocketHandle socketCreateTcp();
//   void socketClose(SocketHandle s);
//   bool socketBindListen(SocketHandle s, unsigned short port);
//   SocketHandle socketAccept(SocketHandle listener);
//   bool socketConnect(SocketHandle s, const char* host, unsigned short port);
//   int socketSend(SocketHandle s, const char* data, int size);
//   int socketRecv(SocketHandle s, char* data, int size);
//   void socketSetTimeout(SocketHandle s, int milliseconds);
//   std::string socketPeerIp(SocketHandle s);

struct SocketApi {
    SocketHandle (*createTcp)();
    void (*close)(SocketHandle);
    bool (*bindListen)(SocketHandle, unsigned short);
    SocketHandle (*accept)(SocketHandle);
    bool (*connect)(SocketHandle, const char* host, unsigned short);
    int (*send)(SocketHandle, const char*, int);
    int (*recv)(SocketHandle, char*, int);
    void (*setTimeout)(SocketHandle, int);
    std::string (*peerIp)(SocketHandle);
};

// 平台层实现，返回对应平台的操作集
const SocketApi& getSocketApi();

} // namespace opg