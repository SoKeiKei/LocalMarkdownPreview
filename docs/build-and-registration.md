# 构建、安装与登记

## 构建环境

- Windows x64。
- Visual Studio 2022，安装 C++ 桌面开发工具。
- MSVC `v143` 和可用的 Windows SDK。
- PowerShell 5.1 或更高版本。

项目不需要 NuGet、Node.js、.NET、WebView2 或联网下载依赖。固定的 MD4C 源码已包含在 `third_party/md4c`。

## 构建和测试

```powershell
& .\build.ps1 -Configuration Release
& .\build.ps1 -Configuration Debug
```

脚本固定选择 Visual Studio 2022，构建 x64 DLL、清理工具和测试程序，然后运行：

- Markdown 格式、编码、预算和 2,000 组确定性模糊输入测试。
- 本地图片路径、签名、尺寸、预算和损坏文件测试。
- COM 工厂、文件读取时机、只读窗口、图片插入、外观、焦点和卸载生命周期测试。
- 亮色、暗色、高对比调色板、文字对比度、标题留白、代码块内缩、主题刷新消息和 320×120 图片对象尺寸测试。

本机最终结果：13,580 项文档断言通过；42 项图片断言通过，另有 1 项符号链接用例因当前账户没有创建链接权限而跳过；Release 和 Debug 窗口测试均通过。

## 生成安装包

```powershell
& .\package.ps1
```

脚本会重新执行 Release 构建和编码检查，核对安装文件白名单、x64/管理员/最低系统要求、低权限代理设置、文件头、产品信息和 10 MiB 体积上限，最后生成：

```text
dist\LocalMarkdownPreview-0.1.0-dev-x64.exe
dist\SHA256SUMS.txt
```

安装包未签名。下载或传输后应先核对 SHA-256。

## 安装

1. 关闭正在查看 Markdown 的预览窗格不是必须的；安装器会清理只加载本项目 DLL 的专用宿主。
2. 运行 x64 安装包并通过 Windows 的管理员确认。
3. 如果检测到 `.md` 或 `.markdown` 已有其他预览处理器，选择“替换”“保留现有处理器”或“取消”。安装器不会静默覆盖。
4. 安装后在资源管理器中打开预览窗格，选中 Markdown 文件。

安装器不会改变扩展名的默认打开程序或图标，不会添加服务、计划任务、启动项、网络访问或自动更新。

## 升级与卸载

相同 AppId 的新安装包可直接覆盖升级。升级前后都会调用专用宿主清理工具，避免旧 DLL 被锁定。

卸载时，仅当当前预览映射仍属于本项目时，才恢复安装前备份的原值；如果用户后来选择了其他处理器，则保留用户的新选择。卸载程序随后移除本项目文件和空目录。

## 开发登记脚本

`tools/Register-DevelopmentPreview.ps1` 和 `tools/Unregister-DevelopmentPreview.ps1` 仅供临时开发验证。默认只演练，实际写入必须显式使用 `-Apply`；冲突替换还必须显式使用 `-AllowReplacement`。正式使用应安装发行包。

登记逻辑回归测试：

```powershell
& .\tests\registry\Run-RegistryTests.ps1
```

该测试使用随机临时注册根，不触碰真实扩展映射，覆盖演练、冲突取消、显式替换、UTF-8 无 BOM 备份、条件恢复和低完整性保护。

## 依赖与导出核对

本机最终 DLL 只依赖 Windows 系统组件：`USER32.dll`、`ole32.dll`、`SHLWAPI.dll`、`COMCTL32.dll`、`KERNEL32.dll`、`GDI32.dll` 和 `SHELL32.dll`。对外导出仅为 `DllCanUnloadNow` 与 `DllGetClassObject`。
