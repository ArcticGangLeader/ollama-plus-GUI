#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace opg {

struct HttpResponse {
    long status = 0;
    std::string body;     // UTF-8
    std::string error;    // UTF-8；非空表示网络/协议层失败
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

using HeaderList = std::vector<std::pair<std::string, std::string>>;

// 平台无关的 HTTP 客户端接口
class HttpClient {
public:
    virtual ~HttpClient() = default;

    // 同步请求；body 为空时不发送请求体
    virtual HttpResponse request(const std::string& method,
                                 const std::string& url,
                                 const std::string& body,
                                 const std::string& contentType,
                                 const HeaderList& extraHeaders) = 0;

    // 同步下载到文件；progress 返回 false 表示取消
    virtual bool download(const std::string& url,
                          const std::string& destinationPath,
                          const std::function<bool(uint64_t downloaded, uint64_t total)>& progress,
                          std::string& error) = 0;
};

// 由平台层提供实现
std::unique_ptr<HttpClient> createPlatformHttpClient();

} // namespace opg