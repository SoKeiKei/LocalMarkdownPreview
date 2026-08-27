# 安全输入

以下内容必须作为文字忽略或显示为不可用占位，不能执行、联网或读取越界文件。

<script>alert('script')</script>

<img src="x" onerror="alert('event')">

<iframe src="https://example.com"></iframe>

[脚本链接](javascript:alert('link'))

[数据链接](data:text/html;base64,PHNjcmlwdD4=)

[文件链接](file:///C:/Windows/win.ini)

![远程图片](https://example.com/remote.png)

![父目录图片](../outside.png)

![绝对路径图片](C:/Windows/Web/Wallpaper/Windows/img0.jpg)

![UNC 图片](\\server\share\image.png)

![数据图片](data:image/svg+xml,<svg onload="alert(1)"></svg>)

```powershell
Remove-Item -Recurse C:\Important
```

代码块必须只显示文本。
