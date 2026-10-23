#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "http.hpp"
#include "process.hpp"
#include "proxy.hpp"

namespace fs = std::filesystem;
using opg::JsonValue;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* kClassName = L"OllamaPlusGuiWindow";
static const char* kAppVersion = "0.1.0";
static const char* kBuildDate = __DATE__ " " __TIME__;

enum class Page { Search = 0, Models, Service, Settings };

struct SearchResult {
    std::wstring owner;
    std::wstring repository;
    std::wstring revision;
    long long size = 0;
    long long estimatedSize = 0;
    int stars = 0;
    bool gguf = false;
};

struct GgufFile {
    std::wstring path;
    std::wstring name;
    long long size = 0;
};

struct LocalModel {
    std::wstring name;
    std::wstring id;
    std::wstring size;
    std::wstring modified;
};

struct ChatMessage {
    std::string role;
    std::string sender;
    std::string content;
    std::string thinking;
};

struct ChatAttachment {
    std::wstring path;
    std::string name;
    bool image = false;
    std::wstring text;
    std::string base64;
    long long size = 0;
};

struct Settings {
    std::string systemPrompt;
    float temperature = 0.70f;
    float topP = 0.90f;
    int topK = 40;
    float repeatPenalty = 1.10f;
    int numCtx = 4096;
    int seed = -1;
    bool stream = true;
    bool think = false;
    bool jsonMode = false;
    std::string keepAlive = "5m";
};

struct App {
    std::recursive_mutex stateMutex;
    Page page = Page::Search;
    int theme = 0;
    std::string status = "就绪。";

    char searchQuery[256] = "";
    std::vector<SearchResult> searchResults;
    int selectedSearch = -1;
    std::string searchStatus = "输入关键词后搜索。";
    std::atomic<bool> searchBusy{false};

    SearchResult selectedModelScope;
    std::vector<GgufFile> ggufFiles;
    int selectedGguf = -1;
    std::string filesStatus;
    std::atomic<bool> filesBusy{false};
    bool fileModalOpen = false;
    bool fileModalRequested = false;
    char downloadName[160] = "";
    std::wstring downloadTarget;
    std::atomic<bool> downloadBusy{false};
    std::atomic<bool> downloadCancel{false};
    float downloadProgress = 0.0f;
    unsigned long long downloadCompleted = 0;
    unsigned long long downloadTotal = 0;
    std::string downloadStatus;

    char importName[160] = "";
    std::wstring importPath;
    bool importModalOpen = false;
    bool importModalRequested = false;
    std::atomic<bool> importBusy{false};

    std::vector<LocalModel> localModels;
    int selectedModel = -1;
    std::string modelsStatus = "未加载。";
    std::atomic<bool> modelsBusy{false};
    bool renameModalOpen = false;
    bool renameModalRequested = false;
    char renameName[160] = "";
    std::wstring renameOld;
    bool deleteModalOpen = false;
    bool deleteModalRequested = false;
    std::wstring deleteName;

    bool chatOpen = false;
    bool chatRequested = false;
    std::string chatModel;
    char chatInput[16384] = "";
    std::vector<ChatMessage> chatMessages;
    std::vector<ChatAttachment> chatAttachments;
    std::string chatStatus = "就绪。";
    std::atomic<bool> chatBusy{false};
    std::atomic<bool> chatStop{false};

    char serverPortText[16] = "11435";
    char apiKey[256] = "";
    bool requireKey = false;
    bool serverRunning = false;
    std::string serverStatus = "服务端已关闭。";
    std::vector<std::string> blockedIps;
    char blockedIp[64] = "";
    std::string firewallStatus = "未操作。";
    std::atomic<bool> firewallBusy{false};

    std::string ollamaInfo = "未读取。";
    std::atomic<bool> infoBusy{false};
    char commandInput[256] = "";
    char keepAlive[64] = "5m";
    std::string commandOutput;
    std::atomic<bool> commandBusy{false};

    std::string errorTitle;
    std::string errorText;
    bool errorOpen = false;
    bool errorRequested = false;
    bool confirmDeleteOpen = false;
    bool confirmDeleteRequested = false;

    Settings settings;
};

static HWND g_hwnd = nullptr;
static App g;
static opg::ProxyServer g_proxy;

static std::string utf8(const std::wstring& value) { return opg::wideToUtf8(value); }
static std::wstring wide(const std::string& value) { return opg::utf8ToWide(value); }

static std::string trimCopy(const std::string& value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

static std::wstring trimWide(const std::wstring& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && iswspace(value[begin])) ++begin;
    while (end > begin && iswspace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

static std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

static std::string formatBytes(long long value) {
    wchar_t buffer[64];
    if (value < 1024) {
        swprintf(buffer, 64, L"%lld B", value);
    } else {
        double size = static_cast<double>(value);
        const wchar_t* units[] = {L"KiB", L"MiB", L"GiB", L"TiB"};
        int unit = -1;
        while (size >= 1024.0 && unit < 3) {
            size /= 1024.0;
            ++unit;
        }
        swprintf(buffer, 64, L"%.2f %s", size, units[std::max(unit, 0)]);
    }
    return utf8(buffer);
}

static std::string fileNameUtf8(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return utf8(pos == std::wstring::npos ? path : path.substr(pos + 1));
}

static std::wstring fileNameWide(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

static std::wstring fileDirectory(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

static std::wstring fileExtension(const std::wstring& path) {
    std::wstring name = fileNameWide(path);
    size_t pos = name.rfind(L'.');
    return pos == std::wstring::npos ? L"" : lowerWide(name.substr(pos));
}

static std::string jsonEscapeUtf8(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '\"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    result += buffer;
                } else {
                    result.push_back(static_cast<char>(c));
                }
        }
    }
    return result;
}

static void setStatus(const std::string& text) {
    std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
    g.status = text;
}

static void showError(const std::string& title, const std::string& text) {
    std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
    g.errorTitle = title;
    g.errorText = text;
    g.errorOpen = true;
    g.errorRequested = true;
}

static std::wstring sanitizeModelName(const std::wstring& input) {
    std::wstring result;
    bool lastDash = false;
    for (wchar_t c : input) {
        if (iswalnum(c)) {
            result.push_back(towlower(c));
            lastDash = false;
        } else if (!lastDash && !result.empty()) {
            result.push_back(L'-');
            lastDash = true;
        }
    }
    while (!result.empty() && (result.back() == L'-' || result.back() == L'.')) result.pop_back();
    if (result.empty()) result = L"imported-model";
    return result.substr(0, 100);
}

static bool findOllamaExe(std::wstring& result) {
    wchar_t exeBuffer[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exeBuffer, MAX_PATH * 2);
    std::vector<fs::path> bases;
    bases.push_back(fs::path(exeBuffer).parent_path());
    bases.push_back(fs::current_path());
    for (const auto& base : bases) {
        for (const auto& relative : {fs::path(L"ollama.exe"), fs::path(L"bin") / L"ollama.exe", fs::path(L"ollama") / L"ollama.exe"}) {
            std::error_code ec;
            fs::path candidate = base / relative;
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
                result = candidate.wstring();
                return true;
            }
        }
    }
    return false;
}

static std::wstring ollamaCommand() {
    std::wstring path;
    if (findOllamaExe(path)) return opg::quoteIfNeeded(path);
    return L"ollama";
}

static std::string ollamaLocationText() {
    std::wstring path;
    if (findOllamaExe(path)) return "本地 ollama.exe: " + utf8(path);
    return "未找到本地 ollama.exe，使用 PATH 中的 ollama 命令。";
}

static bool isUserAdmin() {
    BOOL admin = FALSE;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &admin);
        FreeSid(adminGroup);
    }
    return admin != FALSE;
}

static bool chooseOpenFile(HWND owner, const wchar_t* filter, const wchar_t* title, bool multiSelect, std::vector<std::wstring>& paths) {
    wchar_t buffer[64 * 1024]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sizeof(buffer) / sizeof(wchar_t);
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | (multiSelect ? OFN_ALLOWMULTISELECT | OFN_EXPLORER : 0);
    if (!GetOpenFileNameW(&ofn)) return false;
    std::wstring directory(buffer);
    wchar_t* cursor = buffer + directory.size() + 1;
    if (*cursor == L'\0') {
        paths.push_back(directory);
    } else {
        while (*cursor) {
            std::wstring name(cursor);
            paths.push_back((fs::path(directory) / name).wstring());
            cursor += name.size() + 1;
        }
    }
    return true;
}

static bool chooseSaveFile(HWND owner, const std::wstring& suggested, const wchar_t* title, std::wstring& path) {
    wchar_t buffer[MAX_PATH * 4]{};
    wcsncpy(buffer, suggested.c_str(), sizeof(buffer) / sizeof(wchar_t) - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"GGUF 模型文件\0*.gguf\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sizeof(buffer) / sizeof(wchar_t);
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = L"gguf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

static std::vector<SearchResult> searchModelScope(const std::wstring& query, std::wstring& error) {
    std::vector<SearchResult> results;
    std::wstring json = L"{\"PageSize\":100,\"PageNumber\":1,\"SortBy\":\"Default\",\"Target\":\"\",\"SingleCriterion\":[],\"Name\":\"" +
                        opg::jsonEscape(query) + L"\",\"Criterion\":[]}";
    opg::HttpResponse response = opg::httpRequest(L"PUT", L"https://www.modelscope.cn/api/v1/dolphin/models",
                                                  opg::wideToUtf8(json), L"application/json");
    if (!response.ok()) {
        error = response.error.empty() ? L"魔搭搜索接口返回异常状态。" : response.error;
        return results;
    }
    JsonValue root;
    std::wstring parseError;
    if (!opg::parseJson(response.body, root, parseError)) {
        error = L"解析搜索结果失败：" + parseError;
        return results;
    }
    const JsonValue* models = root.path({L"Data", L"Model", L"Models"});
    if (!models || !models->isArray()) {
        error = L"搜索结果格式不符合预期。";
        return results;
    }
    for (const JsonValue& item : models->array) {
        std::wstring name = item.get(L"Name") ? item.get(L"Name")->asString() : L"";
        std::wstring owner = item.get(L"Path") ? item.get(L"Path")->asString() : L"";
        const JsonValue* libraries = item.get(L"Libraries");
        bool isGguf = (libraries && libraries->containsString(L"gguf")) ||
                      lowerWide(name).find(L"gguf") != std::wstring::npos;
        if (!isGguf || name.empty()) continue;
        SearchResult model;
        model.repository = name;
        model.owner = owner.empty() ? L"modelscope" : owner;
        model.revision = item.get(L"Revision") ? item.get(L"Revision")->asString() : L"master";
        model.size = item.get(L"StorageSize") ? item.get(L"StorageSize")->asInteger() : 0;
        const JsonValue* modelInfo = item.path({L"ModelInfos", L"gguf", L"model_size"});
        model.estimatedSize = modelInfo ? modelInfo->asInteger() : model.size;
        model.stars = item.get(L"Stars") ? item.get(L"Stars")->asInt() : 0;
        model.gguf = true;
        results.push_back(model);
    }
    std::sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        if (a.stars != b.stars) return a.stars > b.stars;
        return a.repository < b.repository;
    });
    if (results.empty()) error = L"没有找到 GGUF 格式的模型。";
    return results;
}

