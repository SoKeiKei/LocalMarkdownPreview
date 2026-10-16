[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [switch] $RunTests
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$solutionPath = Join-Path $projectRoot 'md4c_parser_spike.sln'

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswherePath = $vswhereCandidates |
    Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    Select-Object -First 1
if (-not $vswherePath) {
    throw '找不到 Visual Studio Installer 的 vswhere.exe。'
}

$vsRoot = (& $vswherePath -latest -version '[17.0,18.0)' -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1).Trim()
if (-not $vsRoot) {
    throw '找不到带有 C++ x64 工具链的 Visual Studio 2022 安装。'
}

$msbuildPath = Join-Path $vsRoot 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuildPath)) {
    throw "找不到 MSBuild：$msbuildPath"
}

& $msbuildPath $solutionPath '/m' '/nologo' '/v:minimal' `
    "/p:Configuration=$Configuration" '/p:Platform=x64'
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild 失败，退出码：$LASTEXITCODE"
}

$exePath = Join-Path $projectRoot "build\$Configuration\md4c_parser_spike.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "构建完成但没有找到产物：$exePath"
}

$artifact = Get-Item -LiteralPath $exePath
Write-Output "构建成功：$($artifact.FullName)"
Write-Output "产物大小：$($artifact.Length) 字节"
Write-Output '工具集：Visual Studio 2022 v143；平台：Release|x64（或指定配置）'

if ($RunTests) {
    & $exePath '--test'
    if ($LASTEXITCODE -ne 0) {
        throw "自动测试失败，退出码：$LASTEXITCODE"
    }
}
