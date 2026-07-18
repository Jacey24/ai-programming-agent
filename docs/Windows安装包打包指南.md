# Windows 安装包打包指南

本文档汇总了 CodePilot Windows 安装包从零搭建到修复所有运行时问题的完整过程，供团队成员重现构建流程。

---

## 1. 快速开始

### 前置条件

| 工具 | 版本 | 说明 |
|------|------|------|
| Visual Studio 2022 | 17.x | 需安装「使用 C++ 的桌面开发」工作负载 |
| CMake | ≥ 3.20 | Windows 环境变量中可用 |
| Node.js | ≥ 18 | npm 可用 |
| Inno Setup 6 | ≥ 6.2 | 安装到默认路径 `C:\Program Files (x86)\Inno Setup 6\` |

### 一键打包

```powershell
powershell -ExecutionPolicy Bypass -File "scripts\package.ps1"
```

可选参数：
| 参数 | 说明 |
|------|------|
| `-Version "0.2.0"` | 手动指定版本号，不指定则自动从 git tag / CMakeLists.txt 提取 |
| `-OutputDir "dist"` | 安装包输出目录（默认 `dist`） |
| `-SkipBuild` | 跳过编译步骤，仅组装已有产物并打包 |

产物：`dist/CodePilot-Setup-<version>.exe`

---

## 2. 打包流程（`scripts/package.ps1`）

```
[1/5] 构建前端        npm run build (frontend/new-web)
[2/5] CMake 配置       cmake -S . -B build -G "Visual Studio 17 2022" -A x64
[3/5] CMake 编译       cmake --build build --config Release
[4/5] 组装 staging    收集 .exe/.dll → 复制 config/ web/ → 创建占位目录
[5/5] 运行 ISCC       ISCC.exe /DMyAppVersion=... setup.iss
```

### 版本号自动生成策略
1. 尝试 `git describe --tags` 获取 tag 名
2. 若失败（仓库无 tag），回退读取 `CMakeLists.txt` 中的 `VERSION` 字段
3. 追加时间戳 `-dev.yyyyMMdd-HHmm`（本地原型构建用）

### API 密钥安全
`config/llm.local.json` 包含用户的实际 API 密钥，**永远不会被打包进安装包**：
- `.gitignore` 第 77 行排除了 `*.local.json`
- `package.ps1` 第 173 行 `-Exclude "llm.local.json"` 在复制 config 时显式排除

---

## 3. 安装包结构

安装路径：`C:\Users\<用户名>\AppData\Local\CodePilot\`

```
CodePilot\
├── codepilot-agent-server.exe           后端
├── codepilot-shell.exe                  壳启动器 (WebView2)
├── *.dll                                运行时 DLL（OpenSSL 等）
├── config\                              公共配置
│   ├── agent.json                       必需
│   ├── llm.json                         LLM provider 定义（不含密钥）
│   ├── experts.json
│   ├── tools.json
│   ├── logging.json
│   ├── workspace.json
│   └── agent_roles.json
├── web\                                 前端静态文件
│   ├── index.html
│   └── assets/
├── storage\                             预创建（含 .gitkeep）
├── workspace\                           预创建（含 .gitkeep）
└── logs\                                预创建（含 .gitkeep）
```

### 设计决策：安装在 `%LOCALAPPDATA%` 而非 `Program Files`

| 位置 | 写入权限 | 是否需要 Admin |
|------|----------|-----------------|
| `C:\Program Files (x86)\` | 只读（普通用户） | 需要 |
| `%LOCALAPPDATA%\` | 完全读写 | 不需要 |

CodePilot 后端需要在运行时写入多个目录：
- `logs/codepilot.log`（spdlog 日志）
- `storage/agent.db`（SQLite 数据库，含 WAL/journal 文件）
- `workspace/`（用户项目文件）

如果安装在 `Program Files`，这些写入操作会触发 Windows 文件系统保护 → 进程无声崩溃。安装在 `%LOCALAPPDATA%` 完全避免此问题，与 VS Code / Chrome / Teams 等应用一致。

### Start Menu 入口
| 快捷方式 | 说明 |
|----------|------|
| `CodePilot` | 启动壳应用（WebView2 窗口） |
| `CodePilot (Open in Browser)` | 在默认浏览器中打开 `http://localhost:8080` |

### 权限
`PrivilegesRequired=none` — 安装到用户目录，**无需管理员权限**。

### 运行时检测
安装程序会自动检测：
- **VC++ Redistributable**（通过注册表键 `VC\Runtimes\X64`）
- **WebView2 Runtime**（通过注册表键 `EdgeUpdate\Clients\{...}`）

缺失时给出警告和下载链接，用户可选择继续。

---

## 4. 问题定位与修复记录

### 问题 1：安装后缺少 DLL（`libssl-3-x64.dll`、`libcrypto-3-x64.dll`）

**现象：** 安装并运行后，后端无法启动，系统弹窗报缺失 OpenSSL DLL。

