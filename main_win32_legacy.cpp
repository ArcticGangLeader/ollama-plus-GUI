
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "http.hpp"
#include "process.hpp"
#include "proxy.hpp"
#include <dxgi.h>


namespace fs = std::filesystem;
using opg::JsonValue;

#ifndef WC_LISTVIEW
#define WC_LISTVIEW L"SysListView32"
#endif

#ifndef WC_TABCONTROL
#define WC_TABCONTROL L"SysTabControl32"
#endif

enum ControlId {
    IDC_TAB = 100,
    IDC_SEARCH_EDIT = 101,
    IDC_SEARCH_BUTTON = 102,
    IDC_SEARCH_LIST = 103,
    IDC_MODEL_REFRESH = 110,
    IDC_MODEL_DELETE = 111,
    IDC_MODEL_IMPORT = 112,
    IDC_MODEL_RUN = 114,
    IDC_MODEL_RENAME = 115,
    IDC_MODEL_LIST = 113,
    IDC_FILE_LIST = 120,
    IDC_FILE_DOWNLOAD = 121,
    IDC_FILE_CANCEL = 122,
    IDC_FILE_NAME = 123,
    IDC_FILE_OPEN_PAGE = 124,
    IDC_FILE_PROGRESS = 125,
    IDC_ERROR_CLOSE = 130,
    IDC_INPUT_OK = 140,
    IDC_INPUT_CANCEL = 141,
    IDC_INPUT_EDIT = 142,

    IDC_SERVICE_PORT = 150,
    IDC_SERVICE_OPEN = 151,
    IDC_SERVICE_CLOSE = 152,
    IDC_SERVICE_KEY_REQUIRED = 153,
    IDC_SERVICE_API_KEY = 154,
    IDC_SERVICE_SAVE_KEY = 155,
    IDC_SERVICE_BLOCK_LIST = 156,
    IDC_SERVICE_ADD_BLOCK = 157,
    IDC_SERVICE_REMOVE_BLOCK = 158,
    IDC_SERVICE_ADMIN_RESTART = 159,
    IDC_SERVICE_FIREWALL_ADD = 160,
    IDC_SERVICE_FIREWALL_REMOVE = 161,
    IDC_SERVICE_FIREWALL_STATUS = 162,

    IDC_CHAT_MODEL = 170,
    IDC_CHAT_HISTORY = 171,
    IDC_CHAT_INPUT = 172,
    IDC_CHAT_SEND = 173,
    IDC_CHAT_IMPORT = 174,
    IDC_CHAT_REMOVE = 175,
    IDC_CHAT_CLEAR = 176,
    IDC_CHAT_FILES = 177,
    IDC_CHAT_STATUS = 178,
    IDC_CHAT_THINK = 179
};

enum AppMessage {
    WM_APP_SEARCH_DONE = WM_APP + 1,
    WM_APP_MODELS_DONE,
    WM_APP_COMMAND_DONE,
    WM_APP_FILES_DONE,
    WM_APP_DOWNLOAD_PROGRESS,
    WM_APP_DOWNLOAD_DONE,
    WM_APP_CHAT_CHUNK,
    WM_APP_CHAT_DONE
};

struct SearchResult {
    std::wstring owner;
    std::wstring repository;
    std::wstring revision;
    long long size = 0;
    int stars = 0;
    bool gguf = false;
    long long estimatedSize = 0;
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

struct SearchPayload {
    std::vector<SearchResult> results;
    std::wstring error;
};

struct FilesPayload {
    std::vector<GgufFile> files;
    std::wstring error;
};

struct ModelsPayload {
    std::vector<LocalModel> models;
    std::wstring error;
};

struct CommandPayload {
    bool success = false;
    std::wstring output;
    std::wstring error;
    std::wstring title;
    bool refreshModels = false;
};

struct ProgressPayload {
    unsigned long long downloaded = 0;
    unsigned long long total = 0;
};

struct DownloadPayload {
    bool success = false;
    std::wstring error;
    std::wstring targetPath;
    std::wstring modelfilePath;
    std::wstring modelName;
};

struct ChatMessage {
    std::wstring sender;
    std::wstring role;
    std::wstring content;
    std::vector<std::string> images;
};

struct ChatAttachment {
    std::wstring path;
    std::wstring name;
    bool image = false;
    std::wstring text;
    std::string base64;
    long long size = 0;
};

struct ChatPayload {
    std::wstring model;
    std::wstring chunk;
    bool thinking = false;
    bool thinkingStart = false;
    bool afterThinking = false;
    std::wstring full;
    std::wstring error;
    bool done = false;
};

struct ChatRequestPayload {
    std::wstring model;
    std::vector<ChatMessage> history;
    std::wstring error;
    std::wstring assistant;
};

struct AppState {
    HWND main = nullptr;
    HWND tab = nullptr;
    HWND searchLabel = nullptr;
    HWND searchEdit = nullptr;
    HWND searchButton = nullptr;
    HWND searchList = nullptr;
    HWND modelRefresh = nullptr;
    HWND modelDelete = nullptr;
    HWND modelImport = nullptr;
    HWND modelRun = nullptr;
    HWND modelRename = nullptr;
    HWND modelList = nullptr;
    HWND status = nullptr;

    HWND fileWindow = nullptr;
    HWND fileLabel = nullptr;
    HWND fileList = nullptr;
    HWND fileNameLabel = nullptr;
    HWND fileNameEdit = nullptr;
    HWND fileDownload = nullptr;
    HWND fileCancel = nullptr;
    HWND fileOpenPage = nullptr;
    HWND fileProgress = nullptr;
    HWND fileStatus = nullptr;

    HWND serviceTitle = nullptr;
    HWND serviceStatusLabel = nullptr;
    HWND servicePortLabel = nullptr;
    HWND servicePortEdit = nullptr;
    HWND serviceOpen = nullptr;
    HWND serviceClose = nullptr;
    HWND serviceAdminRestart = nullptr;
    HWND serviceFirewallAdd = nullptr;
    HWND serviceFirewallRemove = nullptr;
    HWND serviceFirewallStatus = nullptr;
    HWND serviceKeyRequired = nullptr;
    HWND serviceApiKeyLabel = nullptr;
    HWND serviceApiKeyEdit = nullptr;
    HWND serviceSaveKey = nullptr;
    HWND serviceBlockList = nullptr;
    HWND serviceAddBlock = nullptr;
    HWND serviceRemoveBlock = nullptr;
    HWND serviceStatsLabel = nullptr;
    HWND serviceNetworkLabel = nullptr;
    HWND serviceBlockedLabel = nullptr;

    HWND chatWindow = nullptr;
    HWND chatModelLabel = nullptr;
    HWND chatModelCombo = nullptr;
    HWND chatHistory = nullptr;
    HWND chatInput = nullptr;
    HWND chatSend = nullptr;
    HWND chatStatusLabel = nullptr;
    HWND chatFilesLabel = nullptr;
    HWND chatFiles = nullptr;
    HWND chatImport = nullptr;
    HWND chatRemoveFile = nullptr;
    HWND chatClear = nullptr;
    HWND chatThink = nullptr;
    std::wstring chatCurrentModel;

    std::vector<SearchResult> searchResults;
    std::vector<GgufFile> ggufFiles;
    std::vector<LocalModel> localModels;
    SearchResult selectedModel;
    std::atomic<bool> fileThreadRunning{false};
    std::atomic<bool> downloadThreadRunning{false};
    std::atomic<bool> commandThreadRunning{false};
    std::atomic<bool> searchThreadRunning{false};
    std::atomic<bool> cancelDownload{false};
    bool recommendationLoaded = false;
    std::vector<ChatMessage> chatHistoryMessages;
    std::vector<ChatAttachment> chatAttachments;
    std::atomic<bool> chatThreadRunning{false};

