# 当前用户范围的 Markdown Preview Handler 开发卸载工具。
#
# 默认只检查并显示计划，不写入注册表。真正恢复需要显式传入 -Apply；
# -WhatIf 可用于演练。恢复只使用注册时生成的备份，并且逐项检查当前值
# 是否仍是本项目写入的值；如果用户已经换了其他处理器，就保留新选择。
# 工具不会修改默认打开程序、图标，也不会结束 Explorer。

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter()]
    [string]$BackupPath = (Join-Path -Path ([Environment]::GetFolderPath('LocalApplicationData')) -ChildPath 'LocalMarkdownPreview\development-registration-backup.json'),

    [Parameter()]
    [string]$RegistryRoot = 'Registry::HKEY_CURRENT_USER',

    [Parameter()]
    [switch]$Apply,

    [Parameter()]
    [switch]$KeepBackup
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$handlerClsid = '{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}'
$handlerAppId = '{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}'
$handlerProgId = 'LocalMarkdownPreview.Handler.1'
$handlerVersionIndependentProgId = 'LocalMarkdownPreview.Handler'
$surrogatePath = 'Prevhost.exe'
$previewHandlerCategory = '{8895b1c6-b41f-4c1c-a562-0d564250836f}'
$displayName = 'Local Markdown Preview Handler'

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

    Assert-RegistryPathInScope -Path $Path
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

function Convert-ValueForRegistryKind {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Value,

        [Parameter(Mandatory = $true)]
        [string]$Kind
    )

    if ($null -eq $Value) {
        return $null
    }

    switch ($Kind) {
        'String' { return [string]$Value }
        'ExpandString' { return [string]$Value }
        'DWord' { return [uint32]$Value }
        'QWord' { return [uint64]$Value }
        'MultiString' { return [string[]]@($Value) }
        'Binary' { return [byte[]]@($Value) }
        default { return $Value }
    }
}

function Remove-EmptyCreatedKeys {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$KeyCaptures
    )

    foreach ($capture in @($KeyCaptures | Where-Object { -not [bool]$_.existsBefore } | Sort-Object { ([string]$_.path).Length } -Descending)) {
        $path = [string]$capture.path
        $key = Get-RegistryKeySafe -Path $path
        if ($null -eq $key) {
            continue
        }

        $isEmpty = @($key.GetValueNames()).Count -eq 0 -and @($key.GetSubKeyNames()).Count -eq 0
        $key.Close()
        if (-not $isEmpty) {
            continue
        }

        $prefix = 'Registry::HKEY_CURRENT_USER'
        $relativePath = $path.Substring($prefix.Length).TrimStart('\')
        $lastSeparator = $relativePath.LastIndexOf('\')
        if ($lastSeparator -lt 0) {
            continue
        }

        $parentRelativePath = $relativePath.Substring(0, $lastSeparator)
        $childName = $relativePath.Substring($lastSeparator + 1)
        $parent = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($parentRelativePath, $true)
        if ($null -ne $parent) {
            try {
                $parent.DeleteSubKeyTree($childName, $false)
            }
            finally {
                $parent.Close()
            }
        }
    }
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

function Get-BackupEntry {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Entries,

        [Parameter(Mandatory = $true)]
        [string]$Id
    )

    return @($Entries | Where-Object { [string]$_.id -eq $Id } | Select-Object -First 1)
}

function Test-BackupExpectedValue {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Entry
    )

    $state = Get-RegistryValueState -Path ([string]$Entry.path) -Name ([string]$Entry.name)
    if (-not $state.exists) {
        return $false
    }

    return ([string]$state.value) -ceq ([string]$Entry.expected)
}

function Restore-BackupEntry {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Entry
    )

    if ([bool]$Entry.existsBefore) {
        $value = Convert-ValueForRegistryKind -Value $Entry.valueBefore -Kind ([string]$Entry.kindBefore)
        Set-RegistryValueSafe -Path ([string]$Entry.path) -Name ([string]$Entry.name) -Value $value -Kind ([string]$Entry.kindBefore)
        return 'restored-original'
    }

    Remove-RegistryValueSafe -Path ([string]$Entry.path) -Name ([string]$Entry.name)
    return 'removed-project-value'
}

