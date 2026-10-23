#pragma once

#include <string>

namespace opg {

// 查找 ollama 可执行文件的绝对路径；找不到返回空字符串
std::string findOllamaExe();

// 返回用于 shell 命令的 ollama 调用前缀：
//   找到 → 带引号的绝对路径
//   未找到 → 空字符串（调用方应提示用户安装）
std::string ollamaCommand();

// 人可读的位置说明
std::string ollamaLocationDescription();

} // namespace opg