static void appendFilesFromJson(const JsonValue& root, std::vector<GgufFile>& files) {
    const JsonValue* list = root.path({L"Data", L"Files"});
    if (!list || !list->isArray()) return;
    for (const JsonValue& item : list->array) {
        std::wstring type = item.get(L"Type") ? item.get(L"Type")->asString() : L"";
        std::wstring path = item.get(L"Path") ? item.get(L"Path")->asString() : L"";
        std::wstring name = item.get(L"Name") ? item.get(L"Name")->asString() : path;
        long long size = item.get(L"Size") ? item.get(L"Size")->asInteger() : 0;
        if (type == L"tree" || path.empty()) continue;
        if (fileExtension(path) == L".gguf") files.push_back({path, name, size});
    }
}

static bool fetchModelFilesRecursive(const std::wstring& owner, const std::wstring& repository,
                                     const std::wstring& revision, const std::wstring& root,
                                     std::vector<GgufFile>& files, std::wstring& error) {
    std::wstring url = L"https://www.modelscope.cn/api/v1/models/" + opg::urlEncode(owner, false) +
                       L"/" + opg::urlEncode(repository, false) + L"/repo/files?Revision=" +
                       opg::urlEncode(revision, false) + L"&Root=" + opg::urlEncode(root, false);
    opg::HttpResponse response = opg::httpRequest(L"GET", url);
    if (!response.ok()) {
        error = response.error.empty() ? L"读取模型文件列表失败。" : response.error;
        return false;
    }
    JsonValue json;
    std::wstring parseError;
    if (!opg::parseJson(response.body, json, parseError)) {
        error = L"解析文件列表失败：" + parseError;
        return false;
    }
    appendFilesFromJson(json, files);
    const JsonValue* entries = json.path({L"Data", L"Files"});
    if (entries && entries->isArray()) {
        for (const JsonValue& item : entries->array) {
            std::wstring type = item.get(L"Type") ? item.get(L"Type")->asString() : L"";
            if (type == L"tree") {
                std::wstring path = item.get(L"Path") ? item.get(L"Path")->asString() : L"";
                if (!path.empty() && files.size() < 1000) {
                    if (!fetchModelFilesRecursive(owner, repository, revision, path, files, error)) return false;
                }
            }
        }
    }
    return true;
}

static std::vector<GgufFile> fetchModelFiles(const SearchResult& model, std::wstring& error) {
    std::vector<GgufFile> files;
    fetchModelFilesRecursive(model.owner, model.repository, model.revision, L"", files, error);
    std::sort(files.begin(), files.end(), [](const GgufFile& a, const GgufFile& b) {
        if (a.size != b.size) return a.size < b.size;
        return a.path < b.path;
    });
    return files;
}

static long long recommendedCapacityBytes() {
    long long bestVram = 0;
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                desc.DedicatedVideoMemory > bestVram) {
                bestVram = static_cast<long long>(desc.DedicatedVideoMemory);
            }
            if (adapter) adapter->Release();
            adapter = nullptr;
        }
        factory->Release();
    }
    const long long gib = 1024LL * 1024LL * 1024LL;
    if (bestVram >= gib) return bestVram - (gib * 175LL / 100LL);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        long long totalMemory = static_cast<long long>(memory.ullTotalPhys);
        if (totalMemory > gib * 3) return totalMemory - gib * 2;
    }
    return gib * 6;
}

static std::vector<SearchResult> recommendedModels(std::wstring& error) {
    long long limit = recommendedCapacityBytes();
    std::vector<SearchResult> results = searchModelScope(L"gguf q4", error);
    if (results.empty() && error.find(L"没有找到") != std::wstring::npos) {
        error.clear();
        results = searchModelScope(L"gguf", error);
    }
    std::vector<SearchResult> filtered;
    for (const SearchResult& model : results) {
        if (model.estimatedSize > 0 && model.estimatedSize <= limit) filtered.push_back(model);
    }
    std::sort(filtered.begin(), filtered.end(), [](const SearchResult& a, const SearchResult& b) {
        if (a.estimatedSize != b.estimatedSize) return a.estimatedSize > b.estimatedSize;
        return a.stars > b.stars;
    });
    if (filtered.empty()) error = L"当前显存/内存容量下没有找到合适的 Q4 GGUF 模型。";
    return filtered;
}

static void startSearch(bool recommended) {
    if (g.searchBusy) return;
    g.searchBusy = true;
    std::wstring query;
    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        query = recommended ? L"gguf q4" : opg::utf8ToWide(g.searchQuery);
        g.searchResults.clear();
        g.selectedSearch = -1;
    }
    if (!recommended && trimWide(query).empty()) {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.searchBusy = false;
        g.searchStatus = "请输入搜索关键词。";
        return;
    }
    setStatus(recommended ? "正在按显存/内存推荐 Q4 模型..." : "正在搜索魔搭社区...");
    std::thread([query, recommended]() {
        std::wstring error;
        std::vector<SearchResult> results = recommended ? recommendedModels(error) : searchModelScope(query, error);
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.searchResults = std::move(results);
        g.searchBusy = false;
        g.selectedSearch = g.searchResults.empty() ? -1 : 0;
        if (!error.empty()) {
            g.searchStatus = utf8(error);
        } else {
            wchar_t buffer[128];
            swprintf(buffer, 128, L"找到 %zu 个 GGUF 模型。", g.searchResults.size());
            g.searchStatus = utf8(buffer);
        }
        setStatus(g.searchStatus);
    }).detach();
}

static void startFetchFiles() {
    if (g.filesBusy) return;
    g.filesBusy = true;
    SearchResult model;
    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        model = g.selectedModelScope;
        g.ggufFiles.clear();
        g.selectedGguf = -1;
        g.filesStatus = "正在读取模型文件...";
    }
    std::thread([model]() {
        std::wstring error;
        std::vector<GgufFile> files = fetchModelFiles(model, error);
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.ggufFiles = std::move(files);
        g.filesBusy = false;
        g.selectedGguf = g.ggufFiles.empty() ? -1 : 0;
        g.filesStatus = error.empty() ? (g.ggufFiles.empty() ? "该模型没有 GGUF 文件。" : "请选择 GGUF 文件。") : utf8(error);
    }).detach();
}

static bool writeModelfileContent(const fs::path& modelfilePath, const std::wstring& ggufPath, std::wstring& error) {
    HANDLE file = CreateFileW(modelfilePath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wchar_t code[64]{};
        swprintf(code, 64, L"（Windows 错误 %lu）", static_cast<unsigned long>(GetLastError()));
        error = L"无法创建 Modelfile：" + modelfilePath.wstring() + code;
        return false;
    }
    std::string content = "FROM \"" + opg::wideToUtf8(ggufPath) + "\"\n";
    DWORD written = 0;
    BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok || written != content.size()) {
        error = L"写入 Modelfile 失败：" + modelfilePath.wstring();
        return false;
    }
    return true;
}

static bool writeDownloadModelfile(const std::wstring& ggufPath, const std::wstring& modelName,
                                   std::wstring& modelfilePath, std::wstring& error) {
    try {
        fs::path directory = fs::path(ggufPath).parent_path();
        fs::path desired = directory / (sanitizeModelName(modelName) + L".Modelfile");
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (writeModelfileContent(desired, ggufPath, error)) {
            modelfilePath = desired.wstring();
            return true;
        }
        wchar_t temp[MAX_PATH * 2]{};
        DWORD length = GetTempPathW(MAX_PATH * 2, temp);
        if (length == 0 || length >= MAX_PATH * 2) return false;
        fs::path fallback = fs::path(temp) / L"OllamaPlusGUI";
        fs::create_directories(fallback, ec);
        fs::path fallbackFile = fallback / (sanitizeModelName(modelName) + L".Modelfile");
        if (writeModelfileContent(fallbackFile, ggufPath, error)) {
            modelfilePath = fallbackFile.wstring();
            return true;
        }
        return false;
    } catch (const fs::filesystem_error& e) {
        error = L"创建 Modelfile 失败：" + opg::utf8ToWide(e.what());
        return false;
    }
}

static void startModelListRefresh() {
    if (g.modelsBusy) return;
    g.modelsBusy = true;
    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.modelsStatus = "正在读取 Ollama 模型列表...";
    }
    std::thread([]() {
        opg::CommandResult result = opg::runCommand(ollamaCommand() + L" list");
        std::vector<LocalModel> models;
        if (result.started) {
            std::wistringstream stream(result.output);
            std::wstring line;
            std::getline(stream, line);
            while (std::getline(stream, line)) {
                std::vector<std::wstring> parts;
                std::wistringstream lineStream(line);
                std::wstring part;
                while (lineStream >> part && parts.size() < 3) parts.push_back(part);
                if (parts.size() >= 3) {
                    std::wstring modified;
                    std::getline(lineStream, modified);
                    models.push_back({parts[0], parts[1], parts[2], trimWide(modified)});
                }
            }
        }
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.localModels = std::move(models);
        g.modelsBusy = false;
        g.selectedModel = g.localModels.empty() ? -1 : 0;
        if (!result.succeeded()) {
            g.modelsStatus = utf8(result.error);
            showError("读取 Ollama 模型列表失败", g.modelsStatus);
        } else {
            g.modelsStatus = "已加载 " + std::to_string(g.localModels.size()) + " 个本地模型。";
        }
        setStatus(g.modelsStatus);
    }).detach();
}

