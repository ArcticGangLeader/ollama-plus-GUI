#include "core/http_client.hpp"

#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstring>

namespace opg {
namespace {

std::string nsStringToStd(NSString* s) {
    if (!s) return {};
    const char* utf8 = [s UTF8String];
    return utf8 ? std::string(utf8) : std::string();
}

NSString* stdToNsString(const std::string& s) {
    NSString* ns = [NSString stringWithUTF8String:s.c_str()];
    return ns ? ns : @"";
}

class MacHttpClient : public HttpClient {
public:
    HttpResponse request(const std::string& method,
                         const std::string& url,
                         const std::string& body,
                         const std::string& contentType,
                         const HeaderList& extraHeaders) override {
        HttpResponse result;
        @autoreleasepool {
            NSURL* nsUrl = [NSURL URLWithString:stdToNsString(url)];
            if (!nsUrl) { result.error = "URL 无效: " + url; return result; }

            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsUrl];
            req.HTTPMethod = stdToNsString(method);
            if (!contentType.empty())
                [req setValue:stdToNsString(contentType) forHTTPHeaderField:@"Content-Type"];
            [req setValue:@"zh_CN" forHTTPHeaderField:@"x-modelscope-accept-language"];
            [req setValue:@"Mozilla/5.0 (Macintosh) OllamaPlusGUI/1.0"
                forHTTPHeaderField:@"User-Agent"];
            for (const auto& h : extraHeaders)
                [req setValue:stdToNsString(h.second)
                    forHTTPHeaderField:stdToNsString(h.first)];
            if (!body.empty()) {
                req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
                [req setValue:[NSString stringWithFormat:@"%zu", body.size()]
                    forHTTPHeaderField:@"Content-Length"];
            }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSURLResponse* response = nil;
            NSError* netErr = nil;
            NSData* data = [NSURLConnection sendSynchronousRequest:req
                                                 returningResponse:&response
                                                             error:&netErr];
#pragma clang diagnostic pop

            // 即使有网络错误，也把已拿到的 status / body 保留下来
            // （401 等场景 NSURLConnection 可能返回 error 但 response 有效）
            if (response)
                result.status = (long)[(NSHTTPURLResponse*)response statusCode];
            if (data)
                result.body.assign((const char*)data.bytes, data.length);
            if (netErr)
                result.error = nsStringToStd(netErr.localizedDescription);
        }
        return result;
    }

    bool download(const std::string& url,
                  const std::string& destinationPath,
                  const std::function<bool(uint64_t, uint64_t)>& progress,
                  std::string& error) override {
        @autoreleasepool {
            NSURL* nsUrl = [NSURL URLWithString:stdToNsString(url)];
            if (!nsUrl) { error = "URL 无效"; return false; }

            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsUrl];
            [req setValue:@"Mozilla/5.0 (Macintosh) OllamaPlusGUI/1.0"
                forHTTPHeaderField:@"User-Agent"];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSURLResponse* response = nil;
            NSError* netErr = nil;
            NSData* data = [NSURLConnection sendSynchronousRequest:req
                                                 returningResponse:&response
                                                             error:&netErr];
#pragma clang diagnostic pop

            if (netErr) { error = nsStringToStd(netErr.localizedDescription); return false; }

            NSHTTPURLResponse* httpResp = (NSHTTPURLResponse*)response;
            long status = httpResp ? (long)httpResp.statusCode : 0;
            if (status < 200 || status >= 300) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "下载失败，HTTP %ld", status);
                error = buf;
                return false;
            }
            if (!data) { error = "响应为空"; return false; }

            uint64_t total = (uint64_t)data.length;
            if (progress && !progress(0, total)) { error = "下载已取消"; return false; }

            FILE* fp = std::fopen(destinationPath.c_str(), "wb");
            if (!fp) { error = "无法创建目标文件"; return false; }

            const NSUInteger chunk = 1024 * 1024;
            const char* bytes = (const char*)data.bytes;
            uint64_t written = 0;
            bool ok = true;
            for (NSUInteger offset = 0; offset < data.length; offset += chunk) {
                NSUInteger n = MIN(chunk, data.length - offset);
                size_t w = std::fwrite(bytes + offset, 1, n, fp);
                if (w != n) { error = "写入目标文件失败"; ok = false; break; }
                written += w;
                if (progress && !progress(written, total)) {
                    error = "下载已取消";
                    ok = false;
                    break;
                }
            }
            std::fclose(fp);
            if (!ok) {
                std::remove(destinationPath.c_str());
                return false;
            }
            return true;
        }
    }
};

} // namespace

std::unique_ptr<HttpClient> createPlatformHttpClient() {
    return std::unique_ptr<HttpClient>(new MacHttpClient());
}

} // namespace opg