#pragma once

#include <string>
#include <vector>

namespace opg {

struct SearchResult {
    std::string owner;
    std::string repository;
    std::string revision;
    long long size = 0;
    int stars = 0;
    bool gguf = false;
    long long estimatedSize = 0;
};

// 搜索魔搭社区中的 GGUF 模型。返回结果列表；error 非空表示失败。
std::vector<SearchResult> searchModelScope(const std::string& query, std::string& error);

// 拉取某个模型的 GGUF 文件列表（递归目录）
struct GgufFile {
    std::string path;
    std::string name;
    long long size = 0;
};

std::vector<GgufFile> fetchModelFiles(const SearchResult& model, std::string& error);

} // namespace opg