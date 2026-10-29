# SnapLingo

截图 → OCR 文字识别 → 多模型并发翻译的 Windows 桌面工具。

## C++ / Qt 便携版（0.3.0）

新版实现位于 `native/`，使用 C++20、Qt 6 Widgets、系统 Windows OCR 和 Schannel TLS。
保留全局快捷键、桌面选区/放大镜、四种 OCR、多模型并发流式翻译、自定义提示词、思考模式开关、托盘与开机启动。
完整解压便携 ZIP 后运行 `SnapLingo.exe`，无需另装 .NET、Qt 或 C++ 运行库。
当前 x64 Release 便携包实测约 **11.9 MiB ZIP / 29.6 MiB 解压后**；旧 WPF 自包含包为 75.3 / 184.4 MiB。
Windows 本地 OCR 仍需要系统安装相应语言包；云服务需要自行配置密钥。

新版兼容 `%APPDATA%\SnapLingo\settings.json`；首次保存前备份为 `settings.json.pre-qt.bak`。
旧版 WPF 源码暂时保留用于回退，以下旧版截图仅供功能参考。不要同时运行两个版本，以免快捷键冲突。

### 构建与打包新版

开发依赖：Visual Studio 2022 C++ Build Tools（含 Windows SDK）、Qt Base 6.8.3 MSVC x64。
本机 Qt 安装目录为 `D:\Program Files\QT\6.8.3\msvc2022_64`，Build Tools 为 `D:\BuildTools`。
这两项仅开发者需要，使用者只需便携 ZIP。

```powershell
./native/build.ps1
./native/package.ps1
```

脚本支持 `-QtRoot` 和 `-BuildTools` 自定义路径。输出目录已存在时，传入不同的 `-OutputName`，避免覆盖旧包。
默认回归检查使用本地模拟 HTTP 服务，不调用云 API、不读取用户密钥。
显式设置 `SNAPLINGO_TEST_API_KEY` 可运行 `native_tests.exe liveDeepSeek`；
设置 `SNAPLINGO_DESKTOP_TEST=1` 可运行 `native_tests.exe desktopCaptureCancel` 验证桌面选区启动和取消。
便携程序的 `--smoke-test --report <path>` 会检查 TLS、本地 OCR 和显示器映射，生成不含凭据的 JSON 报告。

Qt 动态库、Windows 平台/系统 TLS 插件和正式可再分发的 MSVC CRT 随包提供。
不包含 WebEngine、QML、软件 OpenGL 或独立 OCR 模型。许可证及 Qt 对应源码地址见 `native/licenses/`。

### 验证范围

已覆盖配置迁移/快照隔离、删除编辑项、思考请求参数、SSE 分片与 UTF-8、请求取消、HTTP 错误、云 OCR 响应解析、选区坐标。
三家云 OCR 的真实鉴权仍需要各自的有效账户验证；本机只有一个显示器，混合 DPI 多屏布局需要相应设备验收。
已实测 DeepSeek 关闭思考的流式翻译、Windows OCR、桌面选区启动/Esc 取消，以及解压后在仅保留系统 PATH 环境下启动（Schannel TLS 和显示器映射正常）。

## 功能特点

- **全局快捷键截图** — 默认 `Ctrl+Shift+A`，可自定义
- **多种 OCR 引擎** — Windows 本地 OCR、火山引擎、百度 OCR、腾讯云 OCR
- **多模型并发翻译** — 支持任意 OpenAI 兼容 API，同时对比多个模型结果
- **自定义翻译提示词** — 灵活配置翻译策略
- **截图放大镜** — 精确选取截图边缘
- **开机自启动** — 可选，默认关闭
- **系统托盘常驻** — 不占用任务栏空间

## 功能展示

### 截图选区

使用全局快捷键触发截图，带放大镜辅助精确选取文字区域：

![截图选区](docs/06_截图选区示意.png)

### 翻译结果

OCR 识别完成后，多模型并发翻译，对比不同结果：

![翻译结果](docs/05_截图翻译结果.png)

### 设置 - 模型配置

支持添加多个 OpenAI 兼容 API 模型：

![模型设置](docs/01_模型设置.png)

### 设置 - OCR 服务

支持多种 OCR 引擎，可添加多个服务并切换：

![OCR设置](docs/02_OCR设置.png)

### 设置 - 快捷键与启动

自定义截图快捷键，可选开机自启动：

![快捷键设置](docs/03_快捷键与开机启动.png)

### 关于

![关于](docs/04_关于页面.png)

## 安装（旧版 WPF）

下载 [最新 Release](https://github.com/zouchanglin/SnapLingo/releases) 中的 `SnapLingo-vX.X.X-win-x64.zip`，解压后运行 `SnapLingo.exe` 即可。

## 使用说明

1. 首次运行后，右键系统托盘图标进入"设置"
2. 在"模型配置"中添加 OpenAI 兼容 API（如通义千问、DeepSeek、OpenAI 等）
3. 在"OCR"中配置 OCR 服务（Windows 本地 OCR 开箱即用，无需配置）
4. 按下快捷键（默认 `Ctrl+Shift+A`）开始截图翻译

## 系统要求（旧版 WPF）

- Windows 10 1903+ / Windows 11
- [.NET 8 Desktop 运行时](https://dotnet.microsoft.com/download/dotnet/8.0)

## 构建（旧版 WPF）

开发只需安装 .NET 8 SDK（运行时不能用于编译），不要求安装 Visual Studio。

```bash
dotnet build SnapLingo.csproj -c Release
dotnet run --project SnapLingo.csproj
dotnet publish -c Release -r win-x64 --no-self-contained
```

在 Windows 上运行回归检查：

```bash
dotnet run --project tests/SnapLingo.Regression/SnapLingo.Regression.csproj -c Release
```

检查覆盖配置删除与编辑、配置快照隔离、OCR 不可用、截图裁剪及流式请求取消。
多显示器实机验收时，应分别检查副屏位于左侧/上方、不同缩放比例以及跨屏选区。

## 开源协议

[MIT License](LICENSE)
