[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$solutionPath = Join-Path $projectRoot 'LocalMarkdownPreview.sln'

$vswhereCandidates = @()
foreach ($programFilesRoot in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
    if (-not [string]::IsNullOrWhiteSpace($programFilesRoot)) {
        $candidate = Join-Path $programFilesRoot 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $candidate) {
            $vswhereCandidates += $candidate
        }
    }
}
$vswherePath = $vswhereCandidates | Select-Object -First 1
if (-not $vswherePath) {
    throw '找不到 Visual Studio Installer 的 vswhere.exe。'
}

$vsRoot = (& $vswherePath -latest -version '[17.0,18.0)' -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
    Select-Object -First 1).Trim()
if (-not $vsRoot) {
    throw '找不到带有 C++ x64 工具链的 Visual Studio 2022 安装。'
}

$installationVersion = (& $vswherePath -latest -version '[17.0,18.0)' -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion |
    Select-Object -First 1).Trim()
if (-not $installationVersion -or [version]$installationVersion -lt [version]'17.0' -or
    [version]$installationVersion -ge [version]'18.0') {
    throw "选中的 Visual Studio 不是 2022（17.x）：$installationVersion"
}

$msbuildPath = Join-Path $vsRoot 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuildPath)) {
    throw "找不到 MSBuild：$msbuildPath"
}

& $msbuildPath $solutionPath '/m' '/nologo' '/v:minimal' `
    "/p:Configuration=$Configuration" '/p:Platform=x64' '/p:PlatformToolset=v143'
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild 失败，退出码：$LASTEXITCODE"
}

$dllPath = Join-Path $projectRoot "build\$Configuration\LocalMarkdownPreview.dll"
$smokePath = Join-Path $projectRoot "build\$Configuration\NativeSmokeTests.exe"
$markdownTestsPath = Join-Path $projectRoot "build\$Configuration\MarkdownDocumentTests.exe"
$imageTestsPath = Join-Path $projectRoot "build\$Configuration\SafeImageLoaderTests.exe"
if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "构建完成但没有找到 DLL：$dllPath"
}
if (-not (Test-Path -LiteralPath $smokePath)) {
    throw "构建完成但没有找到烟雾测试：$smokePath"
}
if (-not (Test-Path -LiteralPath $markdownTestsPath)) {
    throw "构建完成但没有找到 Markdown 测试：$markdownTestsPath"
}
if (-not (Test-Path -LiteralPath $imageTestsPath)) {
    throw "构建完成但没有找到图片安全测试：$imageTestsPath"
}

& $markdownTestsPath
if ($LASTEXITCODE -ne 0) {
    throw "Markdown 安全解析测试失败，退出码：$LASTEXITCODE"
}

& $imageTestsPath
if ($LASTEXITCODE -ne 0) {
    throw "图片安全测试失败，退出码：$LASTEXITCODE"
}

& $smokePath $dllPath
if ($LASTEXITCODE -ne 0) {
    throw "原生 COM 烟雾测试失败，退出码：$LASTEXITCODE"
}

$artifact = Get-Item -LiteralPath $dllPath
Write-Output "构建和烟雾测试成功：$($artifact.FullName)"
Write-Output "DLL 大小：$($artifact.Length) 字节"