$registryPrefix = 'Registry::HKEY_CURRENT_USER'
if (-not ($RegistryRoot.Equals($registryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $RegistryRoot.StartsWith($registryPrefix + '\', [StringComparison]::OrdinalIgnoreCase))) {
    throw '开发卸载工具只允许使用 HKEY_CURRENT_USER，拒绝机器范围或合并根路径。'
}

$RegistryRoot = $RegistryRoot.TrimEnd('\')
$script:RegistryRoot = $RegistryRoot
$resolvedBackupPath = [System.IO.Path]::GetFullPath($BackupPath)
$classesRoot = Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Classes'
$previewHandlersRoot = Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft\Windows\CurrentVersion\PreviewHandlers'
$machineClassesRoot = $null
$machinePreviewHandlersRoot = $null
if ($RegistryRoot.Equals('Registry::HKEY_CURRENT_USER', [StringComparison]::OrdinalIgnoreCase)) {
    # 机器级键只用于只读显示；卸载绝不写入 HKLM。
    $machineClassesRoot = 'Registry::HKEY_LOCAL_MACHINE\Software\Classes'
    $machinePreviewHandlersRoot = 'Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers'
}
$handlerClsidRoot = Join-RegistryPath -Parent $classesRoot -Child ('CLSID\' + $handlerClsid)
$handlerInprocRoot = Join-RegistryPath -Parent $handlerClsidRoot -Child 'InprocServer32'
$handlerAppIdRoot = Join-RegistryPath -Parent $classesRoot -Child ('AppID\' + $handlerAppId)
$handlerProgIdRoot = Join-RegistryPath -Parent $classesRoot -Child $handlerProgId
$handlerProgIdShellexRoot = Join-RegistryPath -Parent $handlerProgIdRoot -Child 'shellex'
$handlerProgIdMappingRoot = Join-RegistryPath -Parent $handlerProgIdShellexRoot -Child $previewHandlerCategory
$mdRoot = Join-RegistryPath -Parent $classesRoot -Child '.md'
$markdownRoot = Join-RegistryPath -Parent $classesRoot -Child '.markdown'
$mdMappingRoot = Join-RegistryPath -Parent $mdRoot -Child ('shellex\' + $previewHandlerCategory)
$markdownMappingRoot = Join-RegistryPath -Parent $markdownRoot -Child ('shellex\' + $previewHandlerCategory)

Show-ExistingHandlers -Extensions @('.md', '.markdown') -ClassesRoot $classesRoot -PreviewHandlersRoot $previewHandlersRoot -MachineClassesRoot $machineClassesRoot -MachinePreviewHandlersRoot $machinePreviewHandlersRoot

$summary = [ordered]@{
    mode = if ($Apply) { 'apply-requested' } else { 'dry-run' }
    applied = $false
    registryRoot = $RegistryRoot
    backupPath = $resolvedBackupPath
    restored = @()
    skipped = @()
}

if (-not (Test-Path -LiteralPath $resolvedBackupPath -PathType Leaf)) {
    Write-Warning "找不到注册备份：$resolvedBackupPath；为避免误删，卸载取消。"
    $summary.mode = 'cancelled-missing-backup'
    $summary | ConvertTo-Json -Depth 10
    if ($Apply) {
        throw "找不到注册备份：$resolvedBackupPath"
    }
    return
}

try {
    $backupText = Get-Content -LiteralPath $resolvedBackupPath -Encoding UTF8 -Raw
    $backup = $backupText | ConvertFrom-Json
}
catch {
    throw "注册备份不是有效的 UTF-8 JSON：$resolvedBackupPath；$($_.Exception.Message)"
}

if ([int]$backup.schemaVersion -ne 1) {
    throw "不支持的注册备份版本：$($backup.schemaVersion)"
}
if ([string]$backup.scope -cne 'HKEY_CURRENT_USER') {
    throw '注册备份不是当前用户范围，拒绝恢复。'
}
if ([string]$backup.registryRoot -cne $RegistryRoot) {
    throw "注册备份范围与当前参数不一致：$($backup.registryRoot)"
}
if ([string]$backup.identifiers.clsid -cne $handlerClsid -or
    [string]$backup.identifiers.appid -cne $handlerAppId -or
    [string]$backup.identifiers.progid -cne $handlerProgId -or
    [string]$backup.identifiers.versionIndependentProgid -cne $handlerVersionIndependentProgId -or
    [string]$backup.identifiers.previewHandlerCategory -cne $previewHandlerCategory) {
    throw '注册备份的固定标识与本项目不一致，拒绝恢复。'
}

$valueEntries = @($backup.values)
$keyEntries = @($backup.keys)
if ($valueEntries.Count -eq 0 -or $keyEntries.Count -eq 0) {
    throw '注册备份缺少原值或键存在性记录，拒绝恢复。'
}
foreach ($entry in $valueEntries) {
    Assert-RegistryPathInScope -Path ([string]$entry.path)
    if (-not ($entry.PSObject.Properties.Name -contains 'expected') -or
        -not ($entry.PSObject.Properties.Name -contains 'existsBefore')) {
        throw "注册备份条目不完整：$($entry.id)"
    }
}
foreach ($entry in $keyEntries) {
    Assert-RegistryPathInScope -Path ([string]$entry.path)
    if (-not ($entry.PSObject.Properties.Name -contains 'existsBefore')) {
        throw "注册备份键条目不完整：$($entry.path)"
    }
}

$expectedDefinitions = @(
    [ordered]@{ id = 'mapping.md'; path = $mdMappingRoot; name = ''; expected = $handlerClsid },
    [ordered]@{ id = 'mapping.markdown'; path = $markdownMappingRoot; name = ''; expected = $handlerClsid },
    [ordered]@{ id = 'progid.mapping'; path = $handlerProgIdMappingRoot; name = ''; expected = $handlerClsid },
    [ordered]@{ id = 'preview.list'; path = $previewHandlersRoot; name = $handlerClsid; expected = $displayName },
    [ordered]@{ id = 'clsid.default'; path = $handlerClsidRoot; name = ''; expected = $displayName },
    [ordered]@{ id = 'clsid.appid'; path = $handlerClsidRoot; name = 'AppID'; expected = $handlerAppId },
    [ordered]@{ id = 'inproc.server'; path = $handlerInprocRoot; name = ''; expected = [string]$backup.handlerPath },
    [ordered]@{ id = 'inproc.threading'; path = $handlerInprocRoot; name = 'ThreadingModel'; expected = 'Apartment' },
    [ordered]@{ id = 'inproc.progid'; path = $handlerInprocRoot; name = 'ProgID'; expected = $handlerProgId },
    [ordered]@{ id = 'inproc.version-independent-progid'; path = $handlerInprocRoot; name = 'VersionIndependentProgID'; expected = $handlerVersionIndependentProgId },
    [ordered]@{ id = 'appid.default'; path = $handlerAppIdRoot; name = ''; expected = $displayName },
    [ordered]@{ id = 'appid.surrogate'; path = $handlerAppIdRoot; name = 'DllSurrogate'; expected = $surrogatePath }
)
if ([string]::IsNullOrWhiteSpace([string]$backup.handlerPath) -or
    [System.IO.Path]::GetExtension([string]$backup.handlerPath) -ine '.dll') {
    throw '注册备份缺少有效的 Handler DLL 路径，拒绝恢复。'
}
foreach ($definition in $expectedDefinitions) {
    $matching = @($valueEntries | Where-Object { [string]$_.id -eq [string]$definition.id })
    if ($matching.Count -ne 1) {
        throw "注册备份缺少或重复条目：$($definition.id)"
    }
    $entry = $matching[0]
    if ([string]$entry.path -cne [string]$definition.path -or
        [string]$entry.name -cne [string]$definition.name -or
        [string]$entry.expected -cne [string]$definition.expected) {
        throw "注册备份条目与固定注册结构不一致：$($definition.id)"
    }
}

$allowedKeyPaths = @(
    $RegistryRoot,
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software'),
    $classesRoot,
    (Join-RegistryPath -Parent $classesRoot -Child 'CLSID'),
    (Join-RegistryPath -Parent $classesRoot -Child 'AppID'),
    $mdRoot,
    (Join-RegistryPath -Parent $mdRoot -Child 'shellex'),
    $mdMappingRoot,
    $markdownRoot,
    (Join-RegistryPath -Parent $markdownRoot -Child 'shellex'),
    $markdownMappingRoot,
    $handlerProgIdRoot,
    $handlerProgIdShellexRoot,
    $handlerProgIdMappingRoot,
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft'),
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft\Windows'),
    (Join-RegistryPath -Parent $RegistryRoot -Child 'Software\Microsoft\Windows\CurrentVersion'),
    $previewHandlersRoot,
    $handlerClsidRoot,
    $handlerInprocRoot,
    $handlerAppIdRoot
)
foreach ($entry in $keyEntries) {
    $path = [string]$entry.path
    if (@($allowedKeyPaths | Where-Object { $_ -ieq $path }).Count -ne 1) {
        throw "注册备份包含不属于固定注册结构的键：$path"
    }
}

$mappingIds = @('mapping.md', 'mapping.markdown', 'progid.mapping', 'preview.list')
$classIds = @('clsid.default', 'clsid.appid', 'inproc.server', 'inproc.threading', 'inproc.progid', 'inproc.version-independent-progid')
$appIdIds = @('appid.default', 'appid.surrogate')
$mappingEntries = foreach ($id in $mappingIds) {
    $entry = Get-BackupEntry -Entries $valueEntries -Id $id
    if ($entry.Count -ne 1) { throw "注册备份缺少条目：$id" }
    $entry[0]
}
$classEntries = foreach ($id in $classIds) {
    $entry = Get-BackupEntry -Entries $valueEntries -Id $id
    if ($entry.Count -ne 1) { throw "注册备份缺少条目：$id" }
    $entry[0]
}
$appIdEntries = foreach ($id in $appIdIds) {
    $entry = Get-BackupEntry -Entries $valueEntries -Id $id
    if ($entry.Count -ne 1) { throw "注册备份缺少条目：$id" }
    $entry[0]
}

$actions = [System.Collections.Generic.List[object]]::new()
foreach ($entry in $mappingEntries) {
    if (Test-BackupExpectedValue -Entry $entry) {
        $actions.Add([ordered]@{ id = [string]$entry.id; entry = $entry; action = 'restore' })
    }
    else {
        $summary.skipped += [string]$entry.id
    }
}

$classOwned = (@($classEntries | Where-Object { -not (Test-BackupExpectedValue -Entry $_) }).Count -eq 0)
$appIdOwned = (@($appIdEntries | Where-Object { -not (Test-BackupExpectedValue -Entry $_) }).Count -eq 0)
if ($classOwned -and $appIdOwned) {
    foreach ($entry in @($classEntries + $appIdEntries)) {
        $actions.Add([ordered]@{ id = [string]$entry.id; entry = $entry; action = 'restore' })
    }
}
else {
    foreach ($entry in @($classEntries + $appIdEntries)) {
        $summary.skipped += [string]$entry.id
    }
}

if ($Apply -and $actions.Count -gt 0) {
    if (-not $PSCmdlet.ShouldProcess('当前用户 Preview Handler 注册表', '按所有权检查恢复并清理开发注册')) {
        $summary.mode = 'what-if'
        $summary | ConvertTo-Json -Depth 10
        return
    }

    foreach ($action in $actions) {
        $result = Restore-BackupEntry -Entry $action.entry
        $summary.restored += ("{0}:{1}" -f $action.id, $result)
    }
    Remove-EmptyCreatedKeys -KeyCaptures $keyEntries
    $summary.mode = 'applied'
    $summary.applied = $true
    if (-not $KeepBackup -and [IO.File]::Exists($resolvedBackupPath)) {
        [IO.File]::Delete($resolvedBackupPath)
        $backupDirectory = [IO.Path]::GetDirectoryName($resolvedBackupPath)
        if (-not [string]::IsNullOrWhiteSpace($backupDirectory) -and
            [IO.Directory]::Exists($backupDirectory) -and
            [IO.Directory]::GetFileSystemEntries($backupDirectory).Count -eq 0) {
            [IO.Directory]::Delete($backupDirectory, $false)
        }
        $summary.backupRemoved = $true
    }
    Write-Host '已恢复仍属于本项目的注册值；检测到用户新选择的映射已保留。'
}
elseif (-not $Apply) {
    if ($actions.Count -eq 0) {
        Write-Host '演练完成：没有仍指向本项目的值需要恢复。'
    }
    else {
        Write-Host ("演练完成：将恢复 {0} 项；未写入注册表。实际恢复请显式传入 -Apply。" -f $actions.Count)
    }
    $summary.mode = 'dry-run'
}
else {
    Write-Host '没有仍指向本项目的注册值；未修改用户当前选择。'
    $summary.mode = 'nothing-to-restore'
}

$summary | ConvertTo-Json -Depth 10