static void startDeleteModel(const std::wstring& name) {
    if (g.modelsBusy) return;
    g.modelsBusy = true;
    setStatus("正在删除模型: " + utf8(name));
    std::thread([name]() {
        opg::CommandResult result = opg::runCommand(ollamaCommand() + L" rm " + opg::quoteIfNeeded(name));
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.modelsBusy = false;
        if (!result.succeeded()) {
            showError("删除 Ollama 模型失败", utf8(result.error.empty() ? result.output : result.error));
        } else {
            setStatus("模型已删除: " + utf8(name));
            startModelListRefresh();
        }
    }).detach();
}

static void startRenameModel(const std::wstring& oldName, const std::wstring& newName) {
    if (g.modelsBusy) return;
    g.modelsBusy = true;
    setStatus("正在重命名模型: " + utf8(oldName) + " -> " + utf8(newName));
    std::thread([oldName, newName]() {
        opg::CommandResult copy = opg::runCommand(ollamaCommand() + L" cp " + opg::quoteIfNeeded(oldName) + L" " + opg::quoteIfNeeded(newName));
        bool success = copy.succeeded();
        std::wstring error;
        if (success) {
            opg::CommandResult remove = opg::runCommand(ollamaCommand() + L" rm " + opg::quoteIfNeeded(oldName));
            success = remove.succeeded();
            error = remove.error.empty() ? remove.output : remove.error;
            if (!success) error += L"\n\n注意：新模型名可能已创建，但旧模型删除失败。";
        } else {
            error = copy.error.empty() ? copy.output : copy.error;
        }
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.modelsBusy = false;
        if (!success) {
            showError("重命名 Ollama 模型失败", utf8(error));
        } else {
            setStatus("模型已重命名: " + utf8(newName));
            startModelListRefresh();
        }
    }).detach();
}

static void startLocalImport(const std::wstring& ggufPath, const std::wstring& modelName) {
    if (g.importBusy) return;
    g.importBusy = true;
    setStatus("正在导入本地 GGUF: " + utf8(ggufPath));
    std::thread([ggufPath, modelName]() {
        fs::path directory = fs::path(ggufPath).parent_path();
        fs::path defaultFile = directory / L"Modelfile";
        fs::path modelfile = defaultFile;
        std::wstring modelfileError;
        if (!fs::exists(defaultFile)) {
            fs::path created = directory / (fs::path(ggufPath).stem().wstring() + L".Modelfile");
            if (!writeModelfileContent(created, ggufPath, modelfileError)) {
                std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
                g.importBusy = false;
                showError("创建 Modelfile 失败", utf8(modelfileError));
                return;
            }
            modelfile = created;
        }
        opg::CommandResult result = opg::runCommand(ollamaCommand() + L" create " +
                                                    opg::quoteIfNeeded(modelName) + L" -f " +
                                                    opg::quoteIfNeeded(modelfile.wstring()));
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.importBusy = false;
        if (!result.succeeded()) {
            showError("Ollama 导入 GGUF 失败", utf8(result.error.empty() ? result.output : result.error));
        } else {
            setStatus("模型导入完成: " + utf8(modelName));
            startModelListRefresh();
        }
    }).detach();
}

static void startDownload(const SearchResult& model, const GgufFile& file,
                          const std::wstring& destination, const std::wstring& modelName) {
    if (g.downloadBusy) return;
    g.downloadBusy = true;
    g.downloadCancel = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.downloadProgress = 0.0f;
        g.downloadStatus = "准备下载...";
    }
    std::thread([model, file, destination, modelName]() {
        std::wstring url = opg::buildDownloadUrl(model.owner, model.repository, model.revision, file.path);
        std::wstring error;
        bool success = opg::httpDownloadFile(url, destination, [](unsigned long long downloaded, unsigned long long total) {
            float ratio = total ? static_cast<float>(downloaded) / static_cast<float>(total) : 0.0f;
            const bool canceling = g.downloadCancel.load();
            std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
            g.downloadProgress = ratio;
            g.downloadCompleted = downloaded;
            g.downloadTotal = total;
            g.downloadStatus = canceling ? "正在取消下载..." : ("下载中 " + formatBytes(downloaded) + " / " + formatBytes(total));
            return !canceling;
        }, error);
        std::wstring modelfile;
        std::wstring modelfileError;
        if (success) success = writeDownloadModelfile(destination, modelName, modelfile, modelfileError);
        if (success) error = modelfileError;
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.downloadBusy = false;
        if (!success) {
            if (g.downloadCancel.load()) {
                g.downloadStatus = "下载已取消。";
                g.downloadProgress = 0.0f;
                g.downloadCompleted = 0;
                g.downloadTotal = 0;
                setStatus("GGUF 下载已取消。");
            } else {
                g.downloadStatus = "下载/准备失败。";
                g.downloadProgress = 0.0f;
                g.downloadCompleted = 0;
                g.downloadTotal = 0;
                showError("下载 GGUF 失败", utf8(error));
            }
            return;
        }
        g.downloadStatus = "下载完成，正在导入...";
        g.downloadProgress = 1.0f;
        setStatus("GGUF 下载完成，正在导入: " + utf8(modelName));
        startLocalImport(destination, modelName);
    }).detach();
}

static std::string base64Encode(const std::string& value) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((value.size() + 2) / 3 * 4);
    for (size_t i = 0; i < value.size(); i += 3) {
        unsigned b0 = static_cast<unsigned char>(value[i]);
        unsigned b1 = i + 1 < value.size() ? static_cast<unsigned char>(value[i + 1]) : 0;
        unsigned b2 = i + 2 < value.size() ? static_cast<unsigned char>(value[i + 2]) : 0;
        unsigned triple = (b0 << 16) | (b1 << 8) | b2;
        result.push_back(table[(triple >> 18) & 63]);
        result.push_back(table[(triple >> 12) & 63]);
        result.push_back(i + 1 < value.size() ? table[(triple >> 6) & 63] : '=');
        result.push_back(i + 2 < value.size() ? table[triple & 63] : '=');
    }
    return result;
}

static bool readChatAttachment(const std::wstring& path, ChatAttachment& attachment, std::wstring& error) {
    try {
        attachment.path = path;
        attachment.name = utf8(fileNameWide(path));
        attachment.image = fileExtension(path) == L".png" || fileExtension(path) == L".jpg" ||
                           fileExtension(path) == L".jpeg" || fileExtension(path) == L".gif" ||
                           fileExtension(path) == L".webp";
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) {
            error = L"读取文件大小失败：" + opg::utf8ToWide(ec.message());
            return false;
        }
        attachment.size = static_cast<long long>(size);
        if (attachment.image && size > 10ULL * 1024ULL * 1024ULL) {
            error = L"图片附件过大（最大 10 MB）：" + fileNameWide(path);
            return false;
        }
        if (!attachment.image && size > 2ULL * 1024ULL * 1024ULL) {
            error = L"文本附件过大（最大 2 MB）：" + fileNameWide(path);
            return false;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            wchar_t code[64]{};
            swprintf(code, 64, L"（Windows 错误 %lu）", static_cast<unsigned long>(GetLastError()));
            error = L"无法打开附件：" + path + code;
            return false;
        }
        std::string bytes(static_cast<size_t>(size), '\0');
        DWORD read = 0;
        BOOL ok = size ? ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) : TRUE;
        CloseHandle(file);
        if (!ok || (size && read != bytes.size())) {
            error = L"读取附件内容失败：" + path;
            return false;
        }
        if (attachment.image) attachment.base64 = base64Encode(bytes);
        else attachment.text = opg::utf8ToWide(bytes);
        return true;
    } catch (const std::exception& e) {
        error = L"读取附件失败：" + opg::utf8ToWide(e.what());
        return false;
    }
}

static std::string buildChatRequestBody(const std::string& model, const std::vector<ChatMessage>& history,
                                        const Settings& settings, bool thinking,
                                        const std::vector<std::string>& images) {
    std::string messages;
    if (!settings.systemPrompt.empty()) {
        messages += "{\"role\":\"system\",\"content\":\"" + jsonEscapeUtf8(settings.systemPrompt) + "\"}";
    }
    for (size_t index = 0; index < history.size(); ++index) {
        const ChatMessage& message = history[index];
        if (!messages.empty()) messages += ",";
        messages += "{\"role\":\"" + message.role + "\",\"content\":\"" + jsonEscapeUtf8(message.content) + "\"";
        if (message.role == "assistant" && !message.thinking.empty()) {
            messages += ",\"thinking\":\"" + jsonEscapeUtf8(message.thinking) + "\"";
        }
        if (message.role == "user" && index + 1 == history.size() && !images.empty()) {
            messages += ",\"images\":[";
            for (size_t i = 0; i < images.size(); ++i) {
                if (i) messages += ",";
                messages += "\"" + images[i] + "\"";
            }
            messages += "]";
        }
        messages += "}";
    }
    std::string options = ",\"options\":{";
    options += "\"temperature\":" + std::to_string(settings.temperature) + ",";
    options += "\"top_p\":" + std::to_string(settings.topP) + ",";
    options += "\"top_k\":" + std::to_string(settings.topK) + ",";
    options += "\"repeat_penalty\":" + std::to_string(settings.repeatPenalty) + ",";
    options += "\"num_ctx\":" + std::to_string(settings.numCtx);
    if (settings.seed >= 0) options += ",\"seed\":" + std::to_string(settings.seed);
    options += "}";
    std::string body = "{\"model\":\"" + jsonEscapeUtf8(model) + "\",\"messages\":[" + messages + "]";
    body += ",\"stream\":" + std::string(settings.stream ? "true" : "false");
    body += ",\"think\":" + std::string(thinking ? "true" : "false");
    if (settings.jsonMode) body += ",\"format\":\"json\"";
    if (!settings.keepAlive.empty()) body += ",\"keep_alive\":\"" + jsonEscapeUtf8(settings.keepAlive) + "\"";
    body += options + "}";
    return body;
}

