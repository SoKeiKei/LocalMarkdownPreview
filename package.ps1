[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildScript = Join-Path $root 'build.ps1'
$encodingScript = Join-Path $root 'tools\Test-TextEncoding.ps1'
$installerScript = Join-Path $root 'installer\LocalMarkdownPreview.iss'
$iscc = 'ISCC.exe'
$dist = Join-Path $root 'dist'
$setupName = 'LocalMarkdownPreview-0.1.0-dev-x64.exe'
$setupPath = Join-Path $dist $setupName
$hashPath = Join-Path $dist 'SHA256SUMS.txt'

if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "找不到构建脚本：$buildScript"
}
if (-not (Test-Path -LiteralPath $installerScript -PathType Leaf)) {
    throw "找不到安装器脚本：$installerScript"
}
if (-not (Test-Path -LiteralPath $iscc -PathType Leaf)) {
    throw "找不到本机 Inno Setup 编译器：$iscc"
}

Write-Host "运行 Release 构建。"
& $buildScript -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Release 构建失败，退出码：$LASTEXITCODE"
}

if (Test-Path -LiteralPath $encodingScript -PathType Leaf) {
    Write-Host '检查仓库文本编码。'
    & $encodingScript
    if ($LASTEXITCODE -ne 0) {
        throw "文本编码检查失败，退出码：$LASTEXITCODE"
    }
}

$allowedSources = @(
    '..\build\Release\LocalMarkdownPreview.dll',
    '..\build\Release\PreviewHostCleanup.exe',
    '..\LICENSE',
    '..\THIRD_PARTY_NOTICES.md',
    '..\DEPENDENCIES.lock.json',
    '..\sbom.spdx.json',
    '..\README.md',
    '..\docs\architecture.md',
    '..\docs\build-and-registration.md',
    '..\docs\compatibility-matrix.md',
    '..\docs\performance-report.md',
    '..\docs\installation-test-report.md',
    '..\docs\known-issues.md',
    '..\docs\threat-model.md',
    '..\third_party\md4c\LICENSE.md',
    '..\third_party\md4c\PATCHES.md'
)
$issText = Get-Content -LiteralPath $installerScript -Encoding UTF8 -Raw
$sourceLines = [regex]::Matches($issText, '(?im)^\s*Source:\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
foreach ($source in $sourceLines) {
    if ($allowedSources -notcontains $source) {
        throw "安装器包含未批准的文件来源：$source"
    }
}
foreach ($forbidden in @('Debug', '.pdb', 'NativeSmokeTests', 'MarkdownDocumentTests', 'RegisteredPreviewTests', '\tests\')) {
    if ($issText.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "安装器脚本包含禁止的调试/测试内容：$forbidden"
    }
}
if ($sourceLines.Count -ne $allowedSources.Count) {
    throw "安装器文件清单数量异常，实际 $($sourceLines.Count)，期望 $($allowedSources.Count)。"
}
if ($issText -notmatch '(?im)ArchitecturesAllowed=x64') {
    throw '安装器未限制为 x64。'
}
if ($issText -notmatch '(?im)ArchitecturesInstallIn64BitMode=x64') {
    throw '安装器未启用 x64 注册表视图。'
}
if ($issText -notmatch '(?im)PrivilegesRequired=admin') {
    throw '安装器未声明管理员权限。'
}
if ($issText -notmatch '(?im)MinVersion=10\.0\.19045') {
    throw '安装器未声明 Windows 10 22H2 最低版本。'
}
if ($issText -notmatch '(?im)DllSurrogate.*prevhost\.exe') {
    throw '安装器未声明 Prevhost DllSurrogate。'
}
if ($issText -match '(?im)DisableLowILProcessIsolation') {
    throw '安装器不应写入 DisableLowILProcessIsolation。'
}

New-Item -ItemType Directory -Path $dist -Force | Out-Null
if (Test-Path -LiteralPath $setupPath) {
    Remove-Item -LiteralPath $setupPath -Force
}
Write-Host '编译 Inno Setup 安装器。'
& $iscc /Qp $installerScript
if ($LASTEXITCODE -ne 0) {
    throw "ISCC 编译失败，退出码：$LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) {
    throw "ISCC 未生成预期安装包：$setupPath"
}

$setup = Get-Item -LiteralPath $setupPath
$limit = 10MB
if ($setup.Length -gt $limit) {
    throw "安装包超过 10 MiB：$($setup.Length) 字节。"
}

$header = [IO.File]::ReadAllBytes($setupPath) | Select-Object -First 2
if ($header.Count -ne 2 -or $header[0] -ne 0x4D -or $header[1] -ne 0x5A) {
    throw '生成的安装包不是有效的 Windows 可执行文件。'
}
$versionInfo = $setup.VersionInfo
if ($versionInfo.ProductName.Trim() -ne 'Local Markdown Preview' -or
    $versionInfo.FileDescription.Trim() -ne 'Local Markdown Preview installer') {
    throw '安装包的产品信息不符合预期。'
}

$hash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash.ToUpperInvariant()
[IO.File]::WriteAllText($hashPath, "$hash  $setupName`r`n", [Text.UTF8Encoding]::new($false))
if ((Get-Content -LiteralPath $hashPath -Encoding UTF8 -Raw).StartsWith([char]0xFEFF)) {
    throw 'SHA256SUMS.txt 不得带 UTF-8 BOM。'
}
Write-Host "安装包已生成：$setupPath ($($setup.Length) bytes)"
Write-Host "SHA-256：$hash"
Write-Host '未执行安装或卸载，因此未验证交互页面和实际注册表写入。'
