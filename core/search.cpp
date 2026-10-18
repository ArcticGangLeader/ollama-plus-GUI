#include "core/search.hpp"

#include "core/http_client.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "core/url.hpp"
#include "core/format.hpp"

#include <algorithm>
#include <cstring>

namespace opg {
namespace {

// 从 URL 或路径取出扩展名（小写）
std::string extensionLower(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    return lowerAscii(name.substr(dot));
}

void appendFilesFromJson(const JsonValue& root, std::vector<GgufFile>& files) {
    const JsonValue* list = root.path({"Data", "Files"});
    if (!list || !list->isArray()) return;
    for (const JsonValue& item : list->array) {
        const JsonValue* type = item.get("Type");
        const JsonValue* path = item.get("Path");
        if (!path) continue;
        std::string typeStr = type ? type->asString() : "";
        std::string pathStr = path->asString();
        if (typeStr == "tree" || pathStr.empty()) continue;
        if (extensionLower(pathStr) != ".gguf") continue;
        GgufFile f;
        f.path = pathStr;
        const JsonValue* name = item.get("Name");
        f.name = name ? name->asString() : pathStr;
        const JsonValue* size = item.get("Size");
        f.size = size ? size->asInteger() : 0;
        files.push_back(std::move(f));
    }
}

bool fetchFilesRecursive(HttpClient& http,
                         const std::string& owner,
                         const std::string& repository,
                         const std::string& revision,
                         const std::string& root,
                         std::vector<GgufFile>& files,
                         std::string& error) {
    std::string url = "https://www.modelscope.cn/api/v1/models/" +
                      urlEncode(owner, false) + "/" +
                      urlEncode(repository, false) +
                      "/repo/files?Revision=" + urlEncode(revision, false) +
                      "&Root=" + urlEncode(root, false);
    HttpResponse resp = http.request("GET", url, "", "", {});
    if (!resp.ok()) {
        error = resp.error.empty() ? "读取模型文件列表失败" : resp.error;
        return false;
    }
    JsonValue json;
    std::string parseError;
    if (!parseJson(resp.body, json, parseError)) {
        error = "解析模型文件列表失败: " + parseError;
        return false;
    }
    appendFilesFromJson(json, files);

    const JsonValue* entries = json.path({"Data", "Files"});
    if (entries && entries->isArray()) {
        for (const JsonValue& item : entries->array) {
            const JsonValue* type = item.get("Type");
            const JsonValue* path = item.get("Path");
            if (!type || !path) continue;
            if (type->asString() != "tree") continue;
            std::string sub = path->asString();
            if (sub.empty() || files.size() >= 1000) continue;
            if (!fetchFilesRecursive(http, owner, repository, revision, sub, files, error))
                return false;
        }
    }
    return true;
}

} // namespace

std::vector<SearchResult> searchModelScope(const std::string& query, std::string& error) {
    std::vector<SearchResult> results;
    auto http = createPlatformHttpClient();

    std::string jsonBody =
        "{\"PageSize\":100,\"PageNumber\":1,\"SortBy\":\"Default\",\"Target\":\"\","
        "\"SingleCriterion\":[],\"Name\":\"" + jsonEscape(query) + "\",\"Criterion\":[]}";

    HttpResponse resp = http->request(
        "PUT",
        "https://www.modelscope.cn/api/v1/dolphin/models",
        jsonBody,
        "application/json",
        {});
    if (!resp.ok()) {
        error = resp.error.empty() ? "魔搭搜索接口返回异常状态" : resp.error;
        return results;
    }
    JsonValue root;
    std::string parseError;
    if (!parseJson(resp.body, root, parseError)) {
        error = "解析魔搭搜索结果失败: " + parseError;
        return results;
    }
    const JsonValue* models = root.path({"Data", "Model", "Models"});
    if (!models || !models->isArray()) {
        error = "魔搭搜索结果格式不符合预期";
        return results;
    }
    for (const JsonValue& item : models->array) {
        const JsonValue* name = item.get("Name");
        const JsonValue* owner = item.get("Path");
        if (!name) continue;
        const JsonValue* libs = item.get("Libraries");
        bool isGguf = libs && libs->containsString("gguf");
        std::string lowerName = lowerAscii(name->asString());
        if (!isGguf && lowerName.find("gguf") == std::string::npos) continue;

        SearchResult m;
        m.repository = name->asString();
        m.owner = owner ? owner->asString() : "modelscope";
        const JsonValue* rev = item.get("Revision");
        m.revision = rev ? rev->asString() : "master";
        const JsonValue* size = item.get("StorageSize");
        m.size = size ? size->asInteger() : 0;
        const JsonValue* modelInfo = item.path({"ModelInfos", "gguf", "model_size"});
        m.estimatedSize = modelInfo ? modelInfo->asInteger() : m.size;
        const JsonValue* stars = item.get("Stars");
        m.stars = stars ? stars->asInt() : 0;
        m.gguf = true;
        results.push_back(std::move(m));
    }
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  if (a.stars != b.stars) return a.stars > b.stars;
                  return a.repository < b.repository;
              });
    if (results.empty()) error = "没有找到 GGUF 格式的模型";
    return results;
}

std::vector<GgufFile> fetchModelFiles(const SearchResult& model, std::string& error) {
    std::vector<GgufFile> files;
    auto http = createPlatformHttpClient();
    fetchFilesRecursive(*http, model.owner, model.repository, model.revision,
                        "", files, error);
    std::sort(files.begin(), files.end(),
              [](const GgufFile& a, const GgufFile& b) {
                  if (a.size != b.size) return a.size < b.size;
                  return a.path < b.path;
              });
    return files;
}

} // namespace opg