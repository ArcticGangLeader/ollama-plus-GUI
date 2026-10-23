#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace opg {

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::wstring error;
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

inline std::wstring lastWinHttpError(const wchar_t* action) {
    DWORD code = GetLastError();
    wchar_t buffer[512];
    swprintf(buffer, 512, L"%s 失败（WinHTTP 错误 %lu）", action, static_cast<unsigned long>(code));
    return buffer;
}

inline void splitUrl(const std::wstring& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& secure) {
    std::wstring value = url;
    secure = false;
    port = INTERNET_DEFAULT_HTTP_PORT;
    if (value.rfind(L"https://", 0) == 0) {
        secure = true;
        port = INTERNET_DEFAULT_HTTPS_PORT;
        value = value.substr(8);
    } else if (value.rfind(L"http://", 0) == 0) {
        value = value.substr(7);
    }
    size_t slash = value.find(L'/');
    host = slash == std::wstring::npos ? value : value.substr(0, slash);
    path = slash == std::wstring::npos ? L"/" : value.substr(slash);
    size_t colon = host.find(L':');
    if (colon != std::wstring::npos) {
        port = static_cast<INTERNET_PORT>(wcstoul(host.substr(colon + 1).c_str(), nullptr, 10));
        host = host.substr(0, colon);
    }
}

inline std::wstring urlEncode(const std::wstring& value, bool keepSlash = false) {
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring result;
    for (wchar_t c : value) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') ||
            c == L'-' || c == L'_' || c == L'.' || c == L'~') {
            result.push_back(c);
        } else if (keepSlash && c == L'/') {
            result.push_back(c);
        } else {
            unsigned short code = static_cast<unsigned short>(c);
            if (code < 0x80) {
                result.push_back(L'%');
                result.push_back(hex[(code >> 4) & 15]);
                result.push_back(hex[code & 15]);
            } else {
                char utf8[8];
                int length = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr, nullptr);
                for (int i = 0; i < length; ++i) {
                    unsigned char byte = static_cast<unsigned char>(utf8[i]);
                    result.push_back(L'%');
                    result.push_back(hex[(byte >> 4) & 15]);
                    result.push_back(hex[byte & 15]);
                }
            }
        }
    }
    return result;
}

inline std::wstring buildDownloadUrl(const std::wstring& owner, const std::wstring& repository,
                                     const std::wstring& revision, const std::wstring& filePath) {
    std::wstring path = urlEncode(filePath, false);
    std::wstring rev = urlEncode(revision, false);
    return L"https://www.modelscope.cn/models/" + urlEncode(owner, false) + L"/" +
           urlEncode(repository, false) + L"/resolve/" + rev + L"/" + path;
}

class HttpHandle {
public:
    HttpHandle() = default;
    explicit HttpHandle(HINTERNET value) : value_(value) {}
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;

    void reset(HINTERNET value) {
        if (value_) WinHttpCloseHandle(value_);
        value_ = value;
    }
    HINTERNET get() const { return value_; }
    operator bool() const { return value_ != nullptr; }
    ~HttpHandle() { reset(nullptr); }

private:
    HINTERNET value_ = nullptr;
};

inline HttpResponse httpRequest(const std::wstring& method, const std::wstring& url,
                                const std::string& body = {}, const wchar_t* contentType = nullptr,
                                const std::vector<std::pair<std::wstring, std::wstring>>& extraHeaders = {}) {
    HttpResponse response;
    std::wstring host, path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    bool secure = false;
    splitUrl(url, host, path, port, secure);

    HttpHandle session(WinHttpOpen(L"OllamaPlusGUI/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        response.error = lastWinHttpError(L"初始化网络会话");
        return response;
    }
    HttpHandle connect(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!connect) {
        response.error = lastWinHttpError(L"连接服务器");
        return response;
    }
    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HttpHandle request(WinHttpOpenRequest(connect.get(), method.c_str(), path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        response.error = lastWinHttpError(L"创建请求");
        return response;
    }

    std::wstring headers;
    headers += L"Accept: application/json\r\n";
    headers += L"x-modelscope-accept-language: zh_CN\r\n";
    headers += L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) OllamaPlusGUI/1.0\r\n";
    if (contentType) headers += std::wstring(L"Content-Type: ") + contentType + L"\r\n";
    for (const auto& item : extraHeaders) {
        headers += item.first + L": " + item.second + L"\r\n";
    }
    if (!WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        response.error = lastWinHttpError(L"添加请求头");
        return response;
    }

    const void* payload = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data();
    DWORD payloadSize = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, static_cast<DWORD>(-1),
                            const_cast<void*>(payload), payloadSize, payloadSize, 0)) {
        response.error = lastWinHttpError(L"发送请求");
        return response;
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        response.error = lastWinHttpError(L"接收响应");
        return response;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX)) {
        response.error = lastWinHttpError(L"读取 HTTP 状态");
        return response;
    }
    response.status = statusCode;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            response.error = lastWinHttpError(L"查询响应数据");
            return response;
        }
        if (available == 0) break;
        size_t oldSize = response.body.size();
        response.body.resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response.body.data() + oldSize, available, &read)) {
            response.error = lastWinHttpError(L"读取响应数据");
            return response;
        }
        response.body.resize(oldSize + read);
        if (read == 0) break;
    }
    return response;
}