    HFONT normalFont = nullptr;
    HFONT boldFont = nullptr;
};

static AppState g;
static opg::ProxyServer g_proxy;

static const wchar_t* mainClassName = L"OllamaPlusGuiMainWindow";
static const wchar_t* fileWindowClassName = L"OllamaPlusGuiFileWindow";
static const wchar_t* errorWindowClassName = L"OllamaPlusGuiErrorWindow";
static const wchar_t* inputWindowClassName = L"OllamaPlusGuiInputWindow";
static const wchar_t* chatWindowClassName = L"OllamaPlusGuiChatWindow";

static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK FileWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ErrorWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK InputWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ChatWndProc(HWND, UINT, WPARAM, LPARAM);
static void importChatFiles(HWND hwnd);

static std::wstring trim(const std::wstring& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && iswspace(value[begin])) ++begin;
    while (end > begin && iswspace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

static std::wstring lower(const std::wstring& value) {
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(), towlower);
    return result;
}

static std::wstring formatBytes(long long value) {
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
    return buffer;
}

static std::wstring fileNameOnly(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

static std::wstring fileDirectory(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return pos == 0 ? path.substr(0, 1) : path.substr(0, pos);
}

static std::wstring fileExtensionLower(const std::wstring& path) {
    std::wstring name = fileNameOnly(path);
    size_t pos = name.rfind(L'.');
    if (pos == std::wstring::npos) return L"";
    return lower(name.substr(pos));
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

static std::wstring defaultModelNameFromGguf(const std::wstring& repository, const std::wstring& fileName) {
    std::wstring base = fileName;
    size_t pos = lower(base).rfind(L".gguf");
    if (pos != std::wstring::npos) base = base.substr(0, pos);
    return sanitizeModelName(repository + L"-" + base);
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

static std::wstring ollamaLocationDescription() {
    std::wstring path;
    if (findOllamaExe(path)) return L"已检测到本地 ollama.exe：" + path;
    return L"未在程序目录检测到 ollama.exe，将使用 PATH 中的 ollama 命令。";
}
static void setStatus(const std::wstring& text) {
    if (g.status) SetWindowTextW(g.status, text.c_str());
}

static void centerWindow(HWND window, HWND owner = nullptr) {
    RECT rect{};
    GetWindowRect(window, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    if (owner) {
        RECT ownerRect{};
        if (GetWindowRect(owner, &ownerRect)) {
            x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
            y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
        }
    }
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void createFonts() {
    g.normalFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g.boldFont = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

static void applyFonts() {
    HWND controls[] = {g.tab, g.searchLabel, g.searchEdit, g.searchButton, g.searchList,
                       g.modelRefresh, g.modelDelete, g.modelImport, g.modelRename, g.modelRun, g.modelList, g.status,
                       g.serviceTitle, g.serviceStatusLabel, g.servicePortLabel, g.servicePortEdit, g.serviceOpen, g.serviceClose, g.serviceAdminRestart, g.serviceFirewallAdd, g.serviceFirewallRemove, g.serviceFirewallStatus,
                       g.serviceKeyRequired, g.serviceApiKeyLabel, g.serviceApiKeyEdit, g.serviceSaveKey, g.serviceBlockList, g.serviceAddBlock, g.serviceRemoveBlock,
                       g.serviceStatsLabel, g.serviceNetworkLabel, g.serviceBlockedLabel,
                       g.chatModelLabel, g.chatModelCombo, g.chatHistory, g.chatInput, g.chatSend, g.chatStatusLabel,
                       g.chatFilesLabel, g.chatFiles, g.chatImport, g.chatRemoveFile, g.chatClear,
                       g.fileLabel, g.fileList, g.fileNameLabel, g.fileNameEdit,
                       g.fileDownload, g.fileCancel, g.fileOpenPage, g.fileProgress, g.fileStatus};
    for (HWND control : controls) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    }
    if (g.fileLabel) SendMessageW(g.fileLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g.boldFont), TRUE);
}

static void addListColumn(HWND list, int index, const wchar_t* text, int width, int format = LVCFMT_LEFT) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    column.fmt = format;
    column.cx = width;
    column.pszText = const_cast<LPWSTR>(text);
    ListView_InsertColumn(list, index, &column);
}

static void createMainControls(HWND parent) {
    g.tab = CreateWindowExW(0, WC_TABCONTROL, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_TAB),
                            GetModuleHandleW(nullptr), nullptr);

    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(L"魔搭搜索");
    TabCtrl_InsertItem(g.tab, 0, &item);
    item.pszText = const_cast<LPWSTR>(L"本地模型");
    TabCtrl_InsertItem(g.tab, 1, &item);
    item.pszText = const_cast<LPWSTR>(L"服务端");
    TabCtrl_InsertItem(g.tab, 2, &item);

    g.searchLabel = CreateWindowExW(0, WC_STATICW, L"搜索魔搭社区模型（仅显示 GGUF 模型）：",
                                    WS_CHILD | WS_VISIBLE, 0, 0, 100, 24, parent, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    g.searchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   0, 0, 100, 28, parent, reinterpret_cast<HMENU>(IDC_SEARCH_EDIT),
                                   GetModuleHandleW(nullptr), nullptr);
    g.searchButton = CreateWindowExW(0, WC_BUTTONW, L"搜索",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 90, 30, parent, reinterpret_cast<HMENU>(IDC_SEARCH_BUTTON),
                                     GetModuleHandleW(nullptr), nullptr);

    g.searchList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                                   0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_SEARCH_LIST),
                                   GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(g.searchList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    addListColumn(g.searchList, 0, L"模型", 420);
    addListColumn(g.searchList, 1, L"作者", 160);
    addListColumn(g.searchList, 2, L"仓库大小", 130, LVCFMT_RIGHT);
    addListColumn(g.searchList, 3, L"Stars", 80, LVCFMT_RIGHT);

    g.modelRefresh = CreateWindowExW(0, WC_BUTTONW, L"刷新列表",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 110, 30, parent,
                                     reinterpret_cast<HMENU>(IDC_MODEL_REFRESH), GetModuleHandleW(nullptr), nullptr);
    g.modelDelete = CreateWindowExW(0, WC_BUTTONW, L"删除所选模型",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 140, 30, parent,
                                    reinterpret_cast<HMENU>(IDC_MODEL_DELETE), GetModuleHandleW(nullptr), nullptr);
    g.modelImport = CreateWindowExW(0, WC_BUTTONW, L"导入本地 GGUF",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 150, 30, parent,
                                    reinterpret_cast<HMENU>(IDC_MODEL_IMPORT), GetModuleHandleW(nullptr), nullptr);

    g.modelRename = CreateWindowExW(0, WC_BUTTONW, L"重命名",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 130, 30, parent,
                                 reinterpret_cast<HMENU>(IDC_MODEL_RENAME), GetModuleHandleW(nullptr), nullptr);

    g.modelRun = CreateWindowExW(0, WC_BUTTONW, L"运行模型",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 150, 30, parent,
                                 reinterpret_cast<HMENU>(IDC_MODEL_RUN), GetModuleHandleW(nullptr), nullptr);

    g.modelList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                                  0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_MODEL_LIST),
                                  GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(g.modelList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    addListColumn(g.modelList, 0, L"模型名称", 420);
    addListColumn(g.modelList, 1, L"ID", 160);
    addListColumn(g.modelList, 2, L"大小", 130, LVCFMT_RIGHT);
    addListColumn(g.modelList, 3, L"修改时间", 220);

    g.serviceTitle = CreateWindowExW(0, WC_STATICW, L"服务端反向代理", WS_CHILD | WS_VISIBLE,
                                     0, 0, 200, 28, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.serviceStatusLabel = CreateWindowExW(0, WC_STATICW, L"服务端已关闭。", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
                                           0, 0, 100, 24, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.servicePortLabel = CreateWindowExW(0, WC_STATICW, L"外部端口：", WS_CHILD | WS_VISIBLE,
                                         0, 0, 90, 24, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.servicePortEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"11435",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 100, 28,
                                        parent, reinterpret_cast<HMENU>(IDC_SERVICE_PORT), GetModuleHandleW(nullptr), nullptr);
    g.serviceOpen = CreateWindowExW(0, WC_BUTTONW, L"开放服务端",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 130, 30, parent,
                                    reinterpret_cast<HMENU>(IDC_SERVICE_OPEN), GetModuleHandleW(nullptr), nullptr);
    g.serviceClose = CreateWindowExW(0, WC_BUTTONW, L"关闭服务端",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 120, 30, parent,
                                     reinterpret_cast<HMENU>(IDC_SERVICE_CLOSE), GetModuleHandleW(nullptr), nullptr);
    g.serviceAdminRestart = CreateWindowExW(0, WC_BUTTONW, L"以管理员重启",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 140, 30, parent,
                                            reinterpret_cast<HMENU>(IDC_SERVICE_ADMIN_RESTART), GetModuleHandleW(nullptr), nullptr);
    g.serviceFirewallAdd = CreateWindowExW(0, WC_BUTTONW, L"添加防火墙规则",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 170, 30, parent,
                                           reinterpret_cast<HMENU>(IDC_SERVICE_FIREWALL_ADD), GetModuleHandleW(nullptr), nullptr);
    g.serviceFirewallRemove = CreateWindowExW(0, WC_BUTTONW, L"删除防火墙规则",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 170, 30, parent,
                                              reinterpret_cast<HMENU>(IDC_SERVICE_FIREWALL_REMOVE), GetModuleHandleW(nullptr), nullptr);
    g.serviceFirewallStatus = CreateWindowExW(0, WC_BUTTONW, L"查看防火墙规则",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 170, 30, parent,
                                              reinterpret_cast<HMENU>(IDC_SERVICE_FIREWALL_STATUS), GetModuleHandleW(nullptr), nullptr);
    g.serviceKeyRequired = CreateWindowExW(0, WC_BUTTONW, L"启用 API Key 验证",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           0, 0, 180, 26, parent,
                                           reinterpret_cast<HMENU>(IDC_SERVICE_KEY_REQUIRED), GetModuleHandleW(nullptr), nullptr);
    g.serviceApiKeyLabel = CreateWindowExW(0, WC_STATICW, L"API Key：", WS_CHILD | WS_VISIBLE,
                                           0, 0, 80, 24, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.serviceApiKeyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          0, 0, 300, 28, parent, reinterpret_cast<HMENU>(IDC_SERVICE_API_KEY),
                                          GetModuleHandleW(nullptr), nullptr);
    g.serviceSaveKey = CreateWindowExW(0, WC_BUTTONW, L"保存 Key 设置",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 140, 30, parent,
                                       reinterpret_cast<HMENU>(IDC_SERVICE_SAVE_KEY), GetModuleHandleW(nullptr), nullptr);
    g.serviceBlockedLabel = CreateWindowExW(0, WC_STATICW, L"已拉黑 IP：", WS_CHILD | WS_VISIBLE,
                                            0, 0, 120, 24, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.serviceBlockList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                         0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_SERVICE_BLOCK_LIST),
                                         GetModuleHandleW(nullptr), nullptr);
    g.serviceAddBlock = CreateWindowExW(0, WC_BUTTONW, L"拉黑 IP",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 110, 30, parent,
                                        reinterpret_cast<HMENU>(IDC_SERVICE_ADD_BLOCK), GetModuleHandleW(nullptr), nullptr);
    g.serviceRemoveBlock = CreateWindowExW(0, WC_BUTTONW, L"解除拉黑",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 120, 30, parent,
                                           reinterpret_cast<HMENU>(IDC_SERVICE_REMOVE_BLOCK), GetModuleHandleW(nullptr), nullptr);
    g.serviceStatsLabel = CreateWindowExW(0, WC_STATICW, L"总 token：0", WS_CHILD | WS_VISIBLE,
                                          0, 0, 100, 48, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.serviceNetworkLabel = CreateWindowExW(0, WC_STATICW, L"网络状态：未连接", WS_CHILD | WS_VISIBLE,
                                            0, 0, 100, 48, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.status = CreateWindowExW(0, WC_STATIC, L"就绪。搜索后双击模型可选择 GGUF 文件。",
                               WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 0, 0, 100, 24, parent, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
}

static void layoutMain(int width, int height) {
    if (!g.tab) return;
    int margin = 14;
    int tabX = margin;
    int tabY = margin;
    int tabWidth = std::max(200, width - margin * 2);
    int tabHeight = std::max(100, height - margin * 2 - 30);
    MoveWindow(g.tab, tabX, tabY, tabWidth, tabHeight, TRUE);

    int pageX = tabX + 10;
    int pageY = tabY + 36;
    int pageW = tabWidth - 22;
    int pageH = tabHeight - 48;

    int editWidth = std::max(200, pageW - 110);
    MoveWindow(g.searchLabel, pageX, pageY, pageW, 24, TRUE);
    MoveWindow(g.searchEdit, pageX, pageY + 28, editWidth, 28, TRUE);
    MoveWindow(g.searchButton, pageX + editWidth + 10, pageY + 26, 100, 32, TRUE);
    MoveWindow(g.searchList, pageX, pageY + 70, pageW, std::max(80, pageH - 70), TRUE);

    MoveWindow(g.modelRefresh, pageX, pageY, 110, 30, TRUE);
    MoveWindow(g.modelDelete, pageX + 120, pageY, 150, 30, TRUE);
    MoveWindow(g.modelImport, pageX + 280, pageY, 160, 30, TRUE);
    MoveWindow(g.modelRun, pageX + 450, pageY, 130, 30, TRUE);
    MoveWindow(g.modelRename, pageX + 590, pageY, 120, 30, TRUE);
    MoveWindow(g.modelList, pageX, pageY + 42, pageW, std::max(80, pageH - 42), TRUE);
    int serviceTitleW = std::max(220, pageW);
    MoveWindow(g.serviceTitle, pageX, pageY, serviceTitleW, 28, TRUE);
    MoveWindow(g.serviceStatusLabel, pageX, pageY + 34, serviceTitleW, 24, TRUE);
    MoveWindow(g.servicePortLabel, pageX, pageY + 70, 90, 24, TRUE);
    MoveWindow(g.servicePortEdit, pageX + 94, pageY + 66, 110, 28, TRUE);
    MoveWindow(g.serviceOpen, pageX + 220, pageY + 66, 130, 30, TRUE);
    MoveWindow(g.serviceClose, pageX + 360, pageY + 66, 120, 30, TRUE);
    MoveWindow(g.serviceAdminRestart, pageX + 490, pageY + 66, 140, 30, TRUE);
    MoveWindow(g.serviceFirewallAdd, pageX, pageY + 106, 170, 30, TRUE);
    MoveWindow(g.serviceFirewallRemove, pageX + 180, pageY + 106, 170, 30, TRUE);
    MoveWindow(g.serviceFirewallStatus, pageX + 360, pageY + 106, 170, 30, TRUE);
    MoveWindow(g.serviceKeyRequired, pageX, pageY + 150, 190, 26, TRUE);
    MoveWindow(g.serviceApiKeyLabel, pageX + 200, pageY + 154, 80, 24, TRUE);
    MoveWindow(g.serviceApiKeyEdit, pageX + 284, pageY + 150, 280, 28, TRUE);
    MoveWindow(g.serviceSaveKey, pageX + 574, pageY + 150, 140, 30, TRUE);
    MoveWindow(g.serviceBlockedLabel, pageX, pageY + 194, 120, 24, TRUE);
    MoveWindow(g.serviceBlockList, pageX, pageY + 224, 280, std::max(100, pageH - 228), TRUE);
    MoveWindow(g.serviceAddBlock, pageX + 290, pageY + 224, 110, 30, TRUE);
    MoveWindow(g.serviceRemoveBlock, pageX + 290, pageY + 262, 120, 30, TRUE);
    int statsX = pageX + 430;
    int statsW = std::max(260, pageW - 444);
    MoveWindow(g.serviceStatsLabel, statsX, pageY + 194, statsW, 52, TRUE);
    MoveWindow(g.serviceNetworkLabel, statsX, pageY + 254, statsW, 52, TRUE);    MoveWindow(g.status, margin, height - 26, std::max(100, width - margin * 2), 22, TRUE);
}

static void showActivePage() {
    int selected = TabCtrl_GetCurSel(g.tab);
    HWND searchControls[] = {g.searchLabel, g.searchEdit, g.searchButton, g.searchList};
    HWND modelControls[] = {g.modelRefresh, g.modelDelete, g.modelImport, g.modelRename, g.modelRun, g.modelList};
    HWND serviceControls[] = {g.serviceTitle, g.serviceStatusLabel, g.servicePortLabel, g.servicePortEdit,
                              g.serviceOpen, g.serviceClose, g.serviceAdminRestart, g.serviceFirewallAdd, g.serviceFirewallRemove, g.serviceFirewallStatus, g.serviceKeyRequired,
                              g.serviceApiKeyLabel, g.serviceApiKeyEdit, g.serviceSaveKey, g.serviceBlockedLabel,
                              g.serviceBlockList, g.serviceAddBlock, g.serviceRemoveBlock,
                              g.serviceStatsLabel, g.serviceNetworkLabel};
    std::vector<HWND> visible;
    if (selected == 0) visible.assign(std::begin(searchControls), std::end(searchControls));
    else if (selected == 1) visible.assign(std::begin(modelControls), std::end(modelControls));
    else if (selected == 2) visible.assign(std::begin(serviceControls), std::end(serviceControls));

    HWND allControls[] = {
        g.searchLabel, g.searchEdit, g.searchButton, g.searchList,
        g.modelRefresh, g.modelDelete, g.modelImport, g.modelRename, g.modelRun, g.modelList,
        g.serviceTitle, g.serviceStatusLabel, g.servicePortLabel, g.servicePortEdit,
        g.serviceOpen, g.serviceClose, g.serviceAdminRestart, g.serviceFirewallAdd, g.serviceFirewallRemove, g.serviceFirewallStatus, g.serviceKeyRequired,
        g.serviceApiKeyLabel, g.serviceApiKeyEdit, g.serviceSaveKey, g.serviceBlockedLabel,
        g.serviceBlockList, g.serviceAddBlock, g.serviceRemoveBlock,
        g.serviceStatsLabel, g.serviceNetworkLabel
    };
    for (HWND control : allControls) {
        if (control) ShowWindow(control, SW_HIDE);
    }
    for (HWND control : visible) {
        if (control) ShowWindow(control, SW_SHOW);
    }
    RedrawWindow(g.main, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void fillSearchResults() {
    ListView_DeleteAllItems(g.searchList);
    for (int i = 0; i < static_cast<int>(g.searchResults.size()); ++i) {
        const SearchResult& model = g.searchResults[i];
        std::wstring label = model.owner + L"/" + model.repository;
        std::wstring size = formatBytes(model.estimatedSize > 0 ? model.estimatedSize : model.size);
        wchar_t stars[32];
        swprintf(stars, 32, L"%d", model.stars);

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = label.data();
        item.lParam = i;
        int inserted = ListView_InsertItem(g.searchList, &item);
        ListView_SetItemText(g.searchList, inserted, 1, const_cast<LPWSTR>(model.owner.c_str()));
        ListView_SetItemText(g.searchList, inserted, 2, const_cast<LPWSTR>(size.c_str()));
        ListView_SetItemText(g.searchList, inserted, 3, stars);
    }
}

static void fillLocalModels() {
    ListView_DeleteAllItems(g.modelList);
    for (int i = 0; i < static_cast<int>(g.localModels.size()); ++i) {
        const LocalModel& model = g.localModels[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(model.name.c_str());
        item.lParam = i;
        int inserted = ListView_InsertItem(g.modelList, &item);
        ListView_SetItemText(g.modelList, inserted, 1, const_cast<LPWSTR>(model.id.c_str()));
        ListView_SetItemText(g.modelList, inserted, 2, const_cast<LPWSTR>(model.size.c_str()));
        ListView_SetItemText(g.modelList, inserted, 3, const_cast<LPWSTR>(model.modified.c_str()));
    }
}

static void fillChatModels() {
    if (!g.chatModelCombo) return;
    wchar_t current[512]{};
    GetWindowTextW(g.chatModelCombo, current, 512);
    SendMessageW(g.chatModelCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& model : g.localModels) {
        SendMessageW(g.chatModelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(model.name.c_str()));
    }
    int index = static_cast<int>(SendMessageW(g.chatModelCombo, CB_FINDSTRINGEXACT, -1,
                                              reinterpret_cast<LPARAM>(current)));
    if (index == CB_ERR && !g.localModels.empty()) index = 0;
    if (index != CB_ERR) SendMessageW(g.chatModelCombo, CB_SETCURSEL, index, 0);
}

static void showTextWindow(const std::wstring& title, const std::wstring& text, const wchar_t* className) {
    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), className, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ErrorWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = className;
        RegisterClassW(&wc);
    }
    HWND window = CreateWindowExW(WS_EX_TOPMOST, className, title.c_str(),
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 560,
                                  g.main, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, text.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                0, 0, 100, 100, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(edit));
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    centerWindow(window, g.main);
}

static void showErrorWindow(const std::wstring& title, const std::wstring& text) {
    showTextWindow(title, text, errorWindowClassName);
}

static bool promptText(const std::wstring& title, const std::wstring& label,
                       const std::wstring& initial, std::wstring& result) {
    struct InputState {
        std::wstring value;
        bool ok = false;
        bool closed = false;
    };
    InputState state;
    state.value = initial;

    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), inputWindowClassName, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = InputWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = inputWindowClassName;
        RegisterClassW(&wc);
    }
    HWND owner = g.main;
    EnableWindow(owner, FALSE);
    HWND window = CreateWindowExW(WS_EX_TOPMOST, inputWindowClassName, title.c_str(),
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 520, 190, owner, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    HWND labelControl = CreateWindowExW(0, WC_STATICW, label.c_str(), WS_CHILD | WS_VISIBLE,
                                        20, 20, 460, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, initial.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                20, 50, 460, 28, window, reinterpret_cast<HMENU>(IDC_INPUT_EDIT),
                                GetModuleHandleW(nullptr), nullptr);
    HWND ok = CreateWindowExW(0, WC_BUTTONW, L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              284, 98, 100, 32, window, reinterpret_cast<HMENU>(IDC_INPUT_OK),
                              GetModuleHandleW(nullptr), nullptr);
    HWND cancel = CreateWindowExW(0, WC_BUTTONW, L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  394, 98, 86, 32, window, reinterpret_cast<HMENU>(IDC_INPUT_CANCEL),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(labelControl, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
    ShowWindow(window, SW_SHOW);
    centerWindow(window, owner);

    MSG msg;
    while (true) {
        BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
        if (ret <= 0 || state.closed) break;
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (state.ok) result = state.value;
    return state.ok;
}

static std::vector<SearchResult> searchModelScope(const std::wstring& query, std::wstring& error) {
    std::vector<SearchResult> results;
    std::wstring json = L"{\"PageSize\":100,\"PageNumber\":1,\"SortBy\":\"Default\",\"Target\":\"\",\"SingleCriterion\":[],\"Name\":\"" +
                        opg::jsonEscape(query) + L"\",\"Criterion\":[]}";
    std::string body = opg::wideToUtf8(json);
    opg::HttpResponse response = opg::httpRequest(L"PUT", L"https://www.modelscope.cn/api/v1/dolphin/models",
                                                  body, L"application/json");
    if (!response.ok()) {
        error = response.error.empty() ? L"魔搭搜索接口返回异常状态。" : response.error;
        return results;
    }
    JsonValue root;
    std::wstring parseError;
    if (!opg::parseJson(response.body, root, parseError)) {
        error = L"解析魔搭搜索结果失败：" + parseError;
        return results;
    }
    const JsonValue* models = root.path({L"Data", L"Model", L"Models"});
    if (!models || !models->isArray()) {
        error = L"魔搭搜索结果格式不符合预期。";
        return results;
    }
    for (const JsonValue& item : models->array) {
        std::wstring name = item.get(L"Name") ? item.get(L"Name")->asString() : L"";
        std::wstring owner = item.get(L"Path") ? item.get(L"Path")->asString() : L"";
        const JsonValue* libraries = item.get(L"Libraries");
        bool isGguf = (libraries && libraries->containsString(L"gguf")) ||
                      lower(name).find(L"gguf") != std::wstring::npos;
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
        if (fileExtensionLower(path) == L".gguf") {
            files.push_back({path, name, size});
        }
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
        error = L"解析模型文件列表失败：" + parseError;
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

static void createFileWindow(const SearchResult& model) {
    g.selectedModel = model;
    g.ggufFiles.clear();
    g.cancelDownload = false;

    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), fileWindowClassName, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FileWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = fileWindowClassName;
        RegisterClassW(&wc);
    }
    std::wstring title = L"选择 GGUF 文件 - " + model.owner + L"/" + model.repository;
    g.fileWindow = CreateWindowExW(WS_EX_TOPMOST, fileWindowClassName, title.c_str(),
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 680,
                                   g.main, nullptr, GetModuleHandleW(nullptr), nullptr);
    std::wstring labelText = L"模型：" + model.owner + L"/" + model.repository + L"（版本：" + model.revision + L"）";
    g.fileLabel = CreateWindowExW(0, WC_STATICW, labelText.c_str(), WS_CHILD | WS_VISIBLE,
                                  0, 0, 100, 26, g.fileWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.fileList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                                 0, 0, 100, 100, g.fileWindow, reinterpret_cast<HMENU>(IDC_FILE_LIST),
                                 GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(g.fileList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    addListColumn(g.fileList, 0, L"GGUF 文件", 360);
    addListColumn(g.fileList, 1, L"大小", 130, LVCFMT_RIGHT);
    addListColumn(g.fileList, 2, L"仓库路径", 400);

    g.fileNameLabel = CreateWindowExW(0, WC_STATICW, L"Ollama 模型名：", WS_CHILD | WS_VISIBLE,
                                      0, 0, 110, 24, g.fileWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.fileNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 200, 28, g.fileWindow, reinterpret_cast<HMENU>(IDC_FILE_NAME),
                                     GetModuleHandleW(nullptr), nullptr);
    g.fileOpenPage = CreateWindowExW(0, WC_BUTTONW, L"打开模型页面",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 130, 30, g.fileWindow,
                                     reinterpret_cast<HMENU>(IDC_FILE_OPEN_PAGE), GetModuleHandleW(nullptr), nullptr);
    g.fileDownload = CreateWindowExW(0, WC_BUTTONW, L"下载并导入模型",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                     0, 0, 190, 32, g.fileWindow, reinterpret_cast<HMENU>(IDC_FILE_DOWNLOAD),
                                     GetModuleHandleW(nullptr), nullptr);
    g.fileCancel = CreateWindowExW(0, WC_BUTTONW, L"关闭",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 90, 32, g.fileWindow,
                                   reinterpret_cast<HMENU>(IDC_FILE_CANCEL), GetModuleHandleW(nullptr), nullptr);
    g.fileProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH,
                                     0, 0, 100, 20, g.fileWindow, reinterpret_cast<HMENU>(IDC_FILE_PROGRESS),
                                     GetModuleHandleW(nullptr), nullptr);
    g.fileStatus = CreateWindowExW(0, WC_STATICW, L"正在读取模型文件列表...", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
                                   0, 0, 100, 24, g.fileWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    applyFonts();
    ShowWindow(g.fileWindow, SW_SHOW);
    UpdateWindow(g.fileWindow);
    centerWindow(g.fileWindow, g.main);
}

static void layoutFileWindow(int width, int height) {
    if (!g.fileWindow) return;
    int margin = 16;
    int listWidth = std::max(200, width - margin * 2);
    MoveWindow(g.fileLabel, margin, margin, listWidth, 26, TRUE);
    MoveWindow(g.fileList, margin, margin + 34, listWidth, std::max(100, height - 205), TRUE);
    int y = height - 150;
    MoveWindow(g.fileNameLabel, margin, y + 4, 110, 24, TRUE);
    MoveWindow(g.fileNameEdit, margin + 118, y, 240, 28, TRUE);
    MoveWindow(g.fileOpenPage, margin + 370, y - 1, 130, 31, TRUE);
    MoveWindow(g.fileDownload, margin, y + 42, 200, 32, TRUE);
    MoveWindow(g.fileCancel, margin + 210, y + 42, 100, 32, TRUE);
    MoveWindow(g.fileProgress, margin, y + 86, listWidth, 20, TRUE);
    MoveWindow(g.fileStatus, margin, y + 116, listWidth, 24, TRUE);
}

static void fillGgufFiles() {
    ListView_DeleteAllItems(g.fileList);
    for (int i = 0; i < static_cast<int>(g.ggufFiles.size()); ++i) {
        const GgufFile& file = g.ggufFiles[i];
        std::wstring size = formatBytes(file.size);
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(file.name.c_str());
        item.lParam = i;
        int inserted = ListView_InsertItem(g.fileList, &item);
        ListView_SetItemText(g.fileList, inserted, 1, const_cast<LPWSTR>(size.c_str()));
        ListView_SetItemText(g.fileList, inserted, 2, const_cast<LPWSTR>(file.path.c_str()));
    }
    if (!g.ggufFiles.empty()) {
        ListView_SetItemState(g.fileList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        wchar_t buffer[512];
        GetWindowTextW(g.fileNameEdit, buffer, 512);
        if (trim(buffer).empty()) {
            SetWindowTextW(g.fileNameEdit, defaultModelNameFromGguf(g.selectedModel.repository, g.ggufFiles[0].name).c_str());
        }
    }
}

static int selectedListIndex(HWND list) {
    int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    return index;
}

static bool saveGgufDialog(const std::wstring& fileName, std::wstring& path) {
    wchar_t buffer[MAX_PATH * 4]{};
    wcsncpy(buffer, fileName.c_str(), sizeof(buffer) / sizeof(wchar_t) - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.fileWindow;
    ofn.lpstrFilter = L"GGUF 模型文件\0*.gguf\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sizeof(buffer) / sizeof(wchar_t);
    ofn.lpstrTitle = L"选择 GGUF 保存位置";
    ofn.lpstrDefExt = L"gguf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

static bool chooseGgufFile(std::wstring& path) {
    wchar_t buffer[MAX_PATH * 4]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = L"GGUF 模型文件\0*.gguf\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sizeof(buffer) / sizeof(wchar_t);
    ofn.lpstrTitle = L"选择要导入的 GGUF 文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

static bool writeModelfileIfMissing(const std::wstring& ggufPath, std::wstring& modelfilePath,
                                    std::wstring& error) {
    try {
        fs::path directory = fs::path(ggufPath).parent_path();
        fs::path defaultFile = directory / L"Modelfile";
        if (fs::exists(defaultFile)) {
            modelfilePath = defaultFile.wstring();
            return true;
        }
        fs::path created = directory / (fs::path(ggufPath).stem().wstring() + L".Modelfile");
        if (!fs::exists(created)) {
            std::wofstream stream(created, std::ios::binary);
            if (!stream) {
                error = L"无法创建 Modelfile：" + created.wstring();
                return false;
            }
            stream.imbue(std::locale(""));
            stream << L"FROM \"" << ggufPath << L"\"\n";
            stream.close();
        }
        modelfilePath = created.wstring();
        return true;
    } catch (const fs::filesystem_error& e) {
        error = L"创建 Modelfile 失败：" + opg::utf8ToWide(e.what());
        return false;
    }
}

static bool writeModelfileContent(const fs::path& modelfilePath, const std::wstring& ggufPath,
                                  std::wstring& error) {
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
        fs::path target = fs::path(ggufPath);
        fs::path directory = target.parent_path();
        fs::path desired = directory / (sanitizeModelName(modelName) + L".Modelfile");
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (writeModelfileContent(desired, ggufPath, error)) {
            modelfilePath = desired.wstring();
            return true;
        }

        wchar_t temp[MAX_PATH * 2]{};
        DWORD length = GetTempPathW(MAX_PATH * 2, temp);
        if (length == 0 || length >= MAX_PATH * 2) {
            return false;
        }
        fs::path fallback = fs::path(temp) / L"OllamaPlusGUI";
        fs::create_directories(fallback, ec);
        fs::path fallbackFile = fallback / (sanitizeModelName(modelName) + L".Modelfile");
        if (writeModelfileContent(fallbackFile, ggufPath, error)) {
            modelfilePath = fallbackFile.wstring();
            return true;
        }
        return false;
    } catch (const fs::filesystem_error& e) {
        error = L"创建下载导入 Modelfile 失败：" + opg::utf8ToWide(e.what());
        return false;
    }
}
static void startSearch(const std::wstring& query) {
    if (g.searchThreadRunning) return;
    g.searchThreadRunning = true;
    EnableWindow(g.searchButton, FALSE);
    setStatus(L"正在搜索魔搭社区...");
    auto payload = std::make_unique<SearchPayload>();
    std::wstring localQuery = query;
    std::thread([localQuery, payload = payload.release()]() {
        payload->results = searchModelScope(localQuery, payload->error);
        PostMessageW(g.main, WM_APP_SEARCH_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void startModelListRefresh(bool explicitRefresh) {
    if (g.commandThreadRunning) return;
    g.commandThreadRunning = true;
    EnableWindow(g.modelRefresh, FALSE);
    EnableWindow(g.modelDelete, FALSE);
    EnableWindow(g.modelImport, FALSE);
    if (explicitRefresh) setStatus(L"正在读取 Ollama 模型列表...");
    auto payload = std::make_unique<ModelsPayload>();
    std::thread([payload = payload.release()]() {
        opg::CommandResult result = opg::runCommand(ollamaCommand() + L" list");
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
                    LocalModel model{parts[0], parts[1], parts[2], trim(modified)};
                    payload->models.push_back(model);
                }
            }
        }
        payload->error = result.error;
        PostMessageW(g.main, WM_APP_MODELS_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void startDeleteModel(const std::wstring& modelName) {
    if (g.commandThreadRunning) return;
    g.commandThreadRunning = true;
    EnableWindow(g.modelRefresh, FALSE);
    EnableWindow(g.modelDelete, FALSE);
    EnableWindow(g.modelImport, FALSE);
    setStatus(L"正在删除模型：" + modelName);
    auto payload = std::make_unique<CommandPayload>();
    std::wstring name = modelName;
    std::thread([name, payload = payload.release()]() {
        std::wstring command = ollamaCommand() + L" rm " + opg::quoteIfNeeded(name);
        opg::CommandResult result = opg::runCommand(command);
        payload->success = result.succeeded();
        payload->output = result.output;
        payload->error = result.error;
        payload->title = L"删除 Ollama 模型失败";
        payload->refreshModels = true;
        PostMessageW(g.main, WM_APP_COMMAND_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void startLocalImport(const std::wstring& ggufPath, const std::wstring& modelName) {
    if (g.commandThreadRunning) return;
    std::wstring modelfile;
    std::wstring modelfileError;
    if (!writeModelfileIfMissing(ggufPath, modelfile, modelfileError)) {
        showErrorWindow(L"创建 Modelfile 失败", modelfileError);
        return;
    }
    g.commandThreadRunning = true;
    EnableWindow(g.modelRefresh, FALSE);
    EnableWindow(g.modelDelete, FALSE);
    EnableWindow(g.modelImport, FALSE);
    setStatus(L"正在导入本地 GGUF：" + ggufPath);
    auto payload = std::make_unique<CommandPayload>();
    std::thread([ggufPath, modelName, modelfile, payload = payload.release()]() {
        std::wstring command = ollamaCommand() + L" create " + opg::quoteIfNeeded(modelName) + L" -f " +
                               opg::quoteIfNeeded(modelfile);
        opg::CommandResult result = opg::runCommand(command);
        payload->success = result.succeeded();
        payload->output = result.output;
        payload->error = result.error;
        payload->title = L"Ollama 导入 GGUF 失败";
        payload->refreshModels = true;
        PostMessageW(g.main, WM_APP_COMMAND_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void startFileList(const SearchResult& model) {
    if (g.fileThreadRunning) return;
    g.fileThreadRunning = true;
    EnableWindow(g.fileDownload, FALSE);
    SetWindowTextW(g.fileStatus, L"正在读取模型文件列表...");
    auto payload = std::make_unique<FilesPayload>();
    std::thread([model, payload = payload.release()]() {
        payload->files = fetchModelFiles(model, payload->error);
        PostMessageW(g.fileWindow, WM_APP_FILES_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void startDownload(const GgufFile& file, const std::wstring& destination, const std::wstring& modelName) {
    if (g.downloadThreadRunning) return;
    g.downloadThreadRunning = true;
    g.cancelDownload = false;
    EnableWindow(g.fileDownload, FALSE);
    EnableWindow(g.fileCancel, FALSE);
    EnableWindow(g.fileOpenPage, FALSE);
    EnableWindow(g.fileNameEdit, FALSE);
    ShowWindow(g.fileProgress, SW_SHOW);
    SendMessageW(g.fileProgress, PBM_SETPOS, 0, 0);
    SetWindowTextW(g.fileStatus, L"准备下载...");
    auto payload = std::make_unique<DownloadPayload>();
    SearchResult model = g.selectedModel;
    std::thread([model, file, destination, modelName, payload = payload.release()]() {
        std::wstring url = opg::buildDownloadUrl(model.owner, model.repository, model.revision, file.path);
        auto progressCallback = [](unsigned long long downloaded, unsigned long long total) {
            auto progress = std::make_unique<ProgressPayload>();
            progress->downloaded = downloaded;
            progress->total = total;
            PostMessageW(g.fileWindow, WM_APP_DOWNLOAD_PROGRESS, 0, reinterpret_cast<LPARAM>(progress.release()));
            return !g.cancelDownload.load();
        };
        std::wstring error;
        bool success = opg::httpDownloadFile(url, destination, progressCallback, error);
        payload->success = success;
        payload->error = error;
        payload->targetPath = destination;
        payload->modelName = modelName;
        if (success) {
            std::wstring modelfileError;
            std::wstring modelfile;
            if (writeDownloadModelfile(destination, modelName, modelfile, modelfileError)) {
                payload->modelfilePath = modelfile;
            } else {
                payload->success = false;
                payload->error = modelfileError;
            }
        }
        PostMessageW(g.fileWindow, WM_APP_DOWNLOAD_DONE, 0, reinterpret_cast<LPARAM>(payload));
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

static bool isImageExtension(const std::wstring& extension) {
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
           extension == L".gif" || extension == L".webp";
}

static bool readChatAttachment(const std::wstring& path, ChatAttachment& attachment, std::wstring& error) {
    try {
        attachment.path = path;
        attachment.name = fileNameOnly(path);
        attachment.image = isImageExtension(fileExtensionLower(path));
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) {
            error = L"读取文件大小失败：" + opg::utf8ToWide(ec.message());
            return false;
        }
        attachment.size = static_cast<long long>(size);
        if (attachment.image && size > 10ULL * 1024ULL * 1024ULL) {
            error = L"图片附件过大（最大 10 MB）：" + attachment.name;
            return false;
        }
        if (!attachment.image && size > 2ULL * 1024ULL * 1024ULL) {
            error = L"文本附件过大（最大 2 MB）：" + attachment.name;
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
        BOOL readOk = !bytes.empty() ? ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) : TRUE;
        CloseHandle(file);
        if (!readOk || (bytes.empty() ? false : read != bytes.size())) {
            error = L"读取附件内容失败：" + path;
            return false;
        }
        if (attachment.image) {
            attachment.base64 = base64Encode(bytes);
        } else {
            attachment.text = opg::utf8ToWide(bytes);
        }
        return true;
    } catch (const std::exception& e) {
        error = L"读取附件失败：" + opg::utf8ToWide(e.what());
        return false;
    }
}

static void refreshChatFileList() {
    if (!g.chatFiles) return;
    SendMessageW(g.chatFiles, LB_RESETCONTENT, 0, 0);
    for (const auto& item : g.chatAttachments) {
        SendMessageW(g.chatFiles, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>((item.name + L"  (" + formatBytes(item.size) + L")").c_str()));
    }
}

static void appendChatDisplay(const std::wstring& text) {
    if (!g.chatHistory) return;
    int length = GetWindowTextLengthW(g.chatHistory);
    std::vector<wchar_t> buffer(length + 1, L'\0');
    GetWindowTextW(g.chatHistory, buffer.data(), length + 1);
    std::wstring value = buffer.data();
    value += text;
    SetWindowTextW(g.chatHistory, value.c_str());
    int end = GetWindowTextLengthW(g.chatHistory);
    SendMessageW(g.chatHistory, EM_SETSEL, end, end);
    SendMessageW(g.chatHistory, EM_SCROLLCARET, 0, 0);
}

static void setChatBusy(bool busy) {
    EnableWindow(g.chatSend, !busy);
    EnableWindow(g.chatImport, !busy);
    EnableWindow(g.chatRemoveFile, !busy);
    EnableWindow(g.chatClear, !busy);
    EnableWindow(g.chatThink, !busy);
    SetWindowTextW(g.chatStatusLabel, busy ? L"AI 正在回复..." : L"就绪。可导入多个文本或图片文件。");
}

static std::string buildChatRequestBody(const std::wstring& model,
                                        const std::vector<ChatMessage>& history,
                                        bool thinking) {
    std::string messages;
    for (const auto& message : history) {
        if (!messages.empty()) messages += ",";
        messages += "{\"role\":\"" + opg::wideToUtf8(message.role) +
                    "\",\"content\":\"" + opg::wideToUtf8(opg::jsonEscape(message.content)) + "\"";
        if (!message.images.empty()) {
            messages += ",\"images\":[";
            for (size_t i = 0; i < message.images.size(); ++i) {
                if (i) messages += ",";
                messages += "\"" + message.images[i] + "\"";
            }
            messages += "]";
        }
        messages += "}";
    }
    return "{\"model\":\"" + opg::wideToUtf8(opg::jsonEscape(model)) +
           "\",\"messages\":[" + messages + "],\"stream\":true,\"think\":" +
           (thinking ? "true" : "false") + "}";
}

static void sendChatMessage() {
    if (g.chatThreadRunning) return;
    wchar_t model[512]{};
    GetWindowTextW(g.chatModelCombo, model, 512);
    std::wstring modelName = trim(model);
    if (modelName.empty()) {
        MessageBoxW(g.main, L"请先选择一个本地 Ollama 模型。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t input[128 * 1024]{};
    GetWindowTextW(g.chatInput, input, 128 * 1024);
    std::wstring prompt = trim(input);
    if (prompt.empty() && g.chatAttachments.empty()) {
        MessageBoxW(g.main, L"请输入聊天内容或导入文件。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (prompt.empty()) prompt = L"请分析我导入的文件。";

    std::vector<ChatMessage> history = g.chatHistoryMessages;
    ChatMessage user;
    user.sender = L"用户";
    user.role = L"user";
    user.content = prompt;
    std::wstring attachmentSummary;
    for (const auto& item : g.chatAttachments) {
        if (item.image) {
            user.images.push_back(item.base64);
            attachmentSummary += L"\n[图片附件：" + item.name + L"]";
        } else {
            user.content += L"\n\n[文本附件：" + item.name + L"]\n" + item.text;
            attachmentSummary += L"\n[文本附件：" + item.name + L"]";
        }
    }
    history.push_back(user);
    g.chatHistoryMessages.push_back(user);

    appendChatDisplay(L"用户：" + prompt + (attachmentSummary.empty() ? L"" : attachmentSummary) + L"\r\n" + modelName + L"：");
    SetWindowTextW(g.chatInput, L"");
    g.chatAttachments.clear();
    refreshChatFileList();
    setChatBusy(true);

    std::thread([modelName, history]() {
        bool thinking = SendMessageW(g.chatThink, BM_GETCHECK, 0, 0) == BST_CHECKED;
        std::string body = buildChatRequestBody(modelName, history, thinking);
        std::string lineBuffer;
        std::wstring assistant;
        bool thinkingDisplayed = false;
        auto onChunk = [&](const std::string& chunk) {
            lineBuffer += chunk;
            for (;;) {
                size_t position = lineBuffer.find('\n');
                if (position == std::wstring::npos) break;
                std::string line = lineBuffer.substr(0, position);
                lineBuffer.erase(0, position + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                JsonValue value;
                std::wstring parseError;
                if (!opg::parseJson(line, value, parseError)) continue;
                const JsonValue* thinking = value.path({L"message", L"thinking"});
                if (!thinking) thinking = value.path({L"message", L"reasoning"});
                std::wstring thought = thinking ? thinking->asString() : L"";
                if (!thought.empty()) {
                    auto payload = std::make_unique<ChatPayload>();
                    payload->model = modelName;
                    payload->chunk = thought;
                    payload->thinking = true;
                    payload->thinkingStart = !thinkingDisplayed;
                    thinkingDisplayed = true;
                    PostMessageW(g.main, WM_APP_CHAT_CHUNK, 0, reinterpret_cast<LPARAM>(payload.release()));
                }
                const JsonValue* content = value.path({L"message", L"content"});
                std::wstring piece = content ? content->asString() : L"";
                if (!piece.empty()) {
                    assistant += piece;
                    auto payload = std::make_unique<ChatPayload>();
                    payload->model = modelName;
                    payload->chunk = piece;
                    payload->afterThinking = thinkingDisplayed;
                    PostMessageW(g.main, WM_APP_CHAT_CHUNK, 0, reinterpret_cast<LPARAM>(payload.release()));
                }
            }
            return true;
        };
        opg::StreamResponse response = opg::httpStreamRequest(L"POST", L"http://127.0.0.1:11434/api/chat",
                                                              body, L"application/json", onChunk);
        auto payload = std::make_unique<ChatPayload>();
        payload->done = true;
        payload->model = modelName;
        payload->full = assistant;
        if (!response.ok()) {
            payload->error = response.error;
            if (!response.body.empty()) {
                JsonValue value;
                std::wstring parseError;
                if (opg::parseJson(response.body, value, parseError)) {
                    const JsonValue* error = value.get(L"error");
                    if (error) payload->error = error->asString(payload->error);
                }
            }
        }
        PostMessageW(g.main, WM_APP_CHAT_DONE, 0, reinterpret_cast<LPARAM>(payload.release()));
    }).detach();
}

static void clearChat() {
    if (g.chatThreadRunning) {
        MessageBoxW(g.main, L"AI 正在回复，请等待完成。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    g.chatHistoryMessages.clear();
    g.chatAttachments.clear();
    refreshChatFileList();
    SetWindowTextW(g.chatHistory, L"");
    SetWindowTextW(g.chatStatusLabel, L"已清空对话。");
}

static void layoutChatWindow(int width, int height) {
    int margin = 12;
    MoveWindow(g.chatModelLabel, margin, margin, 70, 24, TRUE);
    MoveWindow(g.chatModelCombo, margin + 74, margin - 2, std::max(260, width - 420), 300, TRUE);
    MoveWindow(g.chatThink, width - 330, margin - 2, 320, 26, TRUE);
    MoveWindow(g.chatHistory, margin, margin + 36, std::max(280, width - margin * 2),
               std::max(140, height - 274), TRUE);
    int filesY = height - 224;
    MoveWindow(g.chatFilesLabel, margin, filesY, 130, 24, TRUE);
    MoveWindow(g.chatFiles, margin, filesY + 28, 300, 68, TRUE);
    MoveWindow(g.chatImport, margin + 310, filesY + 34, 130, 30, TRUE);
    MoveWindow(g.chatRemoveFile, margin + 450, filesY + 34, 130, 30, TRUE);
    MoveWindow(g.chatClear, margin + 590, filesY + 34, 130, 30, TRUE);
    int inputY = height - 130;
    MoveWindow(g.chatInput, margin, inputY, std::max(260, width - 150), 72, TRUE);
    MoveWindow(g.chatSend, width - 124, inputY + 20, 112, 34, TRUE);
    MoveWindow(g.chatStatusLabel, margin, height - 46, std::max(280, width - margin * 2), 24, TRUE);
}

static void applyChatFonts() {
    HWND controls[] = {g.chatModelLabel, g.chatModelCombo, g.chatHistory, g.chatInput, g.chatSend,
                       g.chatStatusLabel, g.chatFilesLabel, g.chatFiles, g.chatImport,
                       g.chatRemoveFile, g.chatClear, g.chatThink};
    for (HWND control : controls) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.normalFont), TRUE);
    }
}

static void destroyChatControls() {
    HWND controls[] = {g.chatModelLabel, g.chatModelCombo, g.chatHistory, g.chatInput, g.chatSend,
                       g.chatStatusLabel, g.chatFilesLabel, g.chatFiles, g.chatImport,
                       g.chatRemoveFile, g.chatClear, g.chatThink};
    for (HWND control : controls) {
        if (control) DestroyWindow(control);
    }
    g.chatModelLabel = nullptr;
    g.chatModelCombo = nullptr;
    g.chatHistory = nullptr;
    g.chatInput = nullptr;
    g.chatSend = nullptr;
    g.chatStatusLabel = nullptr;
    g.chatFilesLabel = nullptr;
    g.chatFiles = nullptr;
    g.chatImport = nullptr;
    g.chatRemoveFile = nullptr;
    g.chatClear = nullptr;
    g.chatThink = nullptr;
    g.chatWindow = nullptr;
}

static LRESULT CALLBACK ChatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            layoutChatWindow(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 920;
            info->ptMinTrackSize.y = 680;
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_CHAT_SEND:
                    sendChatMessage();
                    return 0;
                case IDC_CHAT_IMPORT:
                    importChatFiles(hwnd);
                    return 0;
                case IDC_CHAT_REMOVE: {
                    int index = static_cast<int>(SendMessageW(g.chatFiles, LB_GETCURSEL, 0, 0));
                    if (index != LB_ERR && index < static_cast<int>(g.chatAttachments.size())) {
                        g.chatAttachments.erase(g.chatAttachments.begin() + index);
                        refreshChatFileList();
                    }
                    return 0;
                }
                case IDC_CHAT_CLEAR:
                    clearChat();
                    return 0;
                case IDC_CHAT_THINK:
                    if (SendMessageW(g.chatThink, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                        SetWindowTextW(g.chatStatusLabel, L"思考模式已启用。");
                    } else {
                        SetWindowTextW(g.chatStatusLabel, L"思考模式已关闭。");
                    }
                    return 0;
                default:
                    return 0;
            }
        case WM_CLOSE:
            if (g.chatThreadRunning) {
                if (MessageBoxW(hwnd, L"AI 正在回复。关闭窗口后本次回复将在后台继续，但聊天记录不再实时显示。是否关闭？",
                                L"确认关闭", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                    return 0;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            destroyChatControls();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static bool createChatWindow(const std::wstring& modelName) {
    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), chatWindowClassName, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ChatWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = chatWindowClassName;
        RegisterClassW(&wc);
    }

    g.chatModelLabel = CreateWindowExW(0, WC_STATICW, L"模型：", WS_CHILD | WS_VISIBLE,
                                       0, 0, 70, 24, g.chatWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.chatModelCombo = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                       0, 0, 300, 300, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_MODEL),
                                       GetModuleHandleW(nullptr), nullptr);
    g.chatThink = CreateWindowExW(0, WC_BUTTONW, L"思考模式",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  0, 0, 320, 26, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_THINK),
                                  GetModuleHandleW(nullptr), nullptr);
    g.chatHistory = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                    0, 0, 100, 100, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_HISTORY),
                                    GetModuleHandleW(nullptr), nullptr);
    g.chatInput = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                  0, 0, 100, 100, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_INPUT),
                                  GetModuleHandleW(nullptr), nullptr);
    g.chatSend = CreateWindowExW(0, WC_BUTTONW, L"发送", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 0, 0, 112, 34, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_SEND),
                                 GetModuleHandleW(nullptr), nullptr);
    g.chatStatusLabel = CreateWindowExW(0, WC_STATICW, L"就绪。可导入多个文本或图片文件。", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
                                        0, 0, 100, 24, g.chatWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.chatFilesLabel = CreateWindowExW(0, WC_STATICW, L"已导入文件：", WS_CHILD | WS_VISIBLE,
                                       0, 0, 130, 24, g.chatWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.chatFiles = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                  0, 0, 100, 100, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_FILES),
                                  GetModuleHandleW(nullptr), nullptr);
    g.chatImport = CreateWindowExW(0, WC_BUTTONW, L"导入文件", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   0, 0, 130, 30, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_IMPORT),
                                   GetModuleHandleW(nullptr), nullptr);
    g.chatRemoveFile = CreateWindowExW(0, WC_BUTTONW, L"移除选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       0, 0, 130, 30, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_REMOVE),
                                       GetModuleHandleW(nullptr), nullptr);
    g.chatClear = CreateWindowExW(0, WC_BUTTONW, L"清空对话", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 130, 30, g.chatWindow, reinterpret_cast<HMENU>(IDC_CHAT_CLEAR),
                                  GetModuleHandleW(nullptr), nullptr);
    applyChatFonts();
    refreshChatFileList();

    for (const auto& message : g.chatHistoryMessages) {
        appendChatDisplay(message.sender + L"：" + message.content + L"\r\n\r\n");
    }

    g.chatWindow = CreateWindowExW(0, chatWindowClassName, (L"AI 对话 - " + modelName).c_str(),
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760,
                                   g.main, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g.chatWindow) {
        destroyChatControls();
        return false;
    }
    g.chatCurrentModel = modelName;
    applyChatFonts();
    refreshChatFileList();
    ShowWindow(g.chatWindow, SW_SHOW);
    UpdateWindow(g.chatWindow);
    centerWindow(g.chatWindow, g.main);
    return true;
}
static void openChatForModel(const std::wstring& modelName) {
    TabCtrl_SetCurSel(g.tab, 3);
    showActivePage();
    int index = static_cast<int>(SendMessageW(g.chatModelCombo, CB_FINDSTRINGEXACT, -1,
                                              reinterpret_cast<LPARAM>(modelName.c_str())));
    if (index != CB_ERR) {
        SendMessageW(g.chatModelCombo, CB_SETCURSEL, index, 0);
    } else if (SendMessageW(g.chatModelCombo, CB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(g.chatModelCombo, CB_SETCURSEL, 0, 0);
    }
    setStatus(L"已在 AI 对话页选择模型：" + modelName);
    SetFocus(g.chatInput);
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

static void fillBlockedIps() {
    if (!g.serviceBlockList) return;
    SendMessageW(g.serviceBlockList, LB_RESETCONTENT, 0, 0);
    for (const auto& ip : g_proxy.blockedList()) {
        SendMessageW(g.serviceBlockList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ip.c_str()));
    }
}

static void restartAsAdmin(HWND hwnd) {
    wchar_t exe[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    HINSTANCE result = ShellExecuteW(hwnd, L"runas", exe, L"", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        setStatus(L"已在管理员权限下重新启动 GUI。");
    } else {
        showErrorWindow(L"以管理员身份重启失败", L"Windows 拒绝提升权限（ShellExecute 错误码 " +
                                                      std::to_wstring(reinterpret_cast<INT_PTR>(result)) + L"）。");
    }
}

static bool openFirewallRule(unsigned short port, std::wstring& error) {
    std::wstring ruleName = L"Ollama Plus GUI Proxy";
    std::wstring deleteCommand = L"netsh advfirewall firewall delete rule name=\"" + ruleName + L"\"";
    opg::CommandResult ignored = opg::runCommand(deleteCommand);
    std::wstring addCommand = L"netsh advfirewall firewall add rule name=\"" + ruleName +
                              L"\" dir=in action=allow protocol=TCP localport=" +
                              std::to_wstring(port) + L" profile=any";
    opg::CommandResult result = opg::runCommand(addCommand);
    if (!result.succeeded()) {
        error = result.error.empty() ? result.output : result.error;
        return false;
    }
    return true;
}

static void updateServiceStatus() {
    if (!g.serviceStatusLabel) return;
    if (g_proxy.running) {
        std::wstring keyState = g_proxy.requireKey ? L"启用" : L"关闭";
        SetWindowTextW(g.serviceStatusLabel, (L"服务端已开放：0.0.0.0:" + std::to_wstring(g_proxy.port) +
                                               L" -> 127.0.0.1:11434；Key 验证：" + keyState).c_str());
    } else {
        SetWindowTextW(g.serviceStatusLabel, L"服务端已关闭。外部请求不会进入本机 Ollama。");
    }
}

static void updateServiceStats() {
    static ULONGLONG lastTick = 0;
    static unsigned long long lastUp = 0;
    static unsigned long long lastDown = 0;
    ULONGLONG now = GetTickCount64();
    unsigned long long up = g_proxy.stats.bytesUp.load();
    unsigned long long down = g_proxy.stats.bytesDown.load();
    double upRate = 0.0;
    double downRate = 0.0;
    if (lastTick != 0 && now > lastTick) {
        double seconds = static_cast<double>(now - lastTick) / 1000.0;
        upRate = static_cast<double>(up > lastUp ? up - lastUp : 0) / seconds;
        downRate = static_cast<double>(down > lastDown ? down - lastDown : 0) / seconds;
    }
    lastTick = now;
    lastUp = up;
    lastDown = down;

    wchar_t stats[512];
    swprintf(stats, 512,
             L"总 token：%llu（提示 %llu / 生成 %llu）\r\n请求：%llu；活跃：%llu；拦截：%llu",
             g_proxy.stats.totalTokens.load(), g_proxy.stats.promptTokens.load(),
             g_proxy.stats.completionTokens.load(), g_proxy.stats.totalRequests.load(),
             g_proxy.stats.activeRequests.load(), g_proxy.stats.blockedRequests.load());
    wchar_t network[512];
    swprintf(network, 512,
             L"上行：%.1f KB/s（累计 %s）\r\n下行：%.1f KB/s（累计 %s）",
             upRate / 1024.0, formatBytes(up).c_str(), downRate / 1024.0, formatBytes(down).c_str());
    SetWindowTextW(g.serviceStatsLabel, stats);
    SetWindowTextW(g.serviceNetworkLabel, network);
}

static bool parseServicePort(HWND hwnd, unsigned short& port) {
    wchar_t text[64]{};
    GetWindowTextW(g.servicePortEdit, text, 64);
    unsigned long value = wcstoul(text, nullptr, 10);
    if (value < 1 || value > 65535) {
        MessageBoxW(hwnd, L"请输入 1-65535 之间的端口号。", L"提示", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    port = static_cast<unsigned short>(value);
    return true;
}

static void openService(HWND hwnd) {
    if (!isUserAdmin()) {
        MessageBoxW(hwnd, L"开放服务端需要管理员权限。请点击“以管理员重启”，或右键程序选择“以管理员身份运行”。",
                    L"需要管理员权限", MB_OK | MB_ICONWARNING);
        return;
    }
    unsigned short port = 0;
    if (!parseServicePort(hwnd, port)) return;
    std::wstring error;
    if (!opg::startProxyServer(g_proxy, port, error)) {
        showErrorWindow(L"开放服务端失败", error);
        return;
    }
    if (!openFirewallRule(port, error)) {
        opg::stopProxyServer(g_proxy);
        showErrorWindow(L"开放防火墙端口失败", error);
        updateServiceStatus();
        return;
    }
    updateServiceStatus();
    setStatus(L"服务端已开放，外部客户端可通过本机 IP 的端口 " + std::to_wstring(port) + L" 访问 Ollama。");
}

static void closeService() {
    opg::stopProxyServer(g_proxy);
    updateServiceStatus();
    setStatus(L"服务端已关闭。");
}

static void saveApiKeySettings(HWND hwnd) {
    g_proxy.requireKey = SendMessageW(g.serviceKeyRequired, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t key[512]{};
    GetWindowTextW(g.serviceApiKeyEdit, key, 512);
    std::wstring value = trim(key);
    if (g_proxy.requireKey && value.empty()) {
        MessageBoxW(hwnd, L"启用 Key 验证时，API Key 不能为空。", L"提示", MB_OK | MB_ICONINFORMATION);
        SendMessageW(g.serviceKeyRequired, BM_SETCHECK, BST_UNCHECKED, 0);
        g_proxy.requireKey = false;
        value.clear();
    }
    g_proxy.apiKey = value;
    updateServiceStatus();
    setStatus(g_proxy.requireKey ? L"API Key 验证已启用。" : L"API Key 验证已关闭。");
}

static void addBlockedIp(HWND hwnd) {
    std::wstring ip;
    if (!promptText(L"拉黑 IP", L"请输入要拒绝的 IP 地址：", L"", ip)) return;
    ip = trim(ip);
    if (ip.empty()) return;
    if (g_proxy.addBlocked(ip)) {
        fillBlockedIps();
        setStatus(L"已拉黑 IP：" + ip);
    } else {
        MessageBoxW(hwnd, L"该 IP 已在拉黑列表中。", L"提示", MB_OK | MB_ICONINFORMATION);
    }
}

static void removeBlockedIp(HWND hwnd) {
    int index = static_cast<int>(SendMessageW(g.serviceBlockList, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR) {
        MessageBoxW(hwnd, L"请先选择要解除拉黑的 IP。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t buffer[256]{};
    SendMessageW(g.serviceBlockList, LB_GETTEXT, index, reinterpret_cast<LPARAM>(buffer));
    std::wstring ip = trim(buffer);
    if (g_proxy.removeBlocked(ip)) {
        fillBlockedIps();
        setStatus(L"已解除拉黑 IP：" + ip);
    }
}

static long long recommendedCapacityBytes() {
    long long bestVram = 0;
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
                !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
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

static void startRecommendedSearch() {
    if (g.searchThreadRunning || g.recommendationLoaded) return;
    g.recommendationLoaded = true;
    g.searchThreadRunning = true;
    EnableWindow(g.searchButton, FALSE);
    long long limit = recommendedCapacityBytes();
    setStatus(L"正在按显存/内存推荐 Q4 GGUF 模型（上限 " + formatBytes(limit) + L"）...");
    auto payload = std::make_unique<SearchPayload>();
    std::thread([payload = payload.release()]() {
        payload->results = recommendedModels(payload->error);
        PostMessageW(g.main, WM_APP_SEARCH_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void startModelRename(const std::wstring& oldName, const std::wstring& newName) {
    if (g.commandThreadRunning || oldName == newName) return;
    g.commandThreadRunning = true;
    EnableWindow(g.modelRefresh, FALSE);
    EnableWindow(g.modelDelete, FALSE);
    EnableWindow(g.modelImport, FALSE);
    EnableWindow(g.modelRename, FALSE);
    EnableWindow(g.modelRun, FALSE);
    setStatus(L"正在重命名模型：" + oldName + L" -> " + newName);
    auto payload = std::make_unique<CommandPayload>();
    std::thread([oldName, newName, payload = payload.release()]() {
        opg::CommandResult copy = opg::runCommand(ollamaCommand() + L" cp " + opg::quoteIfNeeded(oldName) + L" " +
                                                   opg::quoteIfNeeded(newName));
        if (copy.succeeded()) {
            opg::CommandResult remove = opg::runCommand(ollamaCommand() + L" rm " + opg::quoteIfNeeded(oldName));
            payload->success = remove.succeeded();
            payload->output = remove.output;
            payload->error = remove.error;
            if (!remove.succeeded()) {
                payload->error += L"\n\n注意：新模型名可能已创建，但旧模型删除失败。";
            }
        } else {
            payload->success = false;
            payload->output = copy.output;
            payload->error = copy.error;
        }
        payload->title = L"重命名 Ollama 模型失败";
        payload->refreshModels = true;
        PostMessageW(g.main, WM_APP_COMMAND_DONE, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}
static void importChatFiles(HWND hwnd) {
    wchar_t buffer[64 * 1024]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"文本/图片文件\0*.txt;*.md;*.json;*.xml;*.csv;*.log;*.c;*.cpp;*.h;*.hpp;*.cs;*.py;*.js;*.ts;*.png;*.jpg;*.jpeg;*.gif;*.webp\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sizeof(buffer) / sizeof(wchar_t);
    ofn.lpstrTitle = L"导入聊天文件（可多选）";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring directory(buffer);
    std::vector<std::wstring> paths;
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

    int imported = 0;
    std::wstring errors;
    for (const auto& path : paths) {
        std::error_code ec;
        bool duplicate = false;
        for (const auto& existing : g.chatAttachments) {
            if (fs::equivalent(existing.path, path, ec)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        ChatAttachment attachment;
        std::wstring error;
        if (readChatAttachment(path, attachment, error)) {
            g.chatAttachments.push_back(std::move(attachment));
            ++imported;
        } else {
            if (!errors.empty()) errors += L"\r\n";
            errors += error;
        }
    }
    refreshChatFileList();
    if (imported) SetWindowTextW(g.chatStatusLabel, (L"已导入 " + std::to_wstring(imported) + L" 个文件。").c_str());
    if (!errors.empty()) showErrorWindow(L"导入文件失败", errors);
}

static LRESULT handleMainCommand(HWND hwnd, int id, HWND, UINT) {
    switch (id) {
        case IDC_SEARCH_BUTTON: {
            wchar_t query[512]{};
            GetWindowTextW(g.searchEdit, query, 512);
            std::wstring trimmed = trim(query);
            if (trimmed.empty()) {
                MessageBoxW(hwnd, L"请输入搜索关键词。", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            startSearch(trimmed);
            return 0;
        }
        case IDC_MODEL_REFRESH:
            startModelListRefresh(true);
            return 0;
        case IDC_MODEL_DELETE: {
            int index = selectedListIndex(g.modelList);
            if (index < 0 || index >= static_cast<int>(g.localModels.size())) {
                MessageBoxW(hwnd, L"请先选择要删除的模型。", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            LocalModel model = g.localModels[index];
            std::wstring message = L"确定删除模型：\n" + model.name + L"\n\n此操作会从 Ollama 中移除该模型，且不可恢复。";
            if (MessageBoxW(hwnd, message.c_str(), L"确认删除", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                startDeleteModel(model.name);
            }
            return 0;
        }
        case IDC_MODEL_RENAME: {
            int index = selectedListIndex(g.modelList);
            if (index < 0 || index >= static_cast<int>(g.localModels.size())) {
                MessageBoxW(hwnd, L"请先选择要重命名的模型。", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            LocalModel model = g.localModels[index];
            std::wstring newName;
            if (!promptText(L"重命名模型", L"请输入新的模型名：", model.name, newName)) return 0;
            newName = sanitizeModelName(newName);
            if (newName.empty() || newName == model.name) return 0;
            startModelRename(model.name, newName);
            return 0;
        }
        case IDC_MODEL_RUN: {
            int index = selectedListIndex(g.modelList);
            if (index < 0 || index >= static_cast<int>(g.localModels.size())) {
                MessageBoxW(hwnd, L"请先选择要运行的模型。", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            openChatForModel(g.localModels[index].name);
            return 0;
        }
        case IDC_SERVICE_OPEN:
            openService(hwnd);
            return 0;
        case IDC_SERVICE_CLOSE:
            closeService();
            return 0;
        case IDC_SERVICE_ADMIN_RESTART:
            restartAsAdmin(hwnd);
            return 0;
        case IDC_SERVICE_SAVE_KEY:
            saveApiKeySettings(hwnd);
            return 0;
        case IDC_SERVICE_ADD_BLOCK:
            addBlockedIp(hwnd);
            return 0;
        case IDC_SERVICE_REMOVE_BLOCK:
            removeBlockedIp(hwnd);
            return 0;
        case IDC_CHAT_SEND:
            sendChatMessage();
            return 0;
        case IDC_CHAT_IMPORT:
            importChatFiles(hwnd);
            return 0;
        case IDC_CHAT_REMOVE: {
            int index = static_cast<int>(SendMessageW(g.chatFiles, LB_GETCURSEL, 0, 0));
            if (index != LB_ERR && index < static_cast<int>(g.chatAttachments.size())) {
                g.chatAttachments.erase(g.chatAttachments.begin() + index);
                refreshChatFileList();
            }
            return 0;
        }
        case IDC_CHAT_CLEAR:
            clearChat();
            return 0;
        case IDC_MODEL_IMPORT: {
            std::wstring gguf;
            if (!chooseGgufFile(gguf)) return 0;
            std::wstring defaultName = sanitizeModelName(fs::path(gguf).stem().wstring());
            std::wstring modelName;
            if (!promptText(L"导入 GGUF 模型", L"请输入 Ollama 模型名：", defaultName, modelName)) return 0;
            modelName = sanitizeModelName(modelName);
            if (modelName.empty()) {
                MessageBoxW(hwnd, L"模型名不能为空。", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            startLocalImport(gguf, modelName);
            return 0;
        }
        default:
            return 0;
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            createMainControls(hwnd);
            applyFonts();
            showActivePage();
            SetTimer(g.main, 1, 1000, nullptr);
            setStatus(ollamaLocationDescription());
            startModelListRefresh(false);
            startRecommendedSearch();
            return 0;
        case WM_TIMER:
            updateServiceStats();
            return 0;
        case WM_SIZE:
            layoutMain(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 880;
            info->ptMinTrackSize.y = 580;
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->idFrom == IDC_TAB && header->code == TCN_SELCHANGE) {
                showActivePage();
                if (TabCtrl_GetCurSel(g.tab) == 0 && !g.searchThreadRunning &&
                    !g.recommendationLoaded && GetWindowTextLengthW(g.searchEdit) == 0) {
                    startRecommendedSearch();
                }
                return 0;
            }
            if (header->idFrom == IDC_MODEL_LIST && header->code == LVN_ITEMACTIVATE) {
                int index = selectedListIndex(g.modelList);
                if (index >= 0 && index < static_cast<int>(g.localModels.size())) {
                    openChatForModel(g.localModels[index].name);
                }
                return 0;
            }
            if (header->idFrom == IDC_SEARCH_LIST && header->code == LVN_ITEMACTIVATE) {
                int index = selectedListIndex(g.searchList);
                if (index >= 0 && index < static_cast<int>(g.searchResults.size())) {
                    if (!g.fileWindow) {
                        createFileWindow(g.searchResults[index]);
                        startFileList(g.searchResults[index]);
                    } else {
                        SetForegroundWindow(g.fileWindow);
                    }
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case WM_COMMAND:
            return handleMainCommand(hwnd, LOWORD(wParam), reinterpret_cast<HWND>(lParam), HIWORD(wParam));
        case WM_APP_SEARCH_DONE: {
            auto payload = std::unique_ptr<SearchPayload>(reinterpret_cast<SearchPayload*>(lParam));
            g.searchThreadRunning = false;
            EnableWindow(g.searchButton, TRUE);
            if (!payload->error.empty()) {
                setStatus(L"搜索失败");
                showErrorWindow(L"魔搭搜索失败", payload->error);
            } else {
                g.searchResults = std::move(payload->results);
                fillSearchResults();
                std::wstring text = L"找到 " + std::to_wstring(g.searchResults.size()) + L" 个 GGUF 模型。双击可选择文件。";
                setStatus(text);
            }
            return 0;
        }
        case WM_APP_MODELS_DONE: {
            auto payload = std::unique_ptr<ModelsPayload>(reinterpret_cast<ModelsPayload*>(lParam));
            g.commandThreadRunning = false;
            EnableWindow(g.modelRefresh, TRUE);
            EnableWindow(g.modelDelete, TRUE);
            EnableWindow(g.modelImport, TRUE);
            EnableWindow(g.modelRename, TRUE);
            EnableWindow(g.modelRun, TRUE);
            if (!payload->error.empty()) {
                setStatus(L"读取模型列表失败");
                showErrorWindow(L"读取 Ollama 模型列表失败", payload->error);
            } else {
                g.localModels = std::move(payload->models);
                fillLocalModels();
                fillChatModels();
                setStatus(L"已加载 " + std::to_wstring(g.localModels.size()) + L" 个本地模型。");
            }
            return 0;
        }
        case WM_APP_COMMAND_DONE: {
            auto payload = std::unique_ptr<CommandPayload>(reinterpret_cast<CommandPayload*>(lParam));
            g.commandThreadRunning = false;
            EnableWindow(g.modelRefresh, TRUE);
            EnableWindow(g.modelDelete, TRUE);
            EnableWindow(g.modelImport, TRUE);
            EnableWindow(g.modelRename, TRUE);
            EnableWindow(g.modelRun, TRUE);
            if (payload->success) {
                setStatus(payload->output.empty() ? L"操作完成。" : payload->output);
            } else {
                std::wstring text = payload->error;
                if (!payload->output.empty() && text.find(payload->output) == std::wstring::npos) {
                    text += L"\n\n--- Ollama 输出 ---\n" + payload->output;
                }
                showErrorWindow(payload->title, text);
            }
            if (payload->refreshModels) startModelListRefresh(false);
            return 0;
        }
        case WM_APP_CHAT_CHUNK: {
            auto payload = std::unique_ptr<ChatPayload>(reinterpret_cast<ChatPayload*>(lParam));
            if (payload->thinkingStart) appendChatDisplay(L"\r\n[思考]\r\n");
            if (payload->afterThinking) appendChatDisplay(L"\r\n[回复]\r\n");
            appendChatDisplay(payload->chunk);
            return 0;
        }
        case WM_APP_CHAT_DONE: {
            auto payload = std::unique_ptr<ChatPayload>(reinterpret_cast<ChatPayload*>(lParam));
            g.chatThreadRunning = false;
            if (!payload->error.empty()) {
                appendChatDisplay(L"\\r\\n[错误] " + payload->error);
                SetWindowTextW(g.chatStatusLabel, L"请求失败。");
            } else {
                if (payload->full.empty()) appendChatDisplay(L"（模型未返回内容）");
                ChatMessage assistant;
                assistant.sender = payload->model.empty() ? L"AI" : payload->model;
                assistant.role = L"assistant";
                assistant.content = payload->full.empty() ? L"（模型未返回内容）" : payload->full;
                g.chatHistoryMessages.push_back(assistant);
                appendChatDisplay(L"\\r\\n\\r\\n");
                SetWindowTextW(g.chatStatusLabel, L"回复完成。");
            }
            setChatBusy(false);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(g.main, 1);
            opg::stopProxyServer(g_proxy);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK FileWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            layoutFileWindow(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 780;
            info->ptMinTrackSize.y = 520;
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_FILE_DOWNLOAD: {
                    int index = selectedListIndex(g.fileList);
                    if (index < 0 || index >= static_cast<int>(g.ggufFiles.size())) {
                        MessageBoxW(hwnd, L"请先选择一个 GGUF 文件。", L"提示", MB_OK | MB_ICONINFORMATION);
                        return 0;
                    }
                    GgufFile file = g.ggufFiles[index];
                    wchar_t buffer[512]{};
                    GetWindowTextW(g.fileNameEdit, buffer, 512);
                    std::wstring modelName = sanitizeModelName(trim(buffer));
                    if (modelName.empty()) {
                        MessageBoxW(hwnd, L"请输入 Ollama 模型名。", L"提示", MB_OK | MB_ICONINFORMATION);
                        return 0;
                    }
                    std::wstring destination;
                    if (!saveGgufDialog(file.name, destination)) return 0;
                    startDownload(file, destination, modelName);
                    return 0;
                }
                case IDC_FILE_OPEN_PAGE: {
                    std::wstring url = L"https://www.modelscope.cn/models/" + g.selectedModel.owner + L"/" +
                                       g.selectedModel.repository;
                    ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                case IDC_FILE_CANCEL:
                    if (g.downloadThreadRunning) {
                        if (MessageBoxW(hwnd, L"正在下载，确定要取消并关闭窗口吗？", L"确认取消",
                                        MB_YESNO | MB_ICONWARNING) == IDYES) {
                            g.cancelDownload = true;
                            SetWindowTextW(g.fileStatus, L"正在取消下载...");
                        }
                        return 0;
                    }
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    return 0;
            }
        case WM_NOTIFY: {
            NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->idFrom == IDC_FILE_LIST && header->code == LVN_ITEMACTIVATE) {
                int index = selectedListIndex(g.fileList);
                if (index >= 0 && index < static_cast<int>(g.ggufFiles.size())) {
                    SetWindowTextW(g.fileNameEdit, defaultModelNameFromGguf(
                        g.selectedModel.repository, g.ggufFiles[index].name).c_str());
                }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case WM_APP_FILES_DONE: {
            auto payload = std::unique_ptr<FilesPayload>(reinterpret_cast<FilesPayload*>(lParam));
            g.fileThreadRunning = false;
            if (!payload->error.empty()) {
                SetWindowTextW(g.fileStatus, L"读取文件列表失败");
                showErrorWindow(L"读取魔搭文件列表失败", payload->error);
                EnableWindow(g.fileDownload, FALSE);
            } else {
                g.ggufFiles = std::move(payload->files);
                fillGgufFiles();
                EnableWindow(g.fileDownload, !g.ggufFiles.empty());
                SetWindowTextW(g.fileStatus, g.ggufFiles.empty() ? L"该模型没有找到 GGUF 文件。"
                                                                  : L"请选择要下载的 GGUF 文件。");
            }
            return 0;
        }
        case WM_APP_DOWNLOAD_PROGRESS: {
            auto payload = std::unique_ptr<ProgressPayload>(reinterpret_cast<ProgressPayload*>(lParam));
            if (payload->total > 0) {
                SendMessageW(g.fileProgress, PBM_SETRANGE32, 0, static_cast<LPARAM>(payload->total));
                SendMessageW(g.fileProgress, PBM_SETPOS, static_cast<WPARAM>(payload->downloaded), 0);
                SetWindowTextW(g.fileStatus, (L"正在下载：" + formatBytes(payload->downloaded) + L" / " + formatBytes(payload->total)).c_str());
            } else {
                SetWindowTextW(g.fileStatus, (L"正在下载：" + formatBytes(payload->downloaded)).c_str());
            }
            return 0;
        }
        case WM_APP_DOWNLOAD_DONE: {
            auto payload = std::unique_ptr<DownloadPayload>(reinterpret_cast<DownloadPayload*>(lParam));
            g.downloadThreadRunning = false;
            EnableWindow(g.fileDownload, TRUE);
            EnableWindow(g.fileCancel, TRUE);
            EnableWindow(g.fileOpenPage, TRUE);
            EnableWindow(g.fileNameEdit, TRUE);
            if (!payload->success) {
                SetWindowTextW(g.fileStatus, L"下载失败");
                showErrorWindow(L"下载 GGUF 失败", payload->error);
                return 0;
            }
            SetWindowTextW(g.fileStatus, L"下载完成，正在通过 Win32 后台任务导入 Ollama。");
            setStatus(L"GGUF 下载完成，正在导入 Ollama 模型：" + payload->modelName);
            startLocalImport(payload->targetPath, payload->modelName);
            return 0;
        }
        case WM_DESTROY:
            if (g.downloadThreadRunning) g.cancelDownload = true;
            g.fileWindow = nullptr;
            g.fileLabel = nullptr;
            g.fileList = nullptr;
            g.fileNameLabel = nullptr;
            g.fileNameEdit = nullptr;
            g.fileDownload = nullptr;
            g.fileCancel = nullptr;
            g.fileOpenPage = nullptr;
            g.fileProgress = nullptr;
            g.fileStatus = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK ErrorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            HWND edit = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (edit) MoveWindow(edit, 12, 12, std::max(100, LOWORD(lParam) - 24),
                                 std::max(100, HIWORD(lParam) - 24), TRUE);
            return 0;
        }
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

struct InputDialogState {
    std::wstring value;
    bool ok = false;
    bool closed = false;
};

static LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<InputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND:
            if (!state) break;
            if (LOWORD(wParam) == IDC_INPUT_OK) {
                wchar_t buffer[512]{};
                HWND edit = GetDlgItem(hwnd, IDC_INPUT_EDIT);
                GetWindowTextW(edit, buffer, 512);
                state->value = trim(buffer);
                state->ok = !state->value.empty();
                state->closed = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDC_INPUT_CANCEL) {
                state->ok = false;
                state->closed = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (state) {
                state->ok = false;
                state->closed = true;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
    createFonts();

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = mainClassName;
    RegisterClassW(&wc);

    g.main = CreateWindowExW(0, mainClassName, L"Ollama Plus GUI - 魔搭 GGUF 搜索与模型管理",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
                             nullptr, nullptr, instance, nullptr);
    if (!g.main) return 1;
    ShowWindow(g.main, showCommand);
    UpdateWindow(g.main);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g.main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g.normalFont) DeleteObject(g.normalFont);
    if (g.boldFont) DeleteObject(g.boldFont);
    return static_cast<int>(msg.wParam);
}


























































