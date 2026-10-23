#pragma once

#include <cstdint>
#include <string>

namespace opg {

// 返回推荐给 GGUF 模型使用的容量上限（字节），
// 考虑 GPU 显存（若有）或统一内存，预留系统开销。
int64_t recommendedCapacityBytes();

// 返回探测到的硬件描述，用于 UI 显示：
//   例："GPU: Intel Iris Plus 1536 MB" 或 "内存：16.00 GiB"
std::string describeCapacity();

} // namespace opg