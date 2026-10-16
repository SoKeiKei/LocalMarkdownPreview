# 当前用户范围的 Markdown Preview Handler 开发注册工具。
#
# 默认只检查并显示计划，不写入注册表。真正写入需要显式传入 -Apply；
# 如发现已有其他处理器，仍需再传入 -AllowReplacement。-WhatIf 可用于演练。
#
# 注册结构依据微软文档：
# https://learn.microsoft.com/en-us/windows/win32/shell/how-to-register-a-preview-handler
# https://learn.microsoft.com/en-us/windows/win32/shell/preview-handlers
#
# 这里不修改扩展名默认值或 DefaultIcon。扩展名的预览类别直接注册到
# .md/.markdown\shellex\{8895...}，固定 ProgID 仍按微软结构注册并带有同一
# shellex 映射。这样既保留用户现有的打开程序，也让 Preview Handler 映射明确。

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter()]
    [string]$HandlerPath = (Join-Path -Path $PSScriptRoot -ChildPath '..\build\Release\LocalMarkdownPreview.dll'),

    [Parameter()]
    [string]$BackupPath = (Join-Path -Path ([Environment]::GetFolderPath('LocalApplicationData')) -ChildPath 'LocalMarkdownPreview\development-registration-backup.json'),

    [Parameter()]
    [string]$RegistryRoot = 'Registry::HKEY_CURRENT_USER',

    [Parameter()]
    [switch]$Apply,

    [Parameter()]
    [switch]$AllowReplacement,

    [Parameter()]
    [switch]$OverwriteBackup
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$handlerClsid = '{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}'
$handlerAppId = '{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}'
$handlerProgId = 'LocalMarkdownPreview.Handler.1'
$handlerVersionIndependentProgId = 'LocalMarkdownPreview.Handler'
$previewHandlerCategory = '{8895b1c6-b41f-4c1c-a562-0d564250836f}'
$displayName = 'Local Markdown Preview Handler'
$surrogatePath = 'Prevhost.exe'

function Join-RegistryPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Parent,

        [Parameter(Mandatory = $true)]
        [string]$Child
    )

    return ($Parent.TrimEnd('\') + '\' + $Child.TrimStart('\'))
}

function Test-RegistryPathInScope {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $root = $script:RegistryRoot.TrimEnd('\')
    return $Path.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
        $Path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-RegistryPathInScope {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-RegistryPathInScope -Path $Path)) {
        throw "拒绝访问范围外的注册表路径：$Path"
    }
}

function Get-RegistryKeySafe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    Assert-RegistryPathInScope -Path $Path
    try {
        return Get-Item -LiteralPath $Path -ErrorAction Stop
    }
    catch {
        return $null
    }
}

function Get-RegistryValueState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name
    )

    $key = Get-RegistryKeySafe -Path $Path
    if ($null -eq $key) {
        return [ordered]@{
            path = $Path
            name = $Name
            keyExists = $false
            exists = $false
            kind = $null
            value = $null
        }
    }

    $valueNames = @($key.GetValueNames())
    if (-not ($valueNames -contains $Name)) {
        return [ordered]@{
            path = $Path
            name = $Name
            keyExists = $true
            exists = $false
            kind = $null
            value = $null
        }
    }

    return [ordered]@{
        path = $Path
        name = $Name
        keyExists = $true
        exists = $true
        kind = $key.GetValueKind($Name).ToString()
        value = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    }
}

