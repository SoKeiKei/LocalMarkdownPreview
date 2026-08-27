# LocalMarkdownPreview

LocalMarkdownPreview 是一个轻量、离线的 Windows 资源管理器 Markdown 预览插件。安装后，在资源管理器中选中 `.md` 或 `.markdown` 文件并打开预览窗格，即可查看格式化内容。

## 已实现

- 标题、粗体、斜体、删除线、列表、任务列表、引用、代码块、表格和链接文字。
- UTF-8、带 BOM 的 UTF-16 LE/BE 和 GB18030 文本。
- 中文路径、带空格的文件名和安全的本地相对图片。
- 只读、可滚动、可选择复制；正文随预览窗格宽度自动换行，只有宽图片等确实溢出时才显示横向滚动。
- 自动跟随 Windows 应用明暗主题；资源管理器未提供颜色时也会主动匹配，并在主题切换后刷新当前预览。
- 原生纵向和横向滚动条与预览背景同步采用亮色、暗色或高对比度外观。
- 标题、正文、链接、引用、代码和表头使用同一套高对比调色板，并带有适度留白、缩进和块级底色。
- 5 MiB 文件上限，以及解析、输出、图片数量、图片尺寸和内存预算。
- 原始 HTML、脚本、危险链接、远程图片、绝对路径、UNC 路径和父目录越界均不会执行或读取。
- 独立的低权限预览宿主，不联网、不遥测、没有后台服务或自动更新。
- x64 安装包、冲突确认、原值备份、条件恢复和专用卸载清理。

## 本机验证结果

当前 Windows 11 x64 开发机已经完成 Release/Debug 构建、13,580 项文档断言、42 项图片边界断言、原生窗口测试、实际安装、覆盖升级、资源管理器预览、低权限宿主确认、500 文件连续切换和卸载恢复测试。

本机实测的 Release DLL 约 252 KiB；安装包约 1.9 MiB；安装目录约 4 MiB。100 KiB 和 1 MiB 文档的解析用时分别约 2–5 毫秒和 20–37 毫秒。加入自动主题刷新后，实际资源管理器宿主私有占用约 2.7 MiB，工作占用约 20 MiB；连续切换 500 个文件没有出现私有占用增长。

未覆盖项目：Windows 10 实机、ARM64、干净虚拟机、代码签名和连续两小时长测。详见 [兼容性矩阵](docs/compatibility-matrix.md) 和 [已知限制](docs/known-issues.md)。

## 构建

需要 Windows x64、Visual Studio 2022 C++ 桌面工具、`v143` 工具集和 PowerShell。

```powershell
& .\build.ps1 -Configuration Release
& .\build.ps1 -Configuration Debug
```

构建脚本会生成 DLL 和测试程序，并自动运行文档、图片和窗口测试。正式安装包使用：

```powershell
& .\package.ps1
```

输出位于 `dist`，同时生成 SHA-256 校验文件。安装需要一次管理员确认，因为 Windows 要求机器级登记独立预览宿主；日常预览仍在系统的低权限宿主中运行。

## 开发验证

```powershell
& .\tests\registry\Run-RegistryTests.ps1
& .\tools\Test-TextEncoding.ps1
```

开发登记脚本默认只演练，不会写入真实注册表。正式使用应运行 `dist` 中的安装包，不建议手工登记 DLL。

## 设计边界

- 使用文件初始化入口，但 `Initialize` 只保存路径，所有读取都延后到 `DoPreview`。选择这一入口是为了在不关闭低权限隔离的前提下可靠验证本地相对图片根目录。
- Markdown 不转换成 HTML，也没有浏览器或脚本引擎。链接仅显示，不可点击；代码块只作为文字。
- 图片先通过最终句柄确认仍位于文档目录内，再读取到内存并解码；读取后不会按原路径重新打开。
- 安装器不改变默认打开程序或图标。发现已有预览处理器时必须由用户选择替换、保留或取消。

## 文档

- [架构说明](docs/architecture.md)
- [构建、安装与登记](docs/build-and-registration.md)
- [威胁模型](docs/threat-model.md)
- [兼容性矩阵](docs/compatibility-matrix.md)
- [性能报告](docs/performance-report.md)
- [安装测试报告](docs/installation-test-report.md)
- [已知限制](docs/known-issues.md)
- [第三方通知](THIRD_PARTY_NOTICES.md)
- [固定依赖](DEPENDENCIES.lock.json)
- [SPDX 软件物料清单](sbom.spdx.json)

## 固定标识

- Handler CLSID：`{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}`
- AppID：`{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}`
- 安装器 AppId：`{C846D84E-793E-4FD5-9B65-FBF6AED37799}`
- ProgID：`LocalMarkdownPreview.Handler.1`

本项目采用 MIT 许可证。MD4C 0.5.3 也采用 MIT 许可证，固定版本和两处 MSVC 兼容补丁已在第三方文档中记录。