static void updateLastAssistant(const std::wstring& contentChunk, const std::wstring& thinkingChunk, bool replace) {
    std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
    if (g.chatMessages.empty()) return;
    ChatMessage& message = g.chatMessages.back();
    if (!thinkingChunk.empty()) {
        if (replace) message.thinking = utf8(thinkingChunk);
        else message.thinking += utf8(thinkingChunk);
    }
    if (!contentChunk.empty()) {
        if (replace) message.content = utf8(contentChunk);
        else message.content += utf8(contentChunk);
    }
}

static void startChat() {
    if (g.chatBusy) return;
    std::string prompt;
    std::vector<ChatMessage> history;
    std::vector<ChatAttachment> attachments;
    std::string model;
    Settings settings;
    bool thinking = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        prompt = trimCopy(g.chatInput);
        attachments = g.chatAttachments;
        history = g.chatMessages;
        model = g.chatModel;
        settings = g.settings;
        thinking = settings.think;
    }
    if (model.empty()) {
        showError("无法发送消息", "请先选择模型。");
        return;
    }
    if (prompt.empty() && attachments.empty()) {
        showError("无法发送消息", "请输入聊天内容或导入文件。");
        return;
    }
    if (prompt.empty()) prompt = "请分析我导入的文件。";
    std::string content = prompt;
    std::vector<std::string> images;
    std::string attachmentSummary;
    for (const ChatAttachment& item : attachments) {
        if (item.image) {
            images.push_back(item.base64);
            attachmentSummary += "\n[图片附件: " + item.name + "]";
        } else {
            content += "\n\n[文本附件: " + item.name + "]\n" + utf8(item.text);
            attachmentSummary += "\n[文本附件: " + item.name + "]";
        }
    }
    ChatMessage user;
    user.role = "user";
    user.sender = "用户";
    user.content = content;
    history.push_back(user);
    ChatMessage assistant;
    assistant.role = "assistant";
    assistant.sender = model;
    assistant.content = "";
    assistant.thinking = "";

    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.chatMessages.push_back(user);
        g.chatMessages.push_back(assistant);
        g.chatInput[0] = '\0';
        g.chatAttachments.clear();
        g.chatStop = false;
        g.chatStatus = "AI 正在回复...";
    }
    g.chatBusy = true;
    std::thread([model, history, settings, thinking, images]() {
        std::string body = buildChatRequestBody(model, history, settings, thinking, images);
        std::string responseText;
        std::string errorText;
        auto onChunk = [&](const std::string& chunk) {
            responseText += chunk;
            if (!g.settings.stream) return true;
            size_t start = 0;
            for (;;) {
                size_t position = responseText.find('\n', start);
                if (position == std::string::npos) break;
                std::string line = responseText.substr(start, position - start);
                start = position + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                JsonValue value;
                std::wstring parseError;
                if (!opg::parseJson(line, value, parseError)) continue;
                const JsonValue* content = value.path({L"message", L"content"});
                const JsonValue* think = value.path({L"message", L"thinking"});
                if (!think) think = value.path({L"message", L"reasoning"});
                updateLastAssistant(content ? content->asString() : std::wstring(), think ? think->asString() : std::wstring(), false);
            }
            responseText.erase(0, start);
            return !g.chatStop.load();
        };
        opg::StreamResponse response = opg::httpStreamRequest(L"POST", L"http://127.0.0.1:11434/api/chat",
                                                              body, L"application/json", onChunk);
        if (!response.ok()) {
            std::wstring error = response.error;
            if (!response.body.empty()) {
                JsonValue value;
                std::wstring parseError;
                if (opg::parseJson(response.body, value, parseError)) {
                    const JsonValue* errorValue = value.get(L"error");
                    if (errorValue) error = errorValue->asString();
                }
            }
            errorText = opg::wideToUtf8(error);
        } else if (!g.settings.stream) {
            JsonValue value;
            std::wstring parseError;
            if (opg::parseJson(responseText, value, parseError)) {
                const JsonValue* content = value.path({L"message", L"content"});
                const JsonValue* think = value.path({L"message", L"thinking"});
                if (!think) think = value.path({L"message", L"reasoning"});
                updateLastAssistant(content ? content->asString() : std::wstring(), think ? think->asString() : std::wstring(), true);
            } else {
                errorText = utf8(parseError);
            }
        }
        if (g.chatStop.load()) errorText = "请求已取消。";
        {
            std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
            g.chatBusy = false;
            if (!errorText.empty()) {
                g.chatStatus = "请求失败。";
                showError("AI 请求失败", errorText);
            } else {
                g.chatStatus = "回复完成。";
            }
            setStatus(g.chatStatus);
        }
    }).detach();
}

static void startOllamaInfo() {
    if (g.infoBusy) return;
    g.infoBusy = true;
    setStatus("正在读取 Ollama 信息...");
    std::thread([]() {
        opg::HttpResponse response = opg::httpRequest(L"GET", L"http://127.0.0.1:11434/api/version");
        opg::CommandResult cli = opg::runCommand(ollamaCommand() + L" --version");
        std::string info = "Ollama CLI: " + utf8(cli.output.empty() ? cli.error : cli.output);
        info += "\nOllama API: ";
        if (response.ok()) {
            JsonValue value;
            std::wstring parseError;
            if (opg::parseJson(response.body, value, parseError)) {
                const JsonValue* version = value.get(L"version");
                info += utf8(version ? version->asString() : L"未知");
            } else {
                info += "响应解析失败";
            }
        } else {
            info += "无法连接（" + utf8(response.error) + "）";
        }
        {
            std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
            g.infoBusy = false;
            g.ollamaInfo = info;
        }
        setStatus("Ollama 信息已更新。");
    }).detach();
}

static void startCommand() {
    if (g.commandBusy) return;
    std::string command;
    {
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        command = trimCopy(g.commandInput);
    }
    if (command.empty()) {
        showError("无法执行命令", "请输入 Ollama 命令参数，例如 list / ps / show 模型名。");
        return;
    }
    g.commandBusy = true;
    std::thread([command]() {
        opg::CommandResult result = opg::runCommand(ollamaCommand() + L" " + wide(command));
        std::string output = utf8(result.output.empty() ? result.error : result.output);
        {
            std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
            g.commandBusy = false;
            g.commandOutput = output;
        }
        setStatus("命令执行完成。");
    }).detach();
}

static void startFirewallAdd(unsigned short port) {
    if (g.firewallBusy) return;
    if (!isUserAdmin()) {
        showError("需要管理员权限", "添加防火墙规则需要管理员权限。请右键程序选择“以管理员身份运行”。");
        return;
    }
    g.firewallBusy = true;
    setStatus("正在添加防火墙规则...");
    std::thread([port]() {
        std::wstring ruleName = L"Ollama Plus GUI Proxy";
        std::wstring deleteCommand = L"netsh advfirewall firewall delete rule name=\"" + ruleName + L"\"";
        opg::runCommand(deleteCommand);
        std::wstring addCommand = L"netsh advfirewall firewall add rule name=\"" + ruleName +
                                  L"\" dir=in action=allow protocol=TCP localport=" +
                                  std::to_wstring(port) + L" profile=any";
        opg::CommandResult result = opg::runCommand(addCommand);
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.firewallBusy = false;
        if (!result.succeeded()) {
            g.firewallStatus = utf8(result.error.empty() ? result.output : result.error);
            showError("添加防火墙规则失败", g.firewallStatus);
        } else {
            g.firewallStatus = "已添加防火墙规则，端口 " + std::to_string(port) + "。";
        }
        setStatus(g.firewallStatus);
    }).detach();
}

static void startFirewallRemove() {
    if (g.firewallBusy) return;
    if (!isUserAdmin()) {
        showError("需要管理员权限", "删除防火墙规则需要管理员权限。");
        return;
    }
    g.firewallBusy = true;
    setStatus("正在删除防火墙规则...");
    std::thread([]() {
        std::wstring command = L"netsh advfirewall firewall delete rule name=\"Ollama Plus GUI Proxy\"";
        opg::CommandResult result = opg::runCommand(command);
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.firewallBusy = false;
        g.firewallStatus = result.succeeded() ? "已删除防火墙规则。" : utf8(result.error.empty() ? result.output : result.error);
        setStatus(g.firewallStatus);
    }).detach();
}

static void startFirewallStatus() {
    if (g.firewallBusy) return;
    g.firewallBusy = true;
    setStatus("正在读取防火墙规则...");
    std::thread([]() {
        opg::CommandResult result = opg::runCommand(L"netsh advfirewall firewall show rule name=\"Ollama Plus GUI Proxy\"");
        std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
        g.firewallBusy = false;
        g.firewallStatus = utf8(result.output.empty() ? result.error : result.output);
        setStatus("防火墙状态已更新。");
    }).detach();
}

static bool openService(HWND hwnd) {
    if (!isUserAdmin()) {
        showError("需要管理员权限", "开放服务端需要管理员权限。请右键程序选择“以管理员身份运行”。");
        return false;
    }
    unsigned short port = static_cast<unsigned short>(atoi(g.serverPortText));
    if (port == 0) {
        showError("无法开放服务端", "请输入有效的端口号。");
        return false;
    }
    std::wstring error;
    if (!opg::startProxyServer(g_proxy, port, error)) {
        showError("开放服务端失败", utf8(error));
        return false;
    }
    g.serverRunning = true;
    g.serverStatus = "服务端已开放: 0.0.0.0:" + std::to_string(port) + " -> 127.0.0.1:11434";
    setStatus("服务端已开放，外部客户端可通过本机 IP 端口 " + std::to_string(port) + " 访问。");
    return true;
}

static void closeService() {
    opg::stopProxyServer(g_proxy);
    g.serverRunning = false;
    g.serverStatus = "服务端已关闭。";
    setStatus("服务端已关闭。");
}

static void refreshBlockedIps() {
    std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
    g.blockedIps.clear();
    for (const auto& ip : g_proxy.blockedList()) g.blockedIps.push_back(utf8(ip));
}