function Get-RegistryValueStateReadOnlyAny {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name
    )

    $allowedPrefixes = @('Registry::HKEY_CURRENT_USER', 'Registry::HKEY_LOCAL_MACHINE')
    $inAllowedHive = @($allowedPrefixes | Where-Object {
            $Path.Equals($_, [StringComparison]::OrdinalIgnoreCase) -or
            $Path.StartsWith($_ + '\', [StringComparison]::OrdinalIgnoreCase)
        }).Count -eq 1
    if (-not $inAllowedHive) {
        throw "只允许只读检查 HKCU/HKLM：$Path"
    }

    try {
        $key = Get-Item -LiteralPath $Path -ErrorAction Stop
    }
    catch {
        return [ordered]@{
            path = $Path
            name = $Name
            keyExists = $false
            exists = $false
            kind = $null
            value = $null
        }
    }

    try {
        $valueNames = @($key.GetValueNames())
        if (-not ($valueNames -contains $Name)) {
            return [ordered]@{
                path = $Path
                name = $Name
                keyExists = $true
                exists = $false
                kind = $null
                value = $null
            }
        }

        return [ordered]@{
            path = $Path
            name = $Name
            keyExists = $true
            exists = $true
            kind = $key.GetValueKind($Name).ToString()
            value = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        }
    }
    finally {
        $key.Close()
    }
}

function Get-RegistryKeyExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return (Get-RegistryKeySafe -Path $Path) -ne $null
}

function Set-RegistryValueSafe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Value,

        [Parameter(Mandatory = $true)]
        [string]$Kind
    )

    Assert-RegistryPathInScope -Path $Path
    $prefix = 'Registry::HKEY_CURRENT_USER'
    $relativePath = $Path.Substring($prefix.Length).TrimStart('\')
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($relativePath, $true)
    if ($null -eq $key) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($relativePath)
    }

    if ($null -eq $key) {
        throw "无法创建注册表键：$Path"
    }

    try {
        $registryKind = [Microsoft.Win32.RegistryValueKind]::Parse(
            [Microsoft.Win32.RegistryValueKind],
            $Kind
        )
        $key.SetValue($Name, $Value, $registryKind)
    }
    finally {
        $key.Close()
    }
}

function Remove-RegistryValueSafe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name
    )

    $prefix = 'Registry::HKEY_CURRENT_USER'
    $relativePath = $Path.Substring($prefix.Length).TrimStart('\')
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($relativePath, $true)
    if ($null -ne $key) {
        try {
            if (@($key.GetValueNames()) -contains $Name) {
                $key.DeleteValue($Name, $false)
            }
        }
        finally {
            $key.Close()
        }
    }
}

function Get-ExpectedValueText {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Value
    )

    if ($null -eq $Value) {
        return $null
    }

    return [string]$Value
}

function New-RegistrationValueCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Id,

        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Expected
    )

    $state = Get-RegistryValueState -Path $Path -Name $Name
    return [ordered]@{
        id = $Id
        path = $Path
        name = $Name
        keyExistsBefore = [bool]$state.keyExists
        existsBefore = [bool]$state.exists
        kindBefore = $state.kind
        valueBefore = $state.value
        expected = (Get-ExpectedValueText -Value $Expected)
    }
}

function New-RegistrationKeyCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return [ordered]@{
        path = $Path
        existsBefore = [bool](Get-RegistryKeyExists -Path $Path)
    }
}

function Write-Utf8JsonNoBom {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [object]$Value
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $directory = [System.IO.Path]::GetDirectoryName($resolvedPath)
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    }

    $json = $Value | ConvertTo-Json -Depth 20
    $utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($resolvedPath, $json + [Environment]::NewLine, $utf8WithoutBom)
}

function Test-ExpectedRegistryValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Expected
    )

    $state = Get-RegistryValueState -Path $Path -Name $Name
    if (-not $state.exists) {
        return $false
    }

    return ([string]$state.value) -ceq (Get-ExpectedValueText -Value $Expected)
}

