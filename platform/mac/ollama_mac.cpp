#include "core/ollama.hpp"
#include "core/process.hpp"

#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <cstdlib>

namespace opg {
namespace {

bool isExecutableFile(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return access(path.c_str(), X_OK) == 0;
}

std::string homeDirectory() {
    const char* home = std::getenv("HOME");
    if (home && *home) return home;
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return pw->pw_dir;
    return "";
}

} // namespace

std::string findOllamaExe() {
    // 1. 优先检查官方 App 安装
    const char* appPaths[] = {
        "/Applications/Ollama.app/Contents/Resources/ollama",
        "/Applications/Ollama.app/Contents/MacOS/ollama",
    };
    for (const char* p : appPaths) {
        if (isExecutableFile(p)) return p;
    }

    // 2. 检查 Homebrew / 系统路径
    const char* systemPaths[] = {
        "/opt/homebrew/bin/ollama",   // Apple Silicon
        "/usr/local/bin/ollama",      // Intel
        "/usr/bin/ollama",
    };
    for (const char* p : systemPaths) {
        if (isExecutableFile(p)) return p;
    }

    // 3. 检查用户目录
    std::string home = homeDirectory();
    if (!home.empty()) {
        std::string userPaths[] = {
            home + "/.ollama/bin/ollama",
            home + "/bin/ollama",
        };
        for (const auto& p : userPaths) {
            if (isExecutableFile(p)) return p;
        }
    }

    // 4. 用 `which ollama` 走 PATH
    //    注意：runCommand 会 fork，但只调一次，不频繁，可接受
    auto r = opg::runCommand("command -v ollama 2>/dev/null", 5);
    if (r.started && r.exitCode == 0) {
        std::string path = r.output;
        // 去掉尾部换行
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
        if (!path.empty() && isExecutableFile(path)) return path;
    }

    return "";
}

std::string ollamaCommand() {
    std::string path = findOllamaExe();
    if (path.empty()) return "";
    return quoteIfNeeded(path);
}

std::string ollamaLocationDescription() {
    std::string path = findOllamaExe();
    if (!path.empty()) return "已检测到 Ollama：" + path;
    return "未检测到 Ollama。请先安装：https://ollama.com/download";
}

} // namespace opg