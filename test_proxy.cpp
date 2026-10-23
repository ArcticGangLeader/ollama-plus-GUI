#include "proxy.hpp"
#include <cstdio>

int main() {
    opg::ProxyServer server;
    server.apiKey = L"test-key";
    server.requireKey = false;
    std::wstring error;
    if (!opg::startProxyServer(server, 12435, error)) {
        printf("START_FAIL\n");
        printf("%ls\n", error.c_str());
        return 1;
    }
    server.addBlocked(L"127.0.0.2");
    printf("START_OK\n");
    fflush(stdout);
    Sleep(15000);
    opg::stopProxyServer(server);
    return 0;
}



