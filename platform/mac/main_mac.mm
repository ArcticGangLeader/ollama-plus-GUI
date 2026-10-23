#import <Cocoa/Cocoa.h>

#include "core/ollama.hpp"
#include "core/capacity.hpp"

namespace appinfo {
std::string ollamaPath;
std::string ollamaDescription;
std::string capacityDescription;

void initialize() {
    ollamaPath = opg::findOllamaExe();
    ollamaDescription = opg::ollamaLocationDescription();
    capacityDescription = opg::describeCapacity();
}
} // namespace appinfo

// 声明在 ui_mac.mm 中
int runGUI(int argc, const char* argv[]);

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        appinfo::initialize();
    }
    return runGUI(argc, argv);
}