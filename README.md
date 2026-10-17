# Ollama Plus GUI

独立 Windows C++/Win32 GUI，用于搜索魔搭社区 GGUF 模型、下载并导入 Ollama、管理本地模型，以及开放 Ollama 反向代理服务端。项目不修改 `ollama-main` 源码，只调用外部 `ollama` 命令和本机 Ollama API。

## 功能

### 魔搭搜索
- 支持关键词搜索，自动只显示 GGUF 模型。
- 双击模型进入二级窗口，递归读取 `.gguf` 文件并选择量化版本。
- 下载完成后自动创建/复用 `Modelfile`，并弹出 CMD 执行 `ollama create`。
- 未输入关键词时，会按显卡显存或物理内存推荐 Q4 GGUF 模型：
  - 有独立显卡：可用上限 = 最大独立显卡显存 - 约 1.75 GiB；
  - 没有独立显卡：可用上限 = 物理内存 - 2 GiB。

### 本地模型
- `ollama list` 查看模型、ID、大小和修改时间。
- 运行模型：打开 CMD 并执行 `ollama run 模型名称`。
- 重命名模型：先 `ollama cp 旧名 新名`，成功后 `ollama rm 旧名`。
- 删除模型、导入本地 GGUF。

### 服务端
- 反向代理监听 `0.0.0.0:外部端口`，转发到 `127.0.0.1:11434`。
- “开放服务端”会自动添加 Windows 防火墙规则；需要管理员权限。
- 支持拉黑/解除拉黑 IP。
- 支持启用 API Key 验证，兼容：
  - `Authorization: Bearer <key>`
  - `x-api-key: <key>`
  - `api-key: <key>`
- 显示总 token、提示/生成 token、请求数、活跃连接、拦截次数、网络上下行累计值和实时速率。
- 外部端口默认 `11435`。

## 构建

```bat
build.bat
```

输出：

```text
dist\OllamaPlusGUI.exe
```

也可以使用 CMake：

```bat
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Ollama 程序检测

GUI 启动和执行 `ollama list / run / cp / rm / create` 时会按以下顺序检测：

1. GUI 同目录：`ollama.exe`
2. GUI 同目录：`bin\ollama.exe`
3. GUI 同目录：`ollama\ollama.exe`
4. 当前工作目录的上述位置
5. 系统 `PATH` 中的 `ollama`

如果检测到本地 `ollama.exe`，GUI 会直接使用该文件；否则继续使用系统 PATH。

## 注意

- 使用“服务端”页面的“开放服务端”时必须以管理员身份运行。
- 服务端只是本机代理，不替换 Ollama 服务；请先确保 Ollama 正在运行。
- 删除模型操作不可恢复。

