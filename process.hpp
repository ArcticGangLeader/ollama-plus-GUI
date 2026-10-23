#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace opg {

struct CommandResult {
    bool started = false;
    DWORD exitCode = 0;
    std::wstring output;
    std::wstring error;

    bool succeeded() const { return started && error.empty() && exitCode == 0; }
};

inline std::wstring normalizeOutput(const std::string& bytes) {
    if (bytes.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                   static_cast<int>(bytes.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (size <= 0) {
        codePage = CP_ACP;
        size = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (size <= 0) return L"";
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), result.data(), size);
    return result;
}

inline CommandResult runCommand(const std::wstring& command, DWORD timeoutMilliseconds = INFINITE) {
    CommandResult result;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        wchar_t buffer[128];
        swprintf(buffer, 128, L"创建输出管道失败（Windows 错误 %lu）", static_cast<unsigned long>(GetLastError()));
        result.error = buffer;
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = command;
    std::vector<wchar_t> commandBuffer(mutableCommand.begin(), mutableCommand.end());
    commandBuffer.push_back(L'\0');
    BOOL created = CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) {
            result.error = L"未找到 ollama 命令。请先安装 Ollama，并确认它在 PATH 中。";
        } else {
            wchar_t buffer[128];
            swprintf(buffer, 128, L"启动命令失败（Windows 错误 %lu）", static_cast<unsigned long>(code));
            result.error = buffer;
        }
        return result;
    }
    result.started = true;

    std::string output;
    char buffer[8192];
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) {
            if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0) {
                DWORD remaining = 0;
                if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &remaining, nullptr) && remaining == 0) break;
            }
            continue;
        }
        DWORD toRead = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, toRead, &read, nullptr) || read == 0) break;
        output.append(buffer, buffer + read);
    }
    if (WaitForSingleObject(process.hProcess, timeoutMilliseconds) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &result.exitCode);
    } else {
        TerminateProcess(process.hProcess, WAIT_TIMEOUT);
        result.exitCode = WAIT_TIMEOUT;
        result.error = L"命令执行超时。";
    }

    DWORD remaining = 0;
    while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &remaining, nullptr) && remaining > 0) {
        DWORD toRead = remaining < sizeof(buffer) ? remaining : static_cast<DWORD>(sizeof(buffer));
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, toRead, &read, nullptr) || read == 0) break;
        output.append(buffer, buffer + read);
    }

    CloseHandle(readPipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.output = normalizeOutput(output);
    if (result.error.empty() && result.exitCode != 0) {
        result.error = result.output.empty() ? L"Ollama 返回了非零退出码。" : result.output;
    }
    return result;
}

inline std::wstring quoteIfNeeded(const std::wstring& value) {
    if (!value.empty() && value.find(L' ') == std::wstring::npos &&
        value.find(L'"') == std::wstring::npos) {
        return value;
    }
    std::wstring result = L"\"";
    for (wchar_t c : value) {
        if (c == L'"') result.push_back(L'\\');
        result.push_back(c);
    }
    result.push_back(L'"');
    return result;
}

} // namespace opg

