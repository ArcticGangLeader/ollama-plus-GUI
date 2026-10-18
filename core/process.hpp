#pragma once

#include <cstdint>
#include <string>

namespace opg {

struct CommandResult {
    bool started = false;
    int exitCode = 0;
    std::string output;   // UTF-8（stdout + stderr 合并）
    std::string error;    // UTF-8
    bool succeeded() const { return started && error.empty() && exitCode == 0; }
};

// 同步执行 shell 命令，收集 stdout+stderr
CommandResult runCommand(const std::string& command, int timeoutSeconds = 0);

// 给路径加引号（如果含空格或特殊字符）
std::string quoteIfNeeded(const std::string& value);

} // namespace opg