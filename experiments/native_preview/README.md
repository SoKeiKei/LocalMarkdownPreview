# Native Markdown Preview Handler 样机

这是一个只用于阶段 1 验证的原生 x64 Windows Preview Handler 样机。它实现微软 Preview Handler 要求的 COM 生命周期，接收 `IInitializeWithStream` 提供的只读流，在宿主提供的子窗口中显示纯文本内容，并处理窗口缩放、焦点、快捷键转发和卸载。

样机不会注册 COM，也不会写入注册表；因此资源管理器不会自动加载它。它只验证 DLL 可被 COM 工厂创建、接口调用顺序和窗口生命周期。Markdown 目前按原文显示，不包含解析器、图片、链接或 HTML 渲染。

## 构建

在 PowerShell 中，从任意目录运行：

```powershell
& '.\experiments\native_preview\build.ps1'
```

或在命令提示符中运行：

```bat
.\experiments\native_preview\build.cmd
```

默认构建 `Release|x64`，产物位于 `build\Release\native_preview.dll`。脚本固定使用稳定版 Visual Studio 2022、`v143` 工具集和 Windows SDK，不下载依赖。也可以用 Visual Studio 2022 打开 `native_preview.sln`，选择 `Release` 与 `x64` 后构建。

## 设计边界

- 仅实现进程内 COM DLL；微软的 Preview Host 负责在独立、低完整性进程中承载它。
- 使用 `IInitializeWithStream`，初始化阶段只保存流，不读取文件；真正读取发生在 `DoPreview`。
- 文本上限为 5 MiB；支持 UTF-8（含 BOM）、UTF-16 LE/BE（含 BOM），无 BOM 内容先按严格 UTF-8 读取，失败后按系统 ANSI 代码页读取。
- 使用系统 `msftedit.dll` 创建 `ES_READONLY` 的 Rich Edit 子窗口，不执行 HTML、脚本、外部命令或远程资源。
- 没有 `DllRegisterServer` / `DllUnregisterServer`，没有安装器和注册脚本；本目录内的工程不会改变系统注册表。
- `SetWindow` 与 `SetRect` 会检查参数和 Win32 返回值，`Unload` 会销毁子窗口并释放流。

## 当前可验证结果

执行构建脚本后，脚本会检查 DLL 存在并打印文件大小。用 `dumpbin /exports build\Release\native_preview.dll` 可看到两个 COM 标准导出：`DllCanUnloadNow` 与 `DllGetClassObject`。

## 尚未验证

- 尚未写入注册表，所以尚未在资源管理器真实 Preview Pane 中加载；本样机不提供自动注册路径。
- 尚未在 Windows 10、ARM64 或不同 DPI/主题下验证。
- 尚未验证宿主实际调用顺序、低完整性策略、快速切换取消和跨进程快捷键转发。
- 只读文本窗口不是最终 Markdown 渲染器，尚未覆盖 Markdown 语法、图片边界、主题和性能预算。

## 参考与许可证

接口和生命周期参考微软 Windows Classic Samples 的 `RecipePreviewHandler`，具体链接和许可说明见 `THIRD_PARTY_NOTICES.md`。本样机代码不是该样例的复制品；保留该说明是为了明确参考来源和许可证义务。
