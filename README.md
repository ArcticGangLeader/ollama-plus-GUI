# Ollama Plus GUI

独立的 Windows C++ / DirectX 11 / Dear ImGui GUI，当前版本 `0.1.0`。  
用于搜索魔搭社区 GGUF 模型、下载并导入 Ollama、管理本地模型、流式对话、开放服务端，以及配置 Ollama 参数。项目不修改 `ollama-main` 源码。

## 界面

- 使用 DirectX 11 + Dear ImGui 自绘 UI。
- 不使用 Win32 原生按钮、输入框、列表框等控件。
- 自定义无边框标题栏和 Win10 风格窗口控制按钮。
- 标题栏、侧边栏和主区域采用一体化背景。
- 侧边栏整合工作区导航与主题切换，不显示重复的“工作区”标题。
- 提供页面切换淡入、侧边导航过渡、主题芯片过渡和标题栏按钮悬停动画。
- 内置黑、红（竞技风）、白三套主题。

## 页面

### 魔搭搜索

- 按关键词搜索魔搭社区模型。
- 自动过滤并显示 GGUF 模型。
- 支持按显存 / 内存推荐 Q4 量化模型。
- 双击模型或点击“选择文件”进入 GGUF 文件二级弹窗。
- 选择目标文件后可下载并自动导入 Ollama。
- 下载期间显示全局进度窗口，包括百分比、已下载大小、总大小和状态。
- 支持取消下载；取消后会删除不完整的临时文件。

### 本地模型

- 显示本地模型名称、ID、大小和修改时间。
- 支持刷新模型列表。
- 支持导入本地 GGUF；如果目录中没有 `Modelfile`，会自动创建。
- 支持重命名模型：
  1. `ollama cp 旧模型 新模型`
  2. `ollama rm 旧模型`
- 支持删除模型。
- 双击模型或点击“运行模型”会弹出 AI 对话窗口。

### AI 对话

- 从“运行模型”或双击本地模型打开。
- 使用 Ollama `/api/chat` 流式接口。
- 回复标题显示实际模型名。
- 支持思考模式开关，实时显示 `message.thinking`。
- 支持导入多个文本和图片附件。
- 支持多轮上下文。
- 使用 ImGui 多视口，聊天窗口可以拖出主窗口独立渲染。

### 服务端

- 本机反向代理：`0.0.0.0:外部端口 -> 127.0.0.1:11434`。
- 支持开放 / 关闭服务端。
- 支持添加、删除、查看 Windows 防火墙规则。
- 支持拉黑 / 解除拉黑 IP。
- 支持启用 API Key 验证。
- 显示总 token、请求数、活跃连接、拦截数和网络上下行累计。

### 设置

- 切换黑 / 红 / 白主题。
- 查看软件版本、构建时间、Ollama CLI / API 信息。
- 设置系统提示词、temperature、top_p、top_k、repeat_penalty、num_ctx、seed。
- 设置流式输出、思考模式、JSON 输出、keep_alive。
- 可执行 Ollama 命令参数，例如 `list`、`ps`、`show 模型名`。

## 图标

- `app_icon.png` 是应用图标源图。
- `make_icon.ps1` 会生成多尺寸 `app_icon.ico`。
- `resources.rc` 将图标和 PNG 资源嵌入 exe。
- 主窗口任务栏 / 标题栏使用 exe 图标。
- ImGui 创建的附属窗口也会加载同一图标。

## 构建

确保已安装 MinGW-w64。若项目中存在：

```text
tools\mingw64\bin\g++.exe
```

则会优先使用便携版编译器。

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

按以下顺序检测：

1. GUI 同目录：`ollama.exe`
2. GUI 同目录：`bin\ollama.exe`
3. GUI 同目录：`ollama\ollama.exe`
4. 当前工作目录的上述位置
5. 系统 `PATH` 中的 `ollama`

## 注意

- “服务端”需要管理员权限。
- 服务端只是本机代理，不替换 Ollama 服务；请先确保 Ollama 正在运行。
- 删除模型操作不可恢复。
- 图片附件最大 10 MB；文本附件最大 2 MB。
- 下载 GGUF 前请确认目标磁盘空间足够。