inline bool httpDownloadFile(const std::wstring& url, const std::wstring& destination,
                             const std::function<bool(unsigned long long, unsigned long long)>& progress,
                             std::wstring& error) {
    std::wstring host, path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    bool secure = false;
    splitUrl(url, host, path, port, secure);

    HttpHandle session(WinHttpOpen(L"OllamaPlusGUI/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        error = lastWinHttpError(L"初始化网络会话");
        return false;
    }
    DWORD timeout = 15000;
    WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, 0);

    HttpHandle connect(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!connect) {
        error = lastWinHttpError(L"连接下载服务器");
        return false;
    }
    HttpHandle request(WinHttpOpenRequest(connect.get(), L"GET", path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        error = lastWinHttpError(L"创建下载请求");
        return false;
    }
    std::wstring headers = L"Accept: application/octet-stream, */*\r\nUser-Agent: OllamaPlusGUI/1.0\r\n";
    if (!WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        error = lastWinHttpError(L"开始下载");
        return false;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX)) {
        error = lastWinHttpError(L"读取下载状态");
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        wchar_t buffer[128];
        swprintf(buffer, 128, L"下载失败，HTTP 状态码 %lu", static_cast<unsigned long>(statusCode));
        error = buffer;
        return false;
    }

    unsigned long long total = 0;
    wchar_t contentLength[64] = {};
    DWORD lengthSize = sizeof(contentLength);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            contentLength, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
        total = wcstoull(contentLength, nullptr, 10);
    }

    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wchar_t buffer[256];
        swprintf(buffer, 256, L"创建目标文件失败（Windows 错误 %lu）", static_cast<unsigned long>(GetLastError()));
        error = buffer;
        return false;
    }

    std::vector<unsigned char> buffer(1024 * 1024);
    unsigned long long downloaded = 0;
    bool succeeded = true;
    bool canceled = false;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            error = lastWinHttpError(L"查询下载数据");
            succeeded = false;
            break;
        }
        if (available == 0) break;
        DWORD toRead = static_cast<DWORD>((std::min)(static_cast<size_t>(available), buffer.size()));
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), toRead, &read)) {
            error = lastWinHttpError(L"读取下载数据");
            succeeded = false;
            break;
        }
        if (read == 0) break;
        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
            wchar_t buffer2[256];
            swprintf(buffer2, 256, L"写入目标文件失败（Windows 错误 %lu）", static_cast<unsigned long>(GetLastError()));
            error = buffer2;
            succeeded = false;
            break;
        }
        downloaded += read;
        if (progress && !progress(downloaded, total)) {
            canceled = true;
            succeeded = false;
            break;
        }
    }
    CloseHandle(file);
    if (!succeeded) {
        DeleteFileW(destination.c_str());
        if (canceled) error = L"下载已取消。";
        return false;
    }
    if (total != 0 && downloaded != total) {
        wchar_t buffer[256];
        swprintf(buffer, 256, L"下载不完整：已接收 %llu / %llu 字节", downloaded, total);
        error = buffer;
        DeleteFileW(destination.c_str());
        return false;
    }
    return true;
}


struct StreamResponse {
    DWORD status = 0;
    std::string body;
    std::wstring error;
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

inline StreamResponse httpStreamRequest(const std::wstring& method, const std::wstring& url,
                                        const std::string& body, const wchar_t* contentType,
                                        const std::function<bool(const std::string&)>& onChunk) {
    StreamResponse response;
    std::wstring host, path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    bool secure = false;
    splitUrl(url, host, path, port, secure);

    HttpHandle session(WinHttpOpen(L"OllamaPlusGUI/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { response.error = lastWinHttpError(L"初始化网络会话"); return response; }
    HttpHandle connect(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!connect) { response.error = lastWinHttpError(L"连接 Ollama 服务"); return response; }
    HttpHandle request(WinHttpOpenRequest(connect.get(), method.c_str(), path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) { response.error = lastWinHttpError(L"创建 Ollama 请求"); return response; }

    std::wstring headers = L"Accept: application/x-ndjson, application/json\r\n";
    if (contentType) headers += std::wstring(L"Content-Type: ") + contentType + L"\r\n";
    if (!WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, static_cast<DWORD>(-1),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        response.error = lastWinHttpError(L"发送 Ollama 请求");
        return response;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX)) {
        response.error = lastWinHttpError(L"读取 Ollama 响应状态");
        return response;
    }
    response.status = statusCode;

    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            response.error = lastWinHttpError(L"查询 Ollama 响应数据");
            return response;
        }
        if (available == 0) break;
        DWORD toRead = static_cast<DWORD>((std::min)(static_cast<size_t>(available), buffer.size()));
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), toRead, &read)) {
            response.error = lastWinHttpError(L"读取 Ollama 响应数据");
            return response;
        }
        if (read == 0) break;
        std::string chunk(buffer.data(), buffer.data() + read);
        if (response.status >= 200 && response.status < 300) {
            if (onChunk && !onChunk(chunk)) {
                response.error = L"请求已取消。";
                return response;
            }
        } else {
            response.body += chunk;
        }
    }
    return response;
}
} // namespace opg





