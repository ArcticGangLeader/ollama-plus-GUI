#include "core/capacity.hpp"
#include "core/format.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <sys/sysctl.h>
#include <cstdio>
#include <string>

namespace opg {
namespace {

int64_t physicalMemoryBytes() {
    int64_t bytes = 0;
    size_t size = sizeof(bytes);
    // 用 sysctl 更底层可靠；失败时回退到 NSProcessInfo
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) != 0) {
        bytes = (int64_t)[[NSProcessInfo processInfo] physicalMemory];
    }
    return bytes;
}

struct GpuInfo {
    bool present = false;
    int64_t dedicatedBytes = 0;   // 显存（独立 GPU）
    int64_t recommendedBytes = 0; // Metal 推荐工作集
    std::string name;
};

GpuInfo probeGpu() {
    GpuInfo info;
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return info;

        info.present = true;
        info.name = device.name ? device.name.UTF8String : "Unknown GPU";

        // recommendedMaxWorkingSetSize 是 Metal 2 的属性，
        // macOS 10.14 可用，但部分老 GPU 可能返回 0
        if ([device respondsToSelector:@selector(recommendedMaxWorkingSetSize)]) {
            info.recommendedBytes = (int64_t)device.recommendedMaxWorkingSetSize;
        }
    }
    return info;
}

} // namespace

int64_t recommendedCapacityBytes() {
    const int64_t gib = 1024LL * 1024LL * 1024LL;
    int64_t totalMemory = physicalMemoryBytes();
    GpuInfo gpu = probeGpu();

    // 情况 1：有独立 GPU（用显存）
    // Intel 集显的 recommendedMaxWorkingSetSize 通常小于 2GB，
    // 而真正瓶颈是物理内存，所以低于 4GB 的工作集视为"集显/共享"
    if (gpu.present && gpu.recommendedBytes >= gib * 4) {
        // 独显：显存 - 1.75GB 预留
        int64_t reserve = gib * 175LL / 100LL;
        if (gpu.recommendedBytes > reserve) return gpu.recommendedBytes - reserve;
    }

    // 情况 2：集显 / 无 GPU 信息 → 用物理内存
    if (totalMemory > gib * 3) {
        // 集显场景：模型占内存大头，取 60%
        // （考虑到 8GB 机器要给系统留 3GB 左右）
        int64_t cap = static_cast<int64_t>(totalMemory * 0.6);
        if (cap < gib) cap = gib;
        return cap;
    }

    // 情况 3：极小内存 → 默认 6GB
    return gib * 6;
}

std::string describeCapacity() {
    int64_t totalMemory = physicalMemoryBytes();
    GpuInfo gpu = probeGpu();

    std::string desc;
    if (gpu.present) {
        desc += "GPU: " + gpu.name;
        if (gpu.recommendedBytes > 0) {
            desc += " (工作集 " + formatBytes(gpu.recommendedBytes) + ")";
        }
        desc += "\n";
    } else {
        desc += "GPU: 未检测到 Metal 设备\n";
    }
    desc += "内存: " + formatBytes(totalMemory) + "\n";
    desc += "推荐上限: " + formatBytes(recommendedCapacityBytes());
    return desc;
}

} // namespace opg