**根因：** `setup.iss` 的 `[Files]` 段只声明了 `.exe` 文件。CMake 生成的 Release 目录中存在多个 `.dll` 依赖，但未被打包。

**修复：** 在 `setup.iss` 中添加通配 DLL 条目：
```iss
Source: "{#StagingDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
```

同时 `package.ps1` 第 151-156 行已自动收集 `build/*/Release/*.dll`。

---

### 问题 2：config 目录完全缺失，后端瞬间崩溃

**现象：** 安装后运行，后端 "not started in time"，进程立即退出。

**根因：** `installer/setup.iss` 的 `[Files]` 段没有 `config/` 目录项。`AppBootstrap.cpp` 启动第一行加载 `config/agent.json` → 文件不存在 → 抛异常 → 进程退出。

**修复：** 在 `setup.iss` 中添加：
```iss
Source: "{#StagingDir}\config\*"; DestDir: "{app}\config"; Flags: ignoreversion recursesubdirs createallsubdirs
```

安全保证：`package.ps1` 在复制 config 时显式排除 `llm.local.json`。

---

### 问题 3：logs 目录写入权限导致后端崩溃

**现象：** 修复问题 2 后，后端仍然无法启动。

**根因：** `AppBootstrap.cpp` 的 `ensure_runtime_directories()` 创建 `./storage`、`./workspace`、`./logs` 三个目录。安装包预创建了前两个，但 `logs/` 未预创建。后端在 `Program Files` 内创建目录 → `ACCESS_DENIED` → 退出。

**修复：**
- `setup.iss` 添加 `logs/` 预创建条目（`.gitkeep` 占位）
- `package.ps1` staging 组装时创建 `logs/` 目录

---

### 问题 4：WebView2 用户数据目录写入权限

**现象：** 壳应用启动后 WebView2 初始化失败或空白窗口。

**根因：** `ShellWindow.cpp` 原代码使用 `L"./webview2_data"`，在 `Program Files` 下 WebView2 运行时无写入权限。

