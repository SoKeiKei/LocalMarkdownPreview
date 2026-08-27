# 架构说明

## 运行方式

Windows 资源管理器把插件加载到独立的 `prevhost.exe` 中。安装器为本项目登记专用 AppID，并使用系统规定的 `Prevhost.exe` 代理；没有写入关闭低完整性隔离的设置。

```text
Explorer
  └─ 低权限 prevhost.exe
       └─ LocalMarkdownPreview.dll
            ├─ 受限文件读取和编码识别
            ├─ MD4C Markdown 解析
            ├─ 安全本地图片验证和 WIC 解码
            └─ 只读 RichEdit 显示
```

插件实现 `IInitializeWithFile`、`IObjectWithSite`、`IPreviewHandler`、`IOleWindow` 和 `IPreviewHandlerVisuals`。`Initialize` 只保存文件路径，不读取文件；全部文件访问、解析和显示都在 `DoPreview` 中进行。`Unload` 设置取消状态、销毁窗口并释放本轮资源。

项目没有浏览器、HTML 渲染器、脚本引擎、网络客户端、后台服务、计划任务或自动更新组件。

## 为什么使用文件入口

微软建议预览处理器优先使用流入口，但也允许在确有需要时使用文件或 Shell Item。本项目必须支持本地相对图片，而 Explorer 提供的流在本机不能可靠给出 Markdown 文件的真实目录。为了验证图片仍位于文档目录内，最终采用文件入口，同时严格遵守“初始化阶段不读取”的生命周期要求。

## 文本处理

1. 以只读、共享读方式打开 Markdown 文件。
2. 拒绝目录、设备、重解析后的异常文件以及超过 5 MiB 的输入。
3. 识别 UTF-8、UTF-16 LE/BE BOM 和严格 GB18030；损坏输入显示错误提示。
4. 使用固定的 MD4C 0.5.3 解析标题、强调、删除线、列表、任务列表、引用、代码、表格、链接和图片节点。
5. 原始 HTML 回调直接丢弃。链接目标只作为文字，不创建可导航控件。
6. 解析受节点数、输出量、时间和取消标志限制；失败时在预览区显示简短信息，不弹窗。

生产限制包括 5 MiB 输入、500,000 个解析回调节点、5 MiB 可见输出和 1.2 秒解析预算。

## 本地图片

图片只接受相对于 Markdown 文件目录的本地路径。协议、绝对路径、UNC、设备名和 `..` 均直接拒绝。允许的格式为 PNG、JPEG、GIF、BMP 和系统 WIC 可解码的 WebP。

安全检查顺序：

1. 组合候选路径并只读打开文件。
2. 通过最终文件句柄取得规范路径，再次确认它仍位于文档目录内，避免符号链接和检查后替换绕过。
3. 检查扩展名、文件签名、10 MiB 编码大小、最大 4096 像素边长和像素总量。
4. 从已经验证的句柄读取到内存，然后从内存解码；不按原路径重新打开。
5. 单图解码预算 16 MiB，总图片预算 32 MiB，单文档最多 20 张图。

图片以 RichEdit OLE 静态位图插入。加载失败时恢复为安全占位文字。

## 窗口与生命周期

显示控件使用系统 `Msftedit.dll`，只读并支持滚动、选择、复制、焦点和宿主字体/颜色。窗口尺寸变化时重新排版；未使用 WebView 或额外渲染进程。

类工厂、实例和服务器锁共同维护 DLL 生命周期。只有全部释放后 `DllCanUnloadNow` 才返回可卸载。安装、升级和卸载时，专用清理工具只终止那些确实加载了本项目 DLL 的 `prevhost.exe`，不会结束 Explorer 或其他预览宿主。

## 固定标识

| 标识 | 值 |
|---|---|
| Handler CLSID | `{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}` |
| AppID | `{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}` |
| ProgID | `LocalMarkdownPreview.Handler.1` |
| 版本无关 ProgID | `LocalMarkdownPreview.Handler` |
| Preview Handler 类别 | `{8895b1c6-b41f-4c1c-a562-0d564250836f}` |

