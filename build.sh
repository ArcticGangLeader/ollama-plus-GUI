#!/bin/bash
set -e
cd "$(dirname "$0")"

CXX=${CXX:-clang++}
CXXFLAGS="-std=c++14 -Wall -Wextra -Wpedantic -O2 -g"
CXXFLAGS_ARC="$CXXFLAGS -fobjc-arc"

# 1. 编译
$CXX $CXXFLAGS -c -I. core/search.cpp               -o build_core_search.o
$CXX $CXXFLAGS -c -I. platform/mac/process_mac.cpp  -o build_process.o
$CXX $CXXFLAGS -c -I. platform/mac/ollama_mac.cpp   -o build_ollama.o
$CXX $CXXFLAGS -c -I. platform/mac/socket_mac.cpp   -o build_socket.o
$CXX $CXXFLAGS -c -I. platform/mac/proxy_mac.cpp    -o build_proxy.o

$CXX $CXXFLAGS_ARC -c -I. platform/mac/http_mac.mm        -o build_http.o
$CXX $CXXFLAGS_ARC -c -I. platform/mac/capacity_mac.mm    -o build_capacity.o
$CXX $CXXFLAGS_ARC -c -I. platform/mac/main_mac.mm        -o build_main.o
$CXX $CXXFLAGS_ARC -c -I. platform/mac/ui_mac.mm          -o build_ui.o
$CXX $CXXFLAGS_ARC -c -I. platform/mac/file_window.mm     -o build_file_window.o

# 2. 链接成裸二进制
$CXX $CXXFLAGS_ARC build_main.o build_ui.o build_file_window.o build_core_search.o \
    build_http.o build_process.o build_ollama.o \
    build_socket.o build_proxy.o build_capacity.o \
    -o OllamaPlusGUI_bin \
    -framework Cocoa -framework Foundation -framework Metal

# 3. 打包成 .app
APP_NAME="OllamaPlusGUI.app"
rm -rf "$APP_NAME"
mkdir -p "$APP_NAME/Contents/MacOS"
mkdir -p "$APP_NAME/Contents/Resources"

mv OllamaPlusGUI_bin "$APP_NAME/Contents/MacOS/OllamaPlusGUI"
cp Info.plist "$APP_NAME/Contents/Info.plist"

# 清理临时对象
rm -f build_*.o

echo "=================================================="
echo "构建完成！"
echo "你可以双击打开应用：$APP_NAME"
echo "或者在终端运行：open $APP_NAME"
echo "=================================================="