static void addBlockedIp() {
    std::string ip = trimCopy(g.blockedIp);
    if (ip.empty()) return;
    g_proxy.addBlocked(wide(ip));
    g.blockedIp[0] = '\0';
    refreshBlockedIps();
    setStatus("已拉黑 IP: " + ip);
}

static void removeBlockedIp(int index) {
    if (index < 0 || index >= static_cast<int>(g.blockedIps.size())) return;
    std::string ip = g.blockedIps[index];
    g_proxy.removeBlocked(wide(ip));
    refreshBlockedIps();
    setStatus("已解除拉黑 IP: " + ip);
}

static void applyTheme();

static void applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(14, 14);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 12.0f;

    ImVec4 bg, panel, frame, frameHover, frameActive, text, textDim, accent, accentHover, accentActive, border;
    if (g.theme == 0) {
        bg = ImVec4(0.03f, 0.03f, 0.04f, 1.00f);
        panel = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        frame = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
        frameHover = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
        frameActive = ImVec4(0.18f, 0.18f, 0.23f, 1.00f);
        text = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
        textDim = ImVec4(0.58f, 0.60f, 0.65f, 1.00f);
        accent = ImVec4(0.86f, 0.09f, 0.16f, 1.00f);
        accentHover = ImVec4(0.95f, 0.16f, 0.20f, 1.00f);
        accentActive = ImVec4(0.70f, 0.05f, 0.10f, 1.00f);
        border = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    } else if (g.theme == 1) {
        bg = ImVec4(0.10f, 0.01f, 0.02f, 1.00f);
        panel = ImVec4(0.16f, 0.02f, 0.04f, 1.00f);
        frame = ImVec4(0.22f, 0.03f, 0.06f, 1.00f);
        frameHover = ImVec4(0.29f, 0.05f, 0.08f, 1.00f);
        frameActive = ImVec4(0.36f, 0.07f, 0.10f, 1.00f);
        text = ImVec4(1.00f, 0.94f, 0.94f, 1.00f);
        textDim = ImVec4(0.92f, 0.68f, 0.68f, 1.00f);
        accent = ImVec4(1.00f, 0.16f, 0.20f, 1.00f);
        accentHover = ImVec4(1.00f, 0.35f, 0.36f, 1.00f);
        accentActive = ImVec4(0.75f, 0.06f, 0.08f, 1.00f);
        border = ImVec4(0.45f, 0.07f, 0.10f, 1.00f);
    } else {
        bg = ImVec4(0.97f, 0.98f, 0.99f, 1.00f);
        panel = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        frame = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
        frameHover = ImVec4(0.88f, 0.90f, 0.93f, 1.00f);
        frameActive = ImVec4(0.82f, 0.85f, 0.89f, 1.00f);
        text = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
        textDim = ImVec4(0.42f, 0.45f, 0.50f, 1.00f);
        accent = ImVec4(0.82f, 0.08f, 0.13f, 1.00f);
        accentHover = ImVec4(0.92f, 0.18f, 0.22f, 1.00f);
        accentActive = ImVec4(0.65f, 0.04f, 0.08f, 1.00f);
        border = ImVec4(0.82f, 0.84f, 0.87f, 1.00f);
    }

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = panel;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_FrameBg] = frame;
    colors[ImGuiCol_FrameBgHovered] = frameHover;
    colors[ImGuiCol_FrameBgActive] = frameActive;
    colors[ImGuiCol_TitleBg] = panel;
    colors[ImGuiCol_TitleBgActive] = panel;
    colors[ImGuiCol_TitleBgCollapsed] = panel;
    colors[ImGuiCol_MenuBarBg] = panel;
    colors[ImGuiCol_ScrollbarBg] = frame;
    colors[ImGuiCol_ScrollbarGrab] = accent;
    colors[ImGuiCol_ScrollbarGrabHovered] = accentHover;
    colors[ImGuiCol_ScrollbarGrabActive] = accentActive;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;
    colors[ImGuiCol_Button] = accent;
    colors[ImGuiCol_ButtonHovered] = accentHover;
    colors[ImGuiCol_ButtonActive] = accentActive;
    colors[ImGuiCol_Header] = frameHover;
    colors[ImGuiCol_HeaderHovered] = accent;
    colors[ImGuiCol_HeaderActive] = accentActive;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accentActive;
    colors[ImGuiCol_ResizeGrip] = border;
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accentActive;
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = accent;
    colors[ImGuiCol_TabSelected] = accent;
    colors[ImGuiCol_TableHeaderBg] = frame;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = border;
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0.03f);
    colors[ImGuiCol_NavCursor] = accent;
}

