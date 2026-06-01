# 提案：集成离线 OCR 识别

## 概述

为 SnapLingo 添加 OCR 文字识别能力，使截图后能自动提取图片中的文字内容，为后续 LLM 翻译提供输入。

## 动机

当前截图功能已完成，但截图只是图片，无法直接翻译。需要 OCR 将图片中的文字提取为可编辑文本，作为整个"截图→识别→翻译→复制"流程的第二环。

## 方案选择

**选定方案：Windows.Media.Ocr（系统内置）**

| 考量因素 | 决策 |
|---------|------|
| 引擎 | Windows.Media.Ocr |
| 额外 NuGet 依赖 | 无 |
| 离线可用 | 是 |
| 识别语言 | 默认 zh-Hans（对英文兼容良好） |
| 部署体积增量 | 0 |

排除的方案：
- Tesseract：需打包 30MB+ 语言数据，中文效果一般
- 云端 API（百度/Azure）：需要网络，违背离线需求

## 技术变更

### 1. 升级 TargetFramework

```xml
<TargetFramework>net8.0-windows10.0.19041.0</TargetFramework>
```

指定 Windows SDK 版本以获取 WinRT API（Windows.Media.Ocr）访问权限。

### 2. 图片格式转换

截图产出的 `BitmapSource`（WPF）需转换为 `SoftwareBitmap`（WinRT），作为 OcrEngine 的输入。

### 3. OCR 识别服务

创建 `OcrService` 类，封装：
- 语言可用性检测
- BitmapSource → SoftwareBitmap 转换
- 调用 OcrEngine 执行识别
- 返回识别文本

### 4. 语言策略

- 默认使用 `zh-Hans` 引擎（对中英文混排兼容性好）
- 启动时检测 `OcrEngine.AvailableRecognizerLanguages`
- 若缺少中文语言包，回退到 `en-US` 并提示用户安装

### 5. UI 变更

- 截图完成后自动触发 OCR
- 主窗口新增文本区域显示识别结果
- 识别结果支持复制

## 数据流

```
BitmapSource ──▶ SoftwareBitmap ──▶ OcrEngine(zh-Hans)
                                          │
                                          ▼
                                     OcrResult
                                      • Lines[]
                                      • Words[]
                                          │
                                          ▼
                                   拼接为纯文本 ──▶ 显示在 UI
```

## 前置条件

- Windows 10 1903+ (Build 19041+)
- 系统已安装中文语言包（设置 → 时间和语言 → 语言）

## 范围

- **包含**：OCR 识别、结果展示、复制功能、语言包缺失提示
- **不包含**：翻译功能（第三步）、多语言手动切换 UI（后续优化）