function Show-ExistingHandlers {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Extensions,

        [Parameter(Mandatory = $true)]
        [string]$ClassesRoot,

        [Parameter(Mandatory = $true)]
        [string]$PreviewHandlersRoot,

        [Parameter()]
        [AllowNull()]
        [string]$MachineClassesRoot,

        [Parameter()]
        [AllowNull()]
        [string]$MachinePreviewHandlersRoot
    )

    Write-Host '现有 Preview Handler 检查（只读）：'
    foreach ($extension in $Extensions) {
        $extensionPath = Join-RegistryPath -Parent $ClassesRoot -Child $extension
        $extensionDefault = Get-RegistryValueState -Path $extensionPath -Name ''
        $mappingPath = Join-RegistryPath -Parent $extensionPath -Child ('shellex\' + $script:previewHandlerCategory)
        $mapping = Get-RegistryValueState -Path $mappingPath -Name ''
        $defaultText = if ($extensionDefault.exists) { [string]$extensionDefault.value } else { '<未设置>' }
        $mappingText = if ($mapping.exists) { [string]$mapping.value } else { '<未设置>' }
        Write-Host ("  {0}: 默认打开 ProgID = {1}; 预览 CLSID = {2}" -f $extension, $defaultText, $mappingText)

        if ($extensionDefault.exists -and -not [string]::IsNullOrWhiteSpace([string]$extensionDefault.value)) {
            $progidPath = Join-RegistryPath -Parent $ClassesRoot -Child ([string]$extensionDefault.value)
            $progidMappingPath = Join-RegistryPath -Parent $progidPath -Child ('shellex\' + $script:previewHandlerCategory)
            $progidMapping = Get-RegistryValueState -Path $progidMappingPath -Name ''
            $progidMappingText = if ($progidMapping.exists) { [string]$progidMapping.value } else { '<未设置>' }
            Write-Host ("    ProgID 映射 {0} = {1}" -f ([string]$extensionDefault.value), $progidMappingText)
        }

        if (-not [string]::IsNullOrWhiteSpace($MachineClassesRoot)) {
            $machineExtensionPath = Join-RegistryPath -Parent $MachineClassesRoot -Child $extension
            $machineMappingPath = Join-RegistryPath -Parent $machineExtensionPath -Child ('shellex\' + $script:previewHandlerCategory)
            $machineDefault = Get-RegistryValueStateReadOnlyAny -Path $machineExtensionPath -Name ''
            $machineMapping = Get-RegistryValueStateReadOnlyAny -Path $machineMappingPath -Name ''
            $machineDefaultText = if ($machineDefault.exists) { [string]$machineDefault.value } else { '<未设置>' }
            $machineMappingText = if ($machineMapping.exists) { [string]$machineMapping.value } else { '<未设置>' }
            if ($machineDefault.exists -or $machineMapping.exists) {
                Write-Host ("    HKLM {0}: 默认打开 ProgID = {1}; 预览 CLSID = {2}" -f $extension, $machineDefaultText, $machineMappingText)
            }
        }
    }

    $previewKey = Get-RegistryKeySafe -Path $PreviewHandlersRoot
    if ($null -eq $previewKey) {
        Write-Host '  PreviewHandlers 清单：<未设置>'
        Show-MachinePreviewHandlers -Path $MachinePreviewHandlersRoot
        return
    }

    $previewValues = @($previewKey.GetValueNames() | Sort-Object)
    if ($previewValues.Count -eq 0) {
        Write-Host '  PreviewHandlers 清单：<空>'
        $previewKey.Close()
        Show-MachinePreviewHandlers -Path $MachinePreviewHandlersRoot
        return
    }

    Write-Host '  PreviewHandlers 清单：'
    foreach ($name in $previewValues) {
        $value = $previewKey.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        Write-Host ("    {0} = {1}" -f $name, [string]$value)
    }
    $previewKey.Close()
    Show-MachinePreviewHandlers -Path $MachinePreviewHandlersRoot
}

function Show-MachinePreviewHandlers {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [AllowNull()]
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    try {
        $key = Get-Item -LiteralPath $Path -ErrorAction Stop
    }
    catch {
        return
    }

    try {
        $names = @($key.GetValueNames() | Sort-Object)
        if ($names.Count -eq 0) {
            return
        }
        Write-Host '  HKLM PreviewHandlers 清单：'
        foreach ($name in $names) {
            $value = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            Write-Host ("    {0} = {1}" -f $name, [string]$value)
        }
    }
    finally {
        $key.Close()
    }
}

function Remove-EmptyCreatedKeys {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$KeyCaptures
    )

    foreach ($capture in @($KeyCaptures | Where-Object { -not [bool]$_.existsBefore } | Sort-Object { $_.path.Length } -Descending)) {
        $key = Get-RegistryKeySafe -Path ([string]$capture.path)
        if ($null -eq $key) {
            continue
        }

        if (@($key.GetValueNames()).Count -eq 0 -and @($key.GetSubKeyNames()).Count -eq 0) {
            Remove-Item -LiteralPath ([string]$capture.path) -Force -ErrorAction Stop
        }
    }
}

$registryPrefix = 'Registry::HKEY_CURRENT_USER'
if (-not ($RegistryRoot.Equals($registryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $RegistryRoot.StartsWith($registryPrefix + '\', [StringComparison]::OrdinalIgnoreCase))) {
    throw '开发注册工具只允许使用 HKEY_CURRENT_USER，拒绝机器范围或合并根路径。'
}

$RegistryRoot = $RegistryRoot.TrimEnd('\')
$script:RegistryRoot = $RegistryRoot

$classesRoot = Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Classes'
$previewHandlersRoot = Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft\Windows\CurrentVersion\PreviewHandlers'
$handlerClsidRoot = Join-RegistryPath -Parent $classesRoot -Child ('CLSID\' + $handlerClsid)
$handlerInprocRoot = Join-RegistryPath -Parent $handlerClsidRoot -Child 'InprocServer32'
$handlerAppIdRoot = Join-RegistryPath -Parent $classesRoot -Child ('AppID\' + $handlerAppId)
$handlerProgIdRoot = Join-RegistryPath -Parent $classesRoot -Child $handlerProgId
$handlerProgIdShellexRoot = Join-RegistryPath -Parent $handlerProgIdRoot -Child 'shellex'
$handlerProgIdMappingRoot = Join-RegistryPath -Parent $handlerProgIdShellexRoot -Child $previewHandlerCategory

$extensions = @('.md', '.markdown')
$extensionMappingRoots = @{}
foreach ($extension in $extensions) {
    $extensionRoot = Join-RegistryPath -Parent $classesRoot -Child $extension
    $extensionMappingRoots[$extension] = Join-RegistryPath -Parent $extensionRoot -Child ('shellex\' + $previewHandlerCategory)
}

$machineClassesRoot = $null
$machinePreviewHandlersRoot = $null
if ($RegistryRoot.Equals('Registry::HKEY_CURRENT_USER', [StringComparison]::OrdinalIgnoreCase)) {
    # 机器级键只用于只读冲突检查；所有写入仍通过 HKCU 的 .NET RegistryKey 完成。
    $machineClassesRoot = 'Registry::HKEY_LOCAL_MACHINE\Software\Classes'
    $machinePreviewHandlersRoot = 'Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers'
}

$resolvedHandlerPath = [System.IO.Path]::GetFullPath($HandlerPath)
$resolvedBackupPath = [System.IO.Path]::GetFullPath($BackupPath)
if ([System.IO.Path]::GetExtension($resolvedHandlerPath) -ine '.dll') {
    throw "HandlerPath 必须指向 DLL：$resolvedHandlerPath"
}
if ($Apply -and -not (Test-Path -LiteralPath $resolvedHandlerPath -PathType Leaf)) {
    throw "找不到要注册的 Handler DLL：$resolvedHandlerPath"
}

Show-ExistingHandlers -Extensions $extensions -ClassesRoot $classesRoot -PreviewHandlersRoot $previewHandlersRoot -MachineClassesRoot $machineClassesRoot -MachinePreviewHandlersRoot $machinePreviewHandlersRoot

$operations = @(
    [ordered]@{ id = 'mapping.md'; path = $extensionMappingRoots['.md']; name = ''; expected = $handlerClsid; kind = 'String' },
    [ordered]@{ id = 'mapping.markdown'; path = $extensionMappingRoots['.markdown']; name = ''; expected = $handlerClsid; kind = 'String' },
    [ordered]@{ id = 'progid.mapping'; path = $handlerProgIdMappingRoot; name = ''; expected = $handlerClsid; kind = 'String' },
    [ordered]@{ id = 'preview.list'; path = $previewHandlersRoot; name = $handlerClsid; expected = $displayName; kind = 'String' },
    [ordered]@{ id = 'clsid.default'; path = $handlerClsidRoot; name = ''; expected = $displayName; kind = 'String' },
    [ordered]@{ id = 'clsid.appid'; path = $handlerClsidRoot; name = 'AppID'; expected = $handlerAppId; kind = 'String' },
    [ordered]@{ id = 'inproc.server'; path = $handlerInprocRoot; name = ''; expected = $resolvedHandlerPath; kind = 'String' },
    [ordered]@{ id = 'inproc.threading'; path = $handlerInprocRoot; name = 'ThreadingModel'; expected = 'Apartment'; kind = 'String' },
    [ordered]@{ id = 'inproc.progid'; path = $handlerInprocRoot; name = 'ProgID'; expected = $handlerProgId; kind = 'String' },
    [ordered]@{ id = 'inproc.version-independent-progid'; path = $handlerInprocRoot; name = 'VersionIndependentProgID'; expected = $handlerVersionIndependentProgId; kind = 'String' },
    [ordered]@{ id = 'appid.default'; path = $handlerAppIdRoot; name = ''; expected = $displayName; kind = 'String' },
    [ordered]@{ id = 'appid.surrogate'; path = $handlerAppIdRoot; name = 'DllSurrogate'; expected = $surrogatePath; kind = 'String' }
)

$conflicts = [System.Collections.Generic.List[string]]::new()
$projectOperationCount = 0
foreach ($operation in $operations) {
    if (Test-ExpectedRegistryValue -Path $operation.path -Name $operation.name -Expected $operation.expected) {
        $projectOperationCount++
        continue
    }

    $state = Get-RegistryValueState -Path $operation.path -Name $operation.name
    if ($state.exists) {
        $conflicts.Add(("{0}: 已有值 {1}" -f $operation.id, [string]$state.value))
    }
}

if (-not [string]::IsNullOrWhiteSpace($machineClassesRoot)) {
    $machineOperations = @(
        [ordered]@{ id = 'mapping.md'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child '.md') -Child ('shellex\' + $previewHandlerCategory); expected = $handlerClsid },
        [ordered]@{ id = 'mapping.markdown'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child '.markdown') -Child ('shellex\' + $previewHandlerCategory); expected = $handlerClsid },
        [ordered]@{ id = 'progid.mapping'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child $handlerProgId) -Child 'shellex') -Child $previewHandlerCategory; expected = $handlerClsid },
        [ordered]@{ id = 'preview.list'; path = $machinePreviewHandlersRoot; expected = $displayName },
        [ordered]@{ id = 'clsid.default'; path = Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid); expected = $displayName },
        [ordered]@{ id = 'clsid.appid'; path = Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid); expected = $handlerAppId },
        [ordered]@{ id = 'inproc.server'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid)) -Child 'InprocServer32'; expected = $resolvedHandlerPath },
        [ordered]@{ id = 'inproc.threading'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid)) -Child 'InprocServer32'; expected = 'Apartment' },
        [ordered]@{ id = 'inproc.progid'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid)) -Child 'InprocServer32'; expected = $handlerProgId },
        [ordered]@{ id = 'inproc.version-independent-progid'; path = Join-RegistryPath -Parent (Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid)) -Child 'InprocServer32'; expected = $handlerVersionIndependentProgId },
        [ordered]@{ id = 'appid.default'; path = Join-RegistryPath -Parent $machineClassesRoot -Child ('AppID\' + $handlerAppId); expected = $displayName },
        [ordered]@{ id = 'appid.surrogate'; path = Join-RegistryPath -Parent $machineClassesRoot -Child ('AppID\' + $handlerAppId); expected = $surrogatePath }
    )
    foreach ($machineOperation in $machineOperations) {
        $userOperation = @($operations | Where-Object { $_.id -eq $machineOperation.id })[0]
        if (Test-ExpectedRegistryValue -Path $userOperation.path -Name $userOperation.name -Expected $userOperation.expected) {
            continue
        }
        $machineName = if ($machineOperation.id -eq 'preview.list') { $handlerClsid } else { $userOperation.name }
        $machineState = Get-RegistryValueStateReadOnlyAny -Path $machineOperation.path -Name $machineName
        if ($machineState.exists -and [string]$machineState.value -cne [string]$machineOperation.expected) {
            $conflicts.Add(("HKLM.{0}: 已有值 {1}" -f $machineOperation.id, [string]$machineState.value))
        }
    }
}

$disableLowIl = Get-RegistryValueState -Path $handlerClsidRoot -Name 'DisableLowILProcessIsolation'
$machineDisableLowIl = $null
if (-not [string]::IsNullOrWhiteSpace($machineClassesRoot)) {
    $machineClsidRoot = Join-RegistryPath -Parent $machineClassesRoot -Child ('CLSID\' + $handlerClsid)
    $machineDisableLowIl = Get-RegistryValueStateReadOnlyAny -Path $machineClsidRoot -Name 'DisableLowILProcessIsolation'
}
if ($disableLowIl.exists -and [string]$disableLowIl.value -eq '1') {
    $conflicts.Add('clsid.DisableLowILProcessIsolation=1 会关闭低完整性隔离；工具不会保留或覆盖该设置。')
}
if ($null -ne $machineDisableLowIl -and $machineDisableLowIl.exists -and [string]$machineDisableLowIl.value -eq '1') {
    $conflicts.Add('HKLM clsid.DisableLowILProcessIsolation=1 会关闭低完整性隔离；工具不会修改机器级设置。')
}

$allOperationsAlreadyPresent = $projectOperationCount -eq $operations.Count
$backupAlreadyExists = Test-Path -LiteralPath $resolvedBackupPath -PathType Leaf

$keyPaths = @(
    $RegistryRoot,
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software'),
    $classesRoot,
    (Join-RegistryPath -Parent $classesRoot -Child 'CLSID'),
    (Join-RegistryPath -Parent $classesRoot -Child 'AppID'),
    $extensionMappingRoots['.md'],
    $extensionMappingRoots['.markdown'],
    (Join-RegistryPath -Parent (Join-RegistryPath -Parent $classesRoot -Child '.md') -Child 'shellex'),
    (Join-RegistryPath -Parent (Join-RegistryPath -Parent $classesRoot -Child '.markdown') -Child 'shellex'),
    (Join-RegistryPath -Parent $classesRoot -Child '.md'),
    (Join-RegistryPath -Parent $classesRoot -Child '.markdown'),
    $handlerProgIdMappingRoot,
    $handlerProgIdShellexRoot,
    $handlerProgIdRoot,
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft'),
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft\Windows'),
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft\Windows\CurrentVersion'),
    $previewHandlersRoot,
    $handlerInprocRoot,
    $handlerClsidRoot,
    $handlerAppIdRoot
)
$keyPaths = @($keyPaths | Sort-Object -Unique)

$valueCaptures = foreach ($operation in $operations) {
    New-RegistrationValueCapture -Id $operation.id -Path $operation.path -Name $operation.name -Expected $operation.expected
}
$keyCaptures = foreach ($keyPath in $keyPaths) {
    New-RegistrationKeyCapture -Path $keyPath
}

$summary = [ordered]@{
    mode = if ($Apply) { 'apply-requested' } else { 'dry-run' }
    applied = $false
    cancelled = $false
    registryRoot = $RegistryRoot
    handlerPath = $resolvedHandlerPath
    backupPath = $resolvedBackupPath
    conflictCount = $conflicts.Count
    conflicts = @($conflicts)
}

$lowIlOptOut = ($disableLowIl.exists -and [string]$disableLowIl.value -eq '1') -or
    ($null -ne $machineDisableLowIl -and $machineDisableLowIl.exists -and [string]$machineDisableLowIl.value -eq '1')
if ($lowIlOptOut) {
    Write-Warning '现有 CLSID 明确关闭了低完整性隔离；为保证安全，注册已取消，即使传入 -AllowReplacement 也不会覆盖。'
    $summary.cancelled = $true
    $summary.mode = 'cancelled-low-il'
    $summary | ConvertTo-Json -Depth 10
    return
}

if ($allOperationsAlreadyPresent) {
    if ($backupAlreadyExists) {
        Write-Host '本项目已完整注册，保留现有备份，不重复写入。'
        $summary.mode = 'already-registered'
        $summary.applied = $false
        $summary | ConvertTo-Json -Depth 10
        return
    }

    if ($Apply) {
        throw '当前注册表已经指向本项目，但找不到原值备份；为避免卸载时误删，先停止。'
    }

    Write-Host '本项目的目标值已经存在，但没有备份；演练结束，不写入。'
    $summary.mode = 'already-registered-without-backup'
    $summary | ConvertTo-Json -Depth 10
    return
}

if ($conflicts.Count -gt 0 -and -not $AllowReplacement) {
    Write-Warning '检测到已有值，默认取消，不会静默覆盖。确认要替换时请显式传入 -AllowReplacement。'
    $summary.cancelled = $true
    $summary.mode = 'cancelled-conflict'
    $summary | ConvertTo-Json -Depth 10
    return
}

if (-not $Apply) {
    Write-Host '演练完成：未写入注册表，也未创建备份。需要实际注册时请显式传入 -Apply。'
    $summary.mode = 'dry-run'
    $summary | ConvertTo-Json -Depth 10
    return
}

if (-not $PSCmdlet.ShouldProcess('当前用户 Preview Handler 注册表', '写入开发注册并保存原值备份')) {
    $summary.mode = 'what-if'
    $summary | ConvertTo-Json -Depth 10
    return
}

if ($backupAlreadyExists -and -not $OverwriteBackup) {
    throw "备份文件已存在：$resolvedBackupPath；为避免覆盖原值，请更换 -BackupPath 或显式传入 -OverwriteBackup。"
}

$backup = [ordered]@{
    schemaVersion = 1
    capturedAt = [DateTimeOffset]::Now.ToString('o')
    scope = 'HKEY_CURRENT_USER'
    registryRoot = $RegistryRoot
    handlerPath = $resolvedHandlerPath
    identifiers = [ordered]@{
        clsid = $handlerClsid
        appid = $handlerAppId
        progid = $handlerProgId
        versionIndependentProgid = $handlerVersionIndependentProgId
        previewHandlerCategory = $previewHandlerCategory
    }
    values = @($valueCaptures)
    keys = @($keyCaptures)
}

Write-Utf8JsonNoBom -Path $resolvedBackupPath -Value $backup

foreach ($operation in $operations) {
    Set-RegistryValueSafe -Path $operation.path -Name $operation.name -Value $operation.expected -Kind $operation.kind
}

$verificationFailures = foreach ($operation in $operations) {
    if (-not (Test-ExpectedRegistryValue -Path $operation.path -Name $operation.name -Expected $operation.expected)) {
        $operation.id
    }
}
if (@($verificationFailures).Count -gt 0) {
    throw ("注册后校验失败：" + (@($verificationFailures) -join ', '))
}

if (Test-ExpectedRegistryValue -Path $handlerClsidRoot -Name 'DisableLowILProcessIsolation' -Expected '1') {
    throw '注册后发现 DisableLowILProcessIsolation=1，拒绝宣称低完整性注册成功。'
}

$summary.mode = 'applied'
$summary.applied = $true
$summary.backupWritten = $resolvedBackupPath
Write-Host ("已完成当前用户开发注册；原值备份已保存为 UTF-8 无 BOM：{0}" -f $resolvedBackupPath)
$summary | ConvertTo-Json -Depth 10
