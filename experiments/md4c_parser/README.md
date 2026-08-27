# MD4C 原生 Markdown 解析样机

这是阶段 1 的离线解析验证样机。它直接把 Windows `WCHAR`（UTF-16）文本交给 MD4C 的事件回调，收集块、行内节点和文字节点；不生成 HTML，不打开链接或图片，也不访问网络。

## 上游与许可证

- 上游：`mity/md4c`
- 版本：`release-0.5.3`
- 固定提交：`472c417005c2c71b8617de4f7b8d6b30411d78f4`
- 许可证：MIT，原文见 [`third_party/md4c/LICENSE.md`](../../third_party/md4c/LICENSE.md)
- 引入文件：`third_party/md4c/md4c.c`、`third_party/md4c/md4c.h` 和上述许可证；没有引入 `md4c-html`、`entity.c` 或其他渲染器文件。

MD4C 通过 `MD4C_USE_UTF16` 编译定义使用 Windows `WCHAR`。样机只启用表格、任务列表和删除线三个扩展。原始 HTML 保持为解析器回调中的可丢弃节点：块级 HTML 和 `MD_TEXT_HTML` 文本只计数，不进入可见文字结果。

## 构建与自动测试

在 PowerShell 中执行：

```powershell
& '.\experiments\md4c_parser\build.ps1' -Configuration Release -RunTests
```

该命令使用 Visual Studio 2022、`v143` 工具集、Windows SDK `10.0.26100.0` 和 `Release|x64`，不会下载依赖、注册 COM 或写入注册表。也可以在 Visual Studio 2022 中打开 `md4c_parser_spike.sln`，选择 `Release` 与 `x64` 后构建。

产物：`build\Release\md4c_parser_spike.exe`。最近一次本机 `Release|x64` 构建（2026-08-27）大小为 **333,824 字节**；构建脚本仍会在每次运行时打印实际字节数。

运行样机事件输出：

```powershell
& '.\experiments\md4c_parser\build\Release\md4c_parser_spike.exe' --sample
```

自动测试覆盖：标题、粗体、斜体、删除线、无序列表、已完成和待办任务、引用、行内及围栏代码、表格、链接、图片、中文 UTF-16，以及原始 HTML 的识别和丢弃。链接目标和图片路径只被收集为属性，不会导航、读取或加载。

最近一次验证结果：`PASS: MD4C UTF-16 parser tests (92 events, 39 dropped HTML code units)`。
