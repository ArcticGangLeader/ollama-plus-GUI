#pragma once

#include <string>

namespace opg {

inline std::string urlEncode(const std::string& value, bool keepSlash = false) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else if (keepSlash && c == '/') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[(c >> 4) & 15]);
            result.push_back(hex[c & 15]);
        }
    }
    return result;
}

inline std::string buildDownloadUrl(const std::string& owner,
                                    const std::string& repository,
                                    const std::string& revision,
                                    const std::string& filePath) {
    return "https://www.modelscope.cn/models/" + urlEncode(owner, false) + "/" +
           urlEncode(repository, false) + "/resolve/" + urlEncode(revision, false) +
           "/" + urlEncode(filePath, false);
}

} // namespace opg