#pragma once

#include <cstdio>
#include <cstdint>
#include <string>

namespace opg {

inline std::string formatBytes(int64_t value) {
    char buffer[64];
    if (value < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%lld B", static_cast<long long>(value));
    } else {
        double size = static_cast<double>(value);
        const char* units[] = {"KiB", "MiB", "GiB", "TiB"};
        int unit = -1;
        while (size >= 1024.0 && unit < 3) {
            size /= 1024.0;
            ++unit;
        }
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit < 0 ? 0 : unit]);
    }
    return buffer;
}

} // namespace opg