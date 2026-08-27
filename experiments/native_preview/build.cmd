@echo off
setlocal

set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo 找不到 Visual Studio Installer 的 vswhere.exe。
    exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
    echo 找不到带有 C++ x64 工具链的 Visual Studio 2022 安装。
    exit /b 1
)

set "MSBUILD=%VSROOT%\MSBuild\Current\Bin\amd64\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo 找不到 MSBuild：%MSBUILD%
    exit /b 1
)

"%MSBUILD%" "%ROOT%native_preview.sln" /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b %errorlevel%

if not exist "%ROOT%build\Release\native_preview.dll" (
    echo 构建完成但没有找到产物。
    exit /b 1
)

for %%I in ("%ROOT%build\Release\native_preview.dll") do echo 构建成功：%%~fI && echo 产物大小：%%~zI 字节
exit /b 0
