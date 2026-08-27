# 第三方声明

本项目当前只随仓库保留一项第三方源码：MD4C。Windows SDK、MSVC、RichEdit 和 OLE32 等属于目标 Windows/Visual Studio 环境提供的系统或开发工具组件，不作为本仓库随附的第三方源码列入本清单。

## MD4C

- 项目：MD4C（`mity/md4c`）
- 版本：0.5.3（`release-0.5.3`）
- 固定提交：`472c417005c2c71b8617de4f7b8d6b30411d78f4`
- 许可证：MIT
- 上游地址：<https://github.com/mity/md4c>
- 随附源码：`third_party/md4c/md4c.c`、`third_party/md4c/md4c.h`
- 许可证原文：[`third_party/md4c/LICENSE.md`](third_party/md4c/LICENSE.md)
- 本地补丁说明：[`third_party/md4c/PATCHES.md`](third_party/md4c/PATCHES.md)

当前仓库对 MD4C 只保留 `md4c.c` 的两处本地补丁，具体以 `PATCHES.md` 为准：

1. 将链接属性结构清零初始化，避免 MSVC 对潜在未初始化读取的警告，并让异常路径保持确定状态。
2. 将强调标记字符串改为 `_T("*_~")`，使 `MD4C_USE_UTF16` 构建使用宽字符串，修复 MSVC 的窄/宽字符指针不匹配。

除上述两处补丁外，`md4c.c` 和 `md4c.h` 保持固定上游提交内容。MD4C 已静态编译进生产 Handler 的解析路径，也用于 `experiments/md4c_parser` 独立样机；两处均使用这个固定提交和本地补丁。

## 许可证义务

重新分发包含 MD4C 源码或其派生二进制的版本时，必须保留 MD4C 的 MIT 许可证声明。完整原文位于 `third_party/md4c/LICENSE.md`；本项目自身许可证位于根目录 `LICENSE`。