**修复：** 改为使用 `%LOCALAPPDATA%\CodePilot\WebView2\`（通过 `SHGetFolderPathW` + `CSIDL_LOCAL_APPDATA`）。

---

### 🔴 核心问题 5：安装在 Program Files 导致无声崩溃

**现象：**

| 测试场景 | 结果 | 推测 |
|----------|------|------|
| 安装器 `[Run]` 勾选启动 | ✅ 正常 | 继承 admin 权限 |
| 卸载重装，不勾选，首次手动启动 | ✅ 正常 | 无 `agent.db`，启动路径短 |
| 关闭后第二次手动启动 | ❌ 崩溃 | 有 `agent.db`，`recoverInterruptedTasks()` 异常？ |
| 删除 `agent.db` 后手动启动 | ❌ 仍然崩溃 | 数据库不是根因 |
| 复制构建目录 server.exe 到安装目录 | ❌ 崩溃 | exe 文件完全一致 |
| **复制整个安装文件夹到桌面** | ✅ **正常** | **唯一变量是目录位置** |
| 手动指定 D 盘安装 | ✅ 正常 | D 盘无 ACL 保护 |

**根因：** `C:\Program Files (x86)\` 受 Windows 文件系统保护，普通用户无法在其中创建/写入文件。后端 spdlog 初始化时尝试创建 `logs/codepilot.log` → `open()` 失败 → spdlog 内部抛异常 → 进程无声崩溃。

**诊断对比：** 引入 stderr 日志重定向后（`main.cpp` → `logs/error.log`），只能看到一行 `[STARTUP]`，后续 `[FATAL]` 消息全部丢失。这是因为 spdlog 的初始化在 before-main 或 atexit 阶段进行——stderr 已指向日志文件，但日志文件本身尚未创建成功，形成死锁。

**修复（治本）：**
`setup.iss` 两行改动：
```iss
; 安装路径从 Program Files 改为用户 AppData
DefaultDirName={localappdata}\{#MyAppName}   ; 原: {autopf}\{#MyAppName}

; 无需管理员权限
PrivilegesRequired=none                       ; 原: PrivilegesRequired=admin
```

安装目标变为 `C:\Users\<用户名>\AppData\Local\CodePilot\`，用户完全可读写。

### 辅助修复：后端 stderr 日志

`main.cpp` 入口处添加了 `_open`/`_dup2` 级别的 stdout/stderr 重定向（含 `sync_with_stdio`），崩溃时可在 `logs/error.log` 看到完整错误信息。

### 辅助修复：SQLite 数据库自动恢复

`AppBootstrap.cpp` 中 `recoverInterruptedTasks()` 改为非致命——第一次失败删除旧数据库并重试，只有重置后仍失败才真正退出。

---

## 5. LLM API Key 配置指南

### 配置方式（优先级从高到低）

CodePilot 支持三种 API Key 配置方式，按优先级排列：

#### 方式 1：环境变量（推荐用于个人开发者）

在 `config/llm.json` 中每个 provider 有 `api_key_env` 字段：

```json
{
  "default": "deepseek",
  "providers": {
    "deepseek": {
      "name": "DeepSeek",
      "base_url": "https://api.deepseek.com",
      "model": "deepseek-chat",
      "api_key_env": "DEEPSEEK_API_KEY",
      "timeout_seconds": 120
    }
  }
}
```

**设置环境变量：**

- **Windows：** `setx DEEPSEEK_API_KEY "sk-xxxxxxxxxxxxxxxx"`（需重新打开终端生效）
- **PowerShell：** `[Environment]::SetEnvironmentVariable("DEEPSEEK_API_KEY", "sk-xxx", "User")`
- **Linux/macOS：** `export DEEPSEEK_API_KEY="sk-xxx"`（放入 `~/.bashrc` 或 `~/.zshrc`）

**各 Provider 对应的环境变量名：**

| Provider | 环境变量 | API 获取地址 |
|----------|----------|-------------|
| DeepSeek | `DEEPSEEK_API_KEY` | https://platform.deepseek.com/api_keys |
| OpenAI | `OPENAI_API_KEY` | https://platform.openai.com/api-keys |
| 豆包 (Doubao) | `LLM_API_KEY` | https://console.volcengine.com/ark |
| 通义千问 (Qwen) | `DASHSCOPE_API_KEY` | https://dashscope.console.aliyun.com |
| 智谱 GLM | `ZHIPU_API_KEY` | https://open.bigmodel.cn |
| Moonshot (Kimi) | `MOONSHOT_API_KEY` | https://platform.moonshot.cn |

#### 方式 2：本地配置文件 `config/llm.local.json`

在安装目录的 `config/` 下创建 `llm.local.json`（**此文件不会被安装包覆盖，不会被 git 追踪**）：

**格式 A（传统，仅设置默认 provider）：**
```json
{
  "api_key": "sk-xxxxxxxxxxxxxxxx"
}
```

**格式 B（推荐，指定特定 provider）：**
```json
{
  "providers": {
    "deepseek": {
      "api_key": "sk-xxxxxxxxxxxxxxxx"
    }
  }
}
```

`LlmClientFacade::loadLocalOverrides()` 会读取此文件并覆盖对应 provider 的 key。

#### 方式 3：安装包自带示例文件

`config/llm.local.example.json` 随安装包分发，内容为占位符：
```json
{
  "api_key": "replace-with-your-local-api-key"
}
```

用户可以复制此文件为 `config/llm.local.json` 并填入实际 key。

### 读取流程（代码级）

```
LlmClientFacade::init("config/llm.json")
  ├── loadProvidersFromConfig()    解析 llm.json → ProviderConfig 列表
  ├── loadLocalOverrides()         读取 llm.local.json → 覆盖 apiKey
  └── buildClients()               为每个 provider 创建 OpenAICompatibleClient
        └── isConfigured()         检查 apiKeyEnv 或 apiKey 是否就绪
              ├── std::getenv(apiKeyEnv)   ← 方式 1
              └── apiKey 非空              ← 方式 2/3
```

### 有效 Key 的判定

`OpenAICompatibleClient::isConfigured()` 的逻辑：
1. 如果 `api_key_env` 非空 → 读取环境变量，非空即有效
2. 否则检查 `api_key` 字段是否非空
3. 两者都为空 → 降级为 MockLlmClient（不调用真实 LLM）

### 日志诊断

启动后查看 `logs/codepilot.log`，可以看到：
```
LlmClientFacade available: true
LlmClientFacade default provider: deepseek
```

如果输出 `available: false` 或 `No LLM provider or API key is configured`，说明 Key 未正确设置。

---

## 6. `logs/error.log` 诊断指南

后端启动失败时，查看 `logs/error.log` 可获取精确错误信息。常见错误及处理：

| 错误信息 | 含义 | 处理 |
|----------|------|------|
| `[FATAL][Configuration] required configuration file is not readable: 'config/agent.json'` | 工作目录不正确 | 确保从安装目录启动 |
| `[FATAL][SQLite] Unable to initialize database` | 数据库损坏且恢复失败 | 手动删除 `storage/agent.db` |
| `[WARN][SQLite] Database init/recovery failed ... attempting reset` | 自动恢复中 | 正常，会重试一次后自动解决 |
| `[FATAL][Logging] Unable to initialize logging` | logging.json 不可读 | 检查文件完整性 |

---

## 7. 版本历史

| 日期 | 版本 | 变更 |
|------|------|------|
| 2026-07-18 | 0.1.0-dev | 初始打包脚本、Inno Setup 配置 |
| 2026-07-18 | 0.1.0-dev | 5 个运行时问题修复（DLL、config、logs、WebView2、Program Files ACL） |
| 2026-07-18 | 0.1.0-dev | API Key 配置文档、error.log 诊断指南 |