#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace opg {

// 从 headers 块里查找某个头字段（不区分大小写）
inline std::string headerValue(const std::string& headers, const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string lowerHeaders = headers;
    std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    size_t pos = 0;
    while ((pos = lowerHeaders.find(lowerName + ":", pos)) != std::string::npos) {
        // 必须是行首
        size_t lineStart = lowerHeaders.rfind('\n', pos);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        if (lineStart != pos) { pos += lowerName.size() + 1; continue; }

        size_t valueStart = pos + name.size() + 1;
        while (valueStart < headers.size() &&
               (headers[valueStart] == ' ' || headers[valueStart] == '\t')) ++valueStart;
        size_t valueEnd = headers.find("\r\n", valueStart);
        if (valueEnd == std::string::npos) valueEnd = headers.size();
        std::string value = headers.substr(valueStart, valueEnd - valueStart);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        return value;
    }
    return {};
}

// 判断头里是否含有效的 API Key
inline bool hasApiKey(const std::string& headers, const std::string& expected) {
    std::string auth = headerValue(headers, "Authorization");
    if (auth.rfind("Bearer ", 0) == 0) auth = auth.substr(7);
    while (!auth.empty() && auth.front() == ' ') auth.erase(auth.begin());
    if (auth == expected) return true;

    std::string xKey = headerValue(headers, "x-api-key");
    while (!xKey.empty() && xKey.front() == ' ') xKey.erase(xKey.begin());
    if (xKey == expected) return true;

    std::string apiKey = headerValue(headers, "api-key");
    while (!apiKey.empty() && apiKey.front() == ' ') apiKey.erase(apiKey.begin());
    return apiKey == expected;
}

// 重建转发给上游 Ollama 的请求
inline std::string rebuildRequest(const std::string& firstLine,
                                  const std::string& headers,
                                  const std::string& body,
                                  unsigned short upstreamPort) {
    std::string result = firstLine + "\r\n";
    size_t start = 0;
    while (start < headers.size()) {
        size_t end = headers.find("\r\n", start);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(start, end - start);
        std::string lowerLine = line;
        std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerLine.rfind("host:", 0) != 0 &&
            lowerLine.rfind("connection:", 0) != 0 &&
            lowerLine.rfind("content-length:", 0) != 0) {
            result += line + "\r\n";
        }
        start = end + 2;
    }
    result += "Host: 127.0.0.1:" + std::to_string(upstreamPort) + "\r\n";
    result += "Connection: close\r\n";
    result += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    result += body;
    return result;
}

// 构造 HTTP 错误响应体
inline std::string buildHttpErrorResponse(unsigned status, const std::string& message) {
    // message 已是 UTF-8
    std::string body = "{\"error\":{\"message\":\"" + message + "\"}}";
    std::string response;
    response += "HTTP/1.1 " + std::to_string(status) + " Error\r\n";
    response += "Content-Type: application/json; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

} // namespace opg