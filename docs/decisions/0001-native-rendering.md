# 决策 0001：采用纯原生 Preview Handler 与非网页渲染

- 状态：已确认
- 日期：2026-08-27

## 背景

首版必须在 Windows 资源管理器预览窗格内显示 Markdown，同时满足以下硬约束：

- 不启动独立 Renderer、浏览器或后台程序。
- 不使用 HTML 执行环境，不联网，不执行脚本。
- 保持 Windows 默认低完整性 Preview Host。
- 100 KB 普通文档新增私有工作集不得超过 80 MiB，目标不超过 50 MiB。
- 安装包目标不超过 10 MiB，安装目录目标不超过 25 MiB。

## 已验证方案

### WPF + Markdig 语法树

样机不使用 HTML 或 WebView，Markdig 只生成语法树，再直接映射为 WPF `FlowDocument`。Release 构建和 3 项安全/语法自动测试均通过，运行文件约 0.66 MiB。

但最简单示例的独立进程实测：

- 工作集：81.29 MiB。
- 私有内存：83.60 MiB。
- 句柄数：527。

它在尚未加载本地图片、Preview Host 集成、主题和取消逻辑前就已超过 80 MiB 硬上限，因此排除。

### 纯原生 C++ + RichEdit 外壳

原生样机最初实现 `IInitializeWithStream`、`IObjectWithSite`、`IOleWindow` 和 `IPreviewHandler`，使用系统 RichEdit 显示只读文本。

VS2022 `v143` Release x64 实测：

- DLL 大小：128,512 字节。
- 直接依赖：`USER32.dll`、`KERNEL32.dll`、`GDI32.dll`。
- COM 工厂、实例计数和可卸载状态验证通过。
- 不下载依赖，不启动外部进程，不修改注册表。

该路线有足够的体积与内存余量继续加入 Markdown 解析、格式和受限图片。

## 决策

正式版本采用：

1. 纯原生 C++20、Win32 和 COM。
2. x64 DLL 由 Windows `prevhost.exe` 按需承载。
3. 最终使用 `IInitializeWithFile`，但初始化阶段只保存路径，读取延后到 `DoPreview`；不关闭低完整性隔离。
4. 使用系统 RichEdit 提供选择、复制、滚动、键盘和自动换行。
5. Markdown 解析器只输出块、行内和文字节点，不生成 HTML。
6. 格式内容由本项目生成安全的 RichEdit 文档数据；链接默认只显示，不在控件内导航。
7. 用文件入口取得文档根目录；图片必须通过最终文件句柄确认仍位于该目录内，随后从同一句柄读入内存解码，否则显示占位。

解析器采用固定的 MD4C 0.5.3：单个 C 源文件、MIT、无运行依赖，支持 CommonMark、表格、任务列表和删除线，并通过回调直接消费节点。固定版本、许可证、两处 MSVC 兼容补丁和恶意输入测试均已完成记录。

## 后果

优点：

- 运行依赖和攻击面更小。
- 不存在 HTML、JavaScript、CSS 或网页导航链路。
- 有望满足安装体积与内存目标。
- RichEdit 已提供成熟的选择、复制、滚动和输入法/Unicode 支持。

代价：

- Markdown 节点到 RichEdit 格式的映射需要自行维护。
- 表格和图片必须使用 RichEdit 支持的安全格式，不能复用网页布局。
- 主题、DPI、取消、图片预算和宽表格需要专门测试。
- 使用文件入口意味着插件自行只读打开 Markdown；因此实现必须持续保持初始化不读取、5 MiB 上限、普通文件检查和明确错误处理。

## 后续实测补充

真实 Explorer 证明文件入口与专用 `Prevhost.exe` AppID 可以同时保持低完整性，宿主 RID 为 `0x1000`。中文路径、带空格的本地相对图片已经在 Explorer 中显示。最终 Release DLL 为 252,928 字节，真实宿主私有占用约 3.2 MiB，路线选择达到体积和内存目标。

## 参考

- Microsoft Building Preview Handlers：<https://learn.microsoft.com/en-us/windows/win32/shell/building-preview-handlers>
- Microsoft Preview Handlers and Shell Preview Host：<https://learn.microsoft.com/en-us/windows/win32/shell/preview-handlers>
- MD4C：<https://github.com/mity/md4c>