static void drawSearchPage() {
    ImGui::TextUnformatted("魔搭 GGUF 搜索");
    ImGui::SeparatorText("搜索");
    ImGui::SetNextItemWidth(std::max(280.0f, ImGui::GetContentRegionAvail().x - 260.0f));
    ImGui::InputText("##searchQuery", g.searchQuery, sizeof(g.searchQuery), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("搜索", ImVec2(100, 0)) && !g.searchBusy) startSearch(false);
    ImGui::SameLine();
    if (ImGui::Button("按显存推荐", ImVec2(130, 0)) && !g.searchBusy) startSearch(true);
    ImGui::SameLine();
    if (g.searchBusy) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.0f), "搜索中...");
    } else {
        ImGui::TextUnformatted(g.searchStatus.c_str());
    }

    ImGui::SeparatorText("结果");
    if (ImGui::BeginTable("##searchResults", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                    ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                          ImVec2(0, std::max(220.0f, ImGui::GetContentRegionAvail().y - 140.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("模型", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("作者", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("估算大小", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Stars", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(g.searchResults.size()); ++i) {
            const SearchResult& model = g.searchResults[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string label = utf8(model.owner + L"/" + model.repository);
            if (ImGui::Selectable(label.c_str(), g.selectedSearch == i, ImGuiSelectableFlags_SpanAllColumns)) {
                g.selectedSearch = i;
                g.selectedModelScope = model;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                g.selectedSearch = i;
                g.selectedModelScope = model;
                g.fileModalOpen = true;
                g.fileModalRequested = true;
                startFetchFiles();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(utf8(model.owner).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytes(model.estimatedSize > 0 ? model.estimatedSize : model.size).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", model.stars);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("选择文件")) {
                g.selectedSearch = i;
                g.selectedModelScope = model;
                g.fileModalOpen = true;
                g.fileModalRequested = true;
                startFetchFiles();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    int selected = g.selectedSearch;
    if (selected >= 0 && selected < static_cast<int>(g.searchResults.size())) {
        const SearchResult& model = g.searchResults[selected];
        ImGui::SeparatorText("选中模型");
        ImGui::Text("模型: %s/%s", utf8(model.owner).c_str(), utf8(model.repository).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("版本: %s", utf8(model.revision).c_str());
        ImGui::Text("仓库大小: %s    估算模型大小: %s", formatBytes(model.size).c_str(),
                    formatBytes(model.estimatedSize).c_str());
        if (ImGui::Button("选择 GGUF 文件")) {
            g.fileModalOpen = true;
            g.fileModalRequested = true;
            startFetchFiles();
        }
    }
}

static void drawModelsPage() {
    ImGui::TextUnformatted("本地 Ollama 模型");
    ImGui::SameLine();
    if (ImGui::Button("刷新", ImVec2(90, 0)) && !g.modelsBusy) startModelListRefresh();
    ImGui::SameLine();
    if (ImGui::Button("导入本地 GGUF", ImVec2(150, 0)) && !g.importBusy) {
        std::vector<std::wstring> paths;
        if (chooseOpenFile(g_hwnd,
                           L"GGUF 模型文件\0*.gguf\0所有文件\0*.*\0", L"选择要导入的 GGUF 文件", false, paths)) {
            g.importPath = paths[0];
            std::wstring name = sanitizeModelName(fs::path(paths[0]).stem().wstring());
            strncpy(g.importName, utf8(name).c_str(), sizeof(g.importName) - 1);
            g.importModalOpen = true;
            g.importModalRequested = true;
        }
    }
    ImGui::SameLine();
    if (g.modelsBusy) ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.0f), "加载中...");
    else ImGui::TextUnformatted(g.modelsStatus.c_str());

    if (ImGui::BeginTable("##localModels", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                          ImVec2(0, std::max(220.0f, ImGui::GetContentRegionAvail().y - 120.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("模型名称", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("修改时间", ImGuiTableColumnFlags_WidthFixed, 210.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(g.localModels.size()); ++i) {
            const LocalModel& model = g.localModels[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(utf8(model.name).c_str(), g.selectedModel == i, ImGuiSelectableFlags_SpanAllColumns)) {
                g.selectedModel = i;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                g.selectedModel = i;
                g.chatModel = utf8(model.name);
                g.chatOpen = true;
                g.chatRequested = true;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(utf8(model.id).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(utf8(model.size).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(utf8(model.modified).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    int selected = g.selectedModel;
    if (selected >= 0 && selected < static_cast<int>(g.localModels.size())) {
        const LocalModel& model = g.localModels[selected];
        ImGui::SeparatorText("模型操作");
        ImGui::Text("已选择: %s", utf8(model.name).c_str());
        ImGui::SameLine();
        if (ImGui::Button("运行模型") && !g.chatBusy) {
            g.chatModel = utf8(model.name);
            g.chatOpen = true;
            g.chatRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("重命名")) {
            g.renameOld = model.name;
            strncpy(g.renameName, utf8(model.name).c_str(), sizeof(g.renameName) - 1);
            g.renameModalOpen = true;
            g.renameModalRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("删除")) {
            g.deleteName = model.name;
            g.deleteModalOpen = true;
            g.deleteModalRequested = true;
        }
    } else {
        ImGui::SeparatorText("模型操作");
        ImGui::TextDisabled("请先选择模型。");
    }
}

static void drawServicePage() {
    ImGui::TextUnformatted("Ollama 服务端代理");
    ImGui::SeparatorText("服务");
    ImGui::SetNextItemWidth(130.0f);
    ImGui::InputText("外部端口", g.serverPortText, sizeof(g.serverPortText));
    if (!g.serverRunning) {
        if (ImGui::Button("开放服务端")) openService(g_hwnd);
    } else if (ImGui::Button("关闭服务端")) closeService();
    ImGui::SameLine();
    ImGui::TextUnformatted(g.serverStatus.c_str());

    ImGui::SeparatorText("防火墙");
    if (ImGui::Button("添加防火墙规则") && !g.firewallBusy) startFirewallAdd(static_cast<unsigned short>(atoi(g.serverPortText)));
    ImGui::SameLine();
    if (ImGui::Button("删除防火墙规则") && !g.firewallBusy) startFirewallRemove();
    ImGui::SameLine();
    if (ImGui::Button("查看规则") && !g.firewallBusy) startFirewallStatus();
    if (g.firewallBusy) ImGui::TextDisabled("处理中...");
    else ImGui::TextUnformatted(g.firewallStatus.c_str());

    ImGui::SeparatorText("API Key");
    ImGui::Checkbox("启用 API Key 验证", &g.requireKey);
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("API Key", g.apiKey, sizeof(g.apiKey));
    ImGui::TextDisabled("客户端可使用 Authorization: Bearer <key>、x-api-key 或 api-key 请求。");

    ImGui::SeparatorText("IP 黑名单");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("##blockedIp", g.blockedIp, sizeof(g.blockedIp));
    ImGui::SameLine();
    if (ImGui::Button("拉黑")) addBlockedIp();
    if (ImGui::BeginListBox("##blockedIps", ImVec2(320.0f, 140.0f))) {
        for (int i = 0; i < static_cast<int>(g.blockedIps.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(g.blockedIps[i].c_str(), false)) removeBlockedIp(i);
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    ImGui::SeparatorText("状态");
    ImGui::Text("总 token: %llu    请求: %llu    活跃: %llu    拦截: %llu",
                g_proxy.stats.totalTokens.load(), g_proxy.stats.totalRequests.load(),
                g_proxy.stats.activeRequests.load(), g_proxy.stats.blockedRequests.load());
    ImGui::Text("上行累计: %s    下行累计: %s",
                formatBytes(g_proxy.stats.bytesUp.load()).c_str(),
                formatBytes(g_proxy.stats.bytesDown.load()).c_str());
}

static void drawSettingsPage() {
    ImGui::TextUnformatted("设置");
    ImGui::SeparatorText("主题");
    const char* themes[] = {"黑", "红（竞技风）", "白"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(themes[i], g.theme == i)) {
            g.theme = i;
            applyTheme();
        }
    }

    ImGui::SeparatorText("软件 / Ollama 信息");
    ImGui::Text("软件版本: %s", kAppVersion);
    ImGui::SameLine();
    ImGui::TextDisabled("构建: %s", kBuildDate);
    ImGui::TextUnformatted(ollamaLocationText().c_str());
    if (ImGui::Button("刷新 Ollama 信息") && !g.infoBusy) startOllamaInfo();
    ImGui::SameLine();
    if (g.infoBusy) ImGui::TextDisabled("读取中...");
    ImGui::BeginChild("##ollamaInfo", ImVec2(0, 110.0f), ImGuiChildFlags_Borders);
    ImGui::TextWrapped("%s", g.ollamaInfo.c_str());
    ImGui::EndChild();

    ImGui::SeparatorText("人格 / 生成参数");
    ImGui::Text("系统提示词");
    static char systemPrompt[4096] = "";
    if (systemPrompt[0] == '\0' && !g.settings.systemPrompt.empty()) {
        strncpy(systemPrompt, g.settings.systemPrompt.c_str(), sizeof(systemPrompt) - 1);
    }
    ImGui::InputTextMultiline("##systemPrompt", systemPrompt, sizeof(systemPrompt), ImVec2(-1, 100.0f));
    g.settings.systemPrompt = systemPrompt;
    ImGui::SliderFloat("创造性（temperature）", &g.settings.temperature, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("核采样（top_p）", &g.settings.topP, 0.0f, 1.0f, "%.2f");
    ImGui::SliderInt("候选数量（top_k）", &g.settings.topK, 0, 200);
    ImGui::SliderFloat("重复惩罚（repeat_penalty）", &g.settings.repeatPenalty, 0.5f, 2.0f, "%.2f");
    ImGui::SliderInt("上下文长度（num_ctx）", &g.settings.numCtx, 512, 131072);
    ImGui::InputInt("随机种子（seed）", &g.settings.seed);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("保持加载（keep_alive）", g.keepAlive, sizeof(g.keepAlive));
    g.settings.keepAlive = g.keepAlive;

    ImGui::SeparatorText("Ollama 功能");
    ImGui::Checkbox("流式输出", &g.settings.stream);
    ImGui::SameLine();
    ImGui::Checkbox("思考模式", &g.settings.think);
    ImGui::SameLine();
    ImGui::Checkbox("JSON 输出", &g.settings.jsonMode);
    ImGui::TextDisabled("这些参数会用于 /api/chat 请求。");

    ImGui::SeparatorText("Ollama 命令");
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("命令参数", g.commandInput, sizeof(g.commandInput), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("执行") && !g.commandBusy) startCommand();
    ImGui::SameLine();
    if (ImGui::Button("list")) {
        strncpy(g.commandInput, "list", sizeof(g.commandInput) - 1);
        if (!g.commandBusy) startCommand();
    }
    ImGui::SameLine();
    if (ImGui::Button("ps")) {
        strncpy(g.commandInput, "ps", sizeof(g.commandInput) - 1);
        if (!g.commandBusy) startCommand();
    }
    if (g.commandBusy) ImGui::TextDisabled("执行中...");
    ImGui::BeginChild("##commandOutput", ImVec2(0, 170.0f), ImGuiChildFlags_Borders);
    ImGui::TextWrapped("%s", g.commandOutput.c_str());
    ImGui::EndChild();
}

static void drawChatWindow() {
    ImGui::SetNextWindowSize(ImVec2(920, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI 对话", &g.chatOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Text("模型: %s", g.chatModel.c_str());
    ImGui::SameLine();
    ImGui::Checkbox("思考模式", &g.settings.think);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g.chatStatus.c_str());

    ImGui::BeginChild("##chatHistory", ImVec2(0, std::max(180.0f, ImGui::GetContentRegionAvail().y - 220.0f)),
                      ImGuiChildFlags_Borders);
    for (const ChatMessage& message : g.chatMessages) {
        ImGui::PushID(&message);
        ImVec4 senderColor = message.role == "user" ? ImVec4(0.85f, 0.20f, 0.22f, 1.0f)
                                                     : ImVec4(0.20f, 0.65f, 0.95f, 1.0f);
        ImGui::TextColored(senderColor, "%s", message.sender.c_str());
        if (!message.thinking.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.70f, 1.0f));
            ImGui::TextWrapped("[思考] %s", message.thinking.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::TextWrapped("%s", message.content.c_str());
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::SeparatorText("附件");
    if (ImGui::Button("导入文件")) {
        std::vector<std::wstring> paths;
        if (chooseOpenFile(g_hwnd,
                           L"文本/图片文件\0*.txt;*.md;*.json;*.xml;*.csv;*.log;*.c;*.cpp;*.h;*.hpp;*.cs;*.py;*.js;*.ts;*.png;*.jpg;*.jpeg;*.gif;*.webp\0所有文件\0*.*\0",
                           L"导入聊天文件（可多选）", true, paths)) {
            for (const auto& path : paths) {
                ChatAttachment attachment;
                std::wstring error;
                if (readChatAttachment(path, attachment, error)) g.chatAttachments.push_back(std::move(attachment));
                else showError("导入附件失败", utf8(error));
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("移除选中")) {
        int selected = -1;
        for (int i = 0; i < static_cast<int>(g.chatAttachments.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(g.chatAttachments[i].name.c_str(), selected == i)) selected = i;
            ImGui::PopID();
        }
        if (selected >= 0 && selected < static_cast<int>(g.chatAttachments.size())) {
            g.chatAttachments.erase(g.chatAttachments.begin() + selected);
        }
    }
    ImGui::BeginChild("##attachmentList", ImVec2(0, 72.0f), ImGuiChildFlags_Borders);
    for (const ChatAttachment& item : g.chatAttachments) {
        ImGui::BulletText("%s (%s)", item.name.c_str(), formatBytes(item.size).c_str());
    }
    ImGui::EndChild();

    ImGui::SeparatorText("输入");
    ImGui::InputTextMultiline("##chatInput", g.chatInput, sizeof(g.chatInput),
                              ImVec2(-1, std::max(72.0f, ImGui::GetContentRegionAvail().y - 44.0f)));
    if (ImGui::Button(g.chatBusy ? "停止" : "发送", ImVec2(120, 0))) {
        if (g.chatBusy) g.chatStop = true;
        else startChat();
    }
    ImGui::End();
}

static void drawDownloadOverlay() {
    if (!g.downloadBusy) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 pos(viewport->WorkPos.x + 18.0f,
                     viewport->WorkPos.y + std::max(20.0f, viewport->WorkSize.y - 178.0f));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (ImGui::Begin("GGUF 下载进度", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("正在下载 GGUF 模型");
        ImGui::Separator();

        char overlay[64]{};
        snprintf(overlay, sizeof(overlay), "%.0f%%", g.downloadProgress * 100.0f);
        ImGui::ProgressBar(g.downloadProgress, ImVec2(-1.0f, 0.0f), overlay);

        if (g.downloadTotal > 0) {
            ImGui::Text("进度: %s / %s", formatBytes(g.downloadCompleted).c_str(),
                        formatBytes(g.downloadTotal).c_str());
        } else {
            ImGui::Text("已接收: %s", formatBytes(g.downloadCompleted).c_str());
        }
        ImGui::TextDisabled("%s", g.downloadStatus.c_str());

        if (ImGui::Button("取消下载", ImVec2(-1.0f, 34.0f))) {
            g.downloadCancel = true;
            g.downloadStatus = "正在取消下载...";
            setStatus("正在取消 GGUF 下载...");
        }
    }
    ImGui::End();
}

static void drawModals() {
    if (g.errorRequested) {
        ImGui::OpenPopup("错误");
        g.errorRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("错误", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(g.errorTitle.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##errorText", ImVec2(0, std::min(320.0f, ImGui::GetTextLineHeightWithSpacing() * 12.0f)),
                          ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", g.errorText.c_str());
        ImGui::EndChild();
        if (ImGui::Button("关闭", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g.fileModalRequested) {
        ImGui::OpenPopup("选择 GGUF 文件");
        g.fileModalRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(920, 620), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("选择 GGUF 文件", &g.fileModalOpen, ImGuiWindowFlags_NoSavedSettings)) {
        if (g.filesBusy) {
            ImGui::TextDisabled("%s", g.filesStatus.c_str());
        } else {
            ImGui::TextUnformatted(g.filesStatus.c_str());
        }
        if (ImGui::BeginTable("##ggufFiles", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                    ImGuiTableFlags_ScrollY,
                                          ImVec2(0, 380.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("文件", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("路径", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(g.ggufFiles.size()); ++i) {
                const GgufFile& file = g.ggufFiles[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(utf8(file.name).c_str(), g.selectedGguf == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    g.selectedGguf = i;
                    std::wstring suggested = fileNameWide(file.path);
                    strncpy(g.downloadName, utf8(suggested).c_str(), sizeof(g.downloadName) - 1);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatBytes(file.size).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(utf8(file.path).c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("导入设置");
        ImGui::Text("模型名");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##downloadName", g.downloadName, sizeof(g.downloadName));
        ImGui::TextDisabled("下载完成后会自动生成专属 Modelfile 并导入 Ollama。");
        bool canDownload = !g.filesBusy && g.selectedGguf >= 0 &&
                           g.selectedGguf < static_cast<int>(g.ggufFiles.size()) &&
                           !g.downloadBusy && strlen(g.downloadName) > 0;
        if (!canDownload) ImGui::BeginDisabled();
        if (ImGui::Button("下载并导入", ImVec2(150, 0))) {
            const GgufFile& file = g.ggufFiles[g.selectedGguf];
            std::wstring destination;
            if (chooseSaveFile(g_hwnd,
                               fileNameWide(file.path), L"选择 GGUF 保存位置", destination)) {
                std::wstring modelName = sanitizeModelName(opg::utf8ToWide(g.downloadName));
                startDownload(g.selectedModelScope, file, destination, modelName);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!canDownload) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("关闭", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        if (g.downloadBusy) {
            ImGui::ProgressBar(g.downloadProgress, ImVec2(-1, 0), g.downloadStatus.c_str());
        } else {
            ImGui::TextDisabled("%s", g.downloadStatus.c_str());
        }
        ImGui::EndPopup();
    }

    if (g.importModalRequested) {
        ImGui::OpenPopup("导入本地 GGUF");
        g.importModalRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("导入本地 GGUF", &g.importModalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("文件: %s", utf8(g.importPath).c_str());
        ImGui::Text("模型名");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##importName", g.importName, sizeof(g.importName));
        bool canImport = !g.importBusy && strlen(g.importName) > 0;
        if (!canImport) ImGui::BeginDisabled();
        if (ImGui::Button("导入", ImVec2(120, 0))) {
            std::wstring modelName = sanitizeModelName(opg::utf8ToWide(g.importName));
            startLocalImport(g.importPath, modelName);
            ImGui::CloseCurrentPopup();
        }
        if (!canImport) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g.renameModalRequested) {
        ImGui::OpenPopup("重命名模型");
        g.renameModalRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("重命名模型", &g.renameModalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("旧名称: %s", utf8(g.renameOld).c_str());
        ImGui::Text("新名称");
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText("##renameName", g.renameName, sizeof(g.renameName));
        if (ImGui::Button("确定", ImVec2(120, 0))) {
            std::wstring newName = sanitizeModelName(opg::utf8ToWide(g.renameName));
            startRenameModel(g.renameOld, newName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g.deleteModalRequested) {
        ImGui::OpenPopup("确认删除模型");
        g.deleteModalRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("确认删除模型", &g.deleteModalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("确定删除模型 %s 吗？", utf8(g.deleteName).c_str());
        ImGui::TextDisabled("此操作不可恢复。");
        if (ImGui::Button("删除", ImVec2(120, 0))) {
            startDeleteModel(g.deleteName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

static void toggleMaximize() {
    if (g_hwnd) ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

static bool titlebarButton(const char* id, const ImVec2& pos, const ImVec2& size,
                           int kind, float& animation) {
    ImGui::SetCursorPos(pos);
    ImGui::PushID(id);
    ImGui::InvisibleButton(id, size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopID();

    const float dt = ImGui::GetIO().DeltaTime;
    animation += ((hovered ? 1.0f : 0.0f) - animation) * std::min(1.0f, dt * 16.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    if (kind == 2) {
        background = hovered ? (active ? ImVec4(0.66f, 0.05f, 0.10f, 1.0f)
                                       : ImVec4(0.91f, 0.07f, 0.14f, 1.0f))
                             : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    } else if (hovered) {
        background = active ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive)
                            : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    }
    draw->AddRectFilled(min, max, ImGui::GetColorU32(background));

    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    if (kind == 0) {
        draw->AddLine(ImVec2(center.x - 8.0f, center.y), ImVec2(center.x + 8.0f, center.y), color, 1.8f);
    } else if (kind == 1) {
        if (IsZoomed(g_hwnd)) {
            draw->AddRect(ImVec2(center.x - 8.0f, center.y - 5.0f), ImVec2(center.x + 2.0f, center.y + 6.0f), color, 0.0f, 0, 1.6f);
            draw->AddRect(ImVec2(center.x - 4.0f, center.y - 8.0f), ImVec2(center.x + 8.0f, center.y + 3.0f), color, 0.0f, 0, 1.6f);
        } else {
            draw->AddRect(ImVec2(center.x - 7.0f, center.y - 7.0f), ImVec2(center.x + 7.0f, center.y + 7.0f), color, 0.0f, 0, 1.6f);
        }
    } else {
        draw->AddLine(ImVec2(center.x - 7.0f, center.y - 7.0f), ImVec2(center.x + 7.0f, center.y + 7.0f), color, 1.8f);
        draw->AddLine(ImVec2(center.x + 7.0f, center.y - 7.0f), ImVec2(center.x - 7.0f, center.y + 7.0f), color, 1.8f);
    }
    return clicked;
}

static ID3D11ShaderResourceView* g_appIconView = nullptr;

static void drawTitleBar() {
    const float height = 64.0f;
    const float buttonWidth = 48.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("##appTitleBar", ImVec2(-1.0f, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Button);

    const ImVec2 logoMin(origin.x + 24.0f, origin.y + 12.0f);
    const ImVec2 logoMax(origin.x + 64.0f, origin.y + 52.0f);
    if (g_appIconView) {
        draw->AddImage((ImTextureID)g_appIconView, logoMin, logoMax);
    } else {
        draw->AddRectFilled(logoMin, logoMax, ImGui::GetColorU32(accent), 8.0f);
    }
    draw->AddText(ImVec2(logoMax.x + 16.0f, origin.y + 15.0f),
                  ImGui::GetColorU32(ImGuiCol_Text), "Ollama Plus");
    draw->AddText(ImVec2(logoMax.x + 16.0f, origin.y + 38.0f),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), "模型 · 服务端 · 对话");

    float animation[3] = {0.0f, 0.0f, 0.0f};
    const ImVec2 closePos(width - buttonWidth, 0.0f);
    const ImVec2 maxPos(closePos.x - buttonWidth, 0.0f);
    const ImVec2 minPos(maxPos.x - buttonWidth, 0.0f);
    if (titlebarButton("##close", closePos, ImVec2(buttonWidth, height), 2, animation[2]))
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    if (titlebarButton("##maximize", maxPos, ImVec2(buttonWidth, height), 1, animation[1]))
        toggleMaximize();
    if (titlebarButton("##minimize", minPos, ImVec2(buttonWidth, height), 0, animation[0]))
        ShowWindow(g_hwnd, SW_MINIMIZE);

    ImGui::SetCursorPos(ImVec2(84.0f, 0.0f));
    ImGui::InvisibleButton("##titleDrag", ImVec2(std::max(40.0f, minPos.x - 84.0f), height),
                           ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) toggleMaximize();
    static bool dragging = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        if (!dragging) {
            dragging = true;
            ReleaseCapture();
            SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
    } else {
        dragging = false;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
static void drawNavIcon(ImDrawList* draw, const ImVec2& center, int kind, ImU32 color) {
    switch (kind) {
        case 0:
            draw->AddCircle(ImVec2(center.x - 2.0f, center.y - 2.0f), 7.0f, color, 24, 2.0f);
            draw->AddLine(ImVec2(center.x + 3.0f, center.y + 3.0f), ImVec2(center.x + 10.0f, center.y + 10.0f), color, 2.4f);
            break;
        case 1:
            draw->AddRect(ImVec2(center.x - 10.0f, center.y - 8.0f), ImVec2(center.x + 10.0f, center.y + 8.0f), color, 4.0f, 0, 2.0f);
            draw->AddLine(ImVec2(center.x - 6.0f, center.y - 2.0f), ImVec2(center.x + 6.0f, center.y - 2.0f), color, 2.0f);
            draw->AddLine(ImVec2(center.x - 6.0f, center.y + 3.0f), ImVec2(center.x + 2.0f, center.y + 3.0f), color, 2.0f);
            break;
        case 2:
            draw->AddRect(ImVec2(center.x - 10.0f, center.y - 7.0f), ImVec2(center.x + 10.0f, center.y + 5.0f), color, 3.0f, 0, 2.0f);
            draw->AddLine(ImVec2(center.x, center.y + 5.0f), ImVec2(center.x, center.y + 9.0f), color, 2.0f);
            draw->AddLine(ImVec2(center.x - 5.0f, center.y + 9.0f), ImVec2(center.x + 5.0f, center.y + 9.0f), color, 2.0f);
            break;
        default:
            draw->AddLine(ImVec2(center.x - 9.0f, center.y - 4.0f), ImVec2(center.x + 9.0f, center.y - 4.0f), color, 2.2f);
            draw->AddLine(ImVec2(center.x - 9.0f, center.y + 4.0f), ImVec2(center.x + 9.0f, center.y + 4.0f), color, 2.2f);
            draw->AddCircleFilled(ImVec2(center.x - 3.0f, center.y - 4.0f), 3.0f, color);
            draw->AddCircleFilled(ImVec2(center.x + 5.0f, center.y + 4.0f), 3.0f, color);
            break;
    }
}

static bool sidebarItem(const char* id, const ImVec2& pos, const ImVec2& size,
                        const char* label, int iconKind, bool selected, float& animation) {
    ImGui::SetCursorPos(pos);
    ImGui::PushID(id);
    ImGui::InvisibleButton(id, size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopID();

    const float dt = ImGui::GetIO().DeltaTime;
    animation += ((hovered || selected ? 1.0f : 0.0f) - animation) * std::min(1.0f, dt * 12.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    const ImVec4 fill = lerpColor(ImVec4(0, 0, 0, 0), selected ? accent : frame, selected ? animation : animation * 0.65f);
    draw->AddRectFilled(min, max, ImGui::GetColorU32(fill), 9.0f);
    if (selected) {
        draw->AddRectFilled(min, ImVec2(min.x + 4.0f, max.y), ImGui::GetColorU32(accent), 3.0f);
        draw->AddRect(min, max, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.45f)), 9.0f);
    }

    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    drawNavIcon(draw, ImVec2(min.x + 30.0f, (min.y + max.y) * 0.5f), iconKind,
                ImGui::GetColorU32(lerpColor(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), accent, animation)));
    draw->AddText(ImVec2(min.x + 52.0f, min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f), text, label);
    return clicked;
}

static bool themeChip(const char* id, const ImVec2& pos, const ImVec2& size,
                      const char* label, const ImVec4& color, bool selected, float& animation) {
    ImGui::SetCursorPos(pos);
    ImGui::PushID(id);
    ImGui::InvisibleButton(id, size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopID();

    const float dt = ImGui::GetIO().DeltaTime;
    animation += ((hovered || selected ? 1.0f : 0.0f) - animation) * std::min(1.0f, dt * 12.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 border = selected ? ImGui::GetStyleColorVec4(ImGuiCol_Button)
                                   : ImGui::GetStyleColorVec4(ImGuiCol_Border);
    draw->AddRectFilled(min, max,
                        ImGui::GetColorU32(lerpColor(frame, border, animation * 0.30f)), 9.0f);
    draw->AddRect(min, max,
                  ImGui::GetColorU32(lerpColor(border, ImGui::GetStyleColorVec4(ImGuiCol_Text), selected ? animation * 0.25f : 0.0f)),
                  9.0f, 0, selected ? 2.2f : 1.0f);
    draw->AddCircleFilled(ImVec2(min.x + 16.0f, (min.y + max.y) * 0.5f), 5.0f, ImGui::GetColorU32(color));
    draw->AddText(ImVec2(min.x + 28.0f, min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_Text), label);
    return clicked;
}

static void drawUI() {
    std::lock_guard<std::recursive_mutex> lock(g.stateMutex);
    static float pageAlpha = 1.0f;
    static int lastPage = -1;
    static float navAnim[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static float themeAnim[3] = {1.0f, 0.0f, 0.0f};
    const float deltaTime = ImGui::GetIO().DeltaTime;
    if (lastPage != static_cast<int>(g.page)) {
        lastPage = static_cast<int>(g.page);
        pageAlpha = 0.0f;
    }
    pageAlpha = std::min(1.0f, pageAlpha + deltaTime * 4.5f);
    for (int i = 0; i < 3; ++i) {
        const float target = g.theme == i ? 1.0f : 0.0f;
        themeAnim[i] += (target - themeAnim[i]) * std::min(1.0f, deltaTime * 10.0f);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##OllamaPlusGUI", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);

    drawTitleBar();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    if (ImGui::BeginChild("##sidebar", ImVec2(300.0f, 0.0f), ImGuiChildFlags_None)) {
        const ImVec2 panelMin = ImGui::GetCursorScreenPos();
        const ImVec2 panelMax(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
        ImGui::GetWindowDrawList()->AddRectFilled(panelMin, panelMax,
            ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_ChildBg)));

        const char* pages[] = {"魔搭搜索", "本地模型", "服务端", "设置"};
        Page pageValues[] = {Page::Search, Page::Models, Page::Service, Page::Settings};
        for (int i = 0; i < 4; ++i) {
            const float y = 28.0f + static_cast<float>(i) * 66.0f;
            if (sidebarItem(pages[i], ImVec2(24.0f, y), ImVec2(252.0f, 50.0f),
                            pages[i], i, g.page == pageValues[i], navAnim[i])) {
                g.page = pageValues[i];
            }
        }

        const float themeLabelY = 28.0f + 4.0f * 66.0f + 24.0f;
        ImGui::SetCursorPos(ImVec2(24.0f, themeLabelY));
        ImGui::TextDisabled("外观");
        const char* themes[] = {"黑", "红", "白"};
        const ImVec4 swatches[] = {ImVec4(0.07f, 0.07f, 0.09f, 1.0f), ImVec4(0.88f, 0.10f, 0.14f, 1.0f), ImVec4(0.98f, 0.98f, 1.0f, 1.0f)};
        for (int i = 0; i < 3; ++i) {
            if (themeChip(themes[i], ImVec2(24.0f + i * 88.0f, themeLabelY + 30.0f), ImVec2(78.0f, 42.0f),
                          themes[i], swatches[i], g.theme == i, themeAnim[i])) {
                if (g.theme != i) {
                    g.theme = i;
                    applyTheme();
                }
            }
        }

        ImGui::SetCursorPos(ImVec2(24.0f, ImGui::GetWindowHeight() - 44.0f));
        ImGui::TextDisabled("v%s", kAppVersion);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pageAlpha);
    if (ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        switch (g.page) {
            case Page::Search: drawSearchPage(); break;
            case Page::Models: drawModelsPage(); break;
            case Page::Service: drawServicePage(); break;
            case Page::Settings: drawSettingsPage(); break;
        }
    }
    ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
    ImGui::End();

    if (g.chatOpen) drawChatWindow();
    drawModals();
    drawDownloadOverlay();
}

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void LoadAppFont() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    const wchar_t* candidates[] = {
        L"C:\\Windows\\Fonts\\msyh.ttc",
        L"C:\\Windows\\Fonts\\msyh.ttf",
        L"C:\\Windows\\Fonts\\simhei.ttf",
        L"C:\\Windows\\Fonts\\simsun.ttc"
    };
    for (const wchar_t* candidate : candidates) {
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(opg::wideToUtf8(candidate).c_str(), 18.0f, &config,
                                                        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font) {
                io.FontDefault = font;
                return;
            }
        }
    }
    io.Fonts->AddFontDefault();
}

static bool CreateAppIconTexture() {
    if (g_appIconView) return true;
    if (!g_pd3dDevice) return false;

    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(2), RT_RCDATA);
    if (!resource) return false;
    const HGLOBAL handle = LoadResource(nullptr, resource);
    if (!handle) return false;
    const void* data = LockResource(handle);
    const DWORD size = SizeofResource(nullptr, resource);
    if (!data || size == 0) return false;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* texture = nullptr;
    bool ok = false;
    std::vector<unsigned char> pixels;

    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) break;
        IStream* stream = ::SHCreateMemStream(static_cast<const BYTE*>(data), size);
    if (!stream) break;
    const HRESULT decodeHr = factory->CreateDecoderFromStream(stream, nullptr,
                                                              WICDecodeMetadataCacheOnDemand, &decoder);
    stream->Release();
    if (FAILED(decodeHr)) break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom))) break;

        UINT width = 0, height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) break;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4,
                                         static_cast<UINT>(pixels.size()), pixels.data()))) break;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = pixels.data();
        initData.SysMemPitch = width * 4;
        if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &initData, &texture))) break;

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        viewDesc.Format = desc.Format;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipLevels = 1;
        if (FAILED(g_pd3dDevice->CreateShaderResourceView(texture, &viewDesc, &g_appIconView))) break;
        ok = true;
    } while (false);

    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return ok;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    int width = 1240;
    int height = 800;
    RECT workArea{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        width = std::min(width, static_cast<int>(workArea.right - workArea.left) - 60);
        height = std::min(height, static_cast<int>(workArea.bottom - workArea.top) - 40);
    }
    g_hwnd = CreateWindowExW(0, kClassName, L"Ollama Plus GUI", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                             nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) return 1;
    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(kClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(g_hwnd, showCommand);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    applyTheme();
    LoadAppFont();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CreateAppIconTexture();

    startModelListRefresh();
    startOllamaInfo();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawUI();
        ImGui::Render();

        const float clearColor[] = {0.03f, 0.03f, 0.04f, 1.00f};
        ID3D11RenderTargetView* target = g_mainRenderTargetView;
        g_pd3dDeviceContext->OMSetRenderTargets(1, &target, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(kClassName, wc.hInstance);
    return 0;
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
                                                featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
                                                &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
                                            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
                                            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
        backBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wParam) return 0;
            break;
        case WM_NCHITTEST: {
            if (IsZoomed(hWnd)) break;
            POINT point{(static_cast<short>(LOWORD(lParam))), (static_cast<short>(HIWORD(lParam)))};
            ScreenToClient(hWnd, &point);
            RECT rect{};
            GetClientRect(hWnd, &rect);
            const LONG border = 8L;
            const bool left = point.x < border;
            const bool right = point.x >= rect.right - border;
            const bool top = point.y < border;
            const bool bottom = point.y >= rect.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            RECT client{};
            GetClientRect(hWnd, &client);
            const LONG dragRight = client.right - 144L;
            if (point.y < 64L && point.x < dragRight) return HTCAPTION;
            break;
        }
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED:
            SetWindowPos(hWnd, nullptr, reinterpret_cast<RECT*>(lParam)->left,
                         reinterpret_cast<RECT*>(lParam)->top,
                         reinterpret_cast<RECT*>(lParam)->right - reinterpret_cast<RECT*>(lParam)->left,
                         reinterpret_cast<RECT*>(lParam)->bottom - reinterpret_cast<RECT*>(lParam)->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

















