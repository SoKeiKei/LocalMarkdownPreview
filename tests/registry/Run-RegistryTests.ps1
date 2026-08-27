# 不依赖 Pester 的注册脚本测试。
# 所有写入都位于本次运行生成的 HKCU 临时子键；finally 会清理该子键和备份文件。

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$registerScript = Join-Path $repoRoot 'tools\Register-DevelopmentPreview.ps1'
$unregisterScript = Join-Path $repoRoot 'tools\Unregister-DevelopmentPreview.ps1'
$handlerPath = Join-Path $env:windir 'System32\msftedit.dll'

$handlerClsid = '{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}'
$handlerAppId = '{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}'
$handlerProgId = 'LocalMarkdownPreview.Handler.1'
$handlerVersionIndependentProgId = 'LocalMarkdownPreview.Handler'
$surrogatePath = 'Prevhost.exe'
$previewHandlerCategory = '{8895b1c6-b41f-4c1c-a562-0d564250836f}'
$registryPrefix = 'Registry::HKEY_CURRENT_USER'
$testId = [guid]::NewGuid().ToString('N')
$registryRoot = "$registryPrefix\Software\LocalMarkdownPreview\RegistryTests\$testId"
$classesRoot = "$registryRoot\Software\Classes"
$backupDirectory = Join-Path ([IO.Path]::GetTempPath()) ("LocalMarkdownPreview.RegistryTests." + $testId)
$backupPath = Join-Path $backupDirectory 'registration-backup.json'

function Join-RegistryPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Parent,

        [Parameter(Mandatory = $true)]
        [string]$Child
    )

    return ($Parent.TrimEnd('\') + '\' + $Child.TrimStart('\'))
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition) {
        throw "断言失败：$Message"
    }
}

function Assert-False {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    Assert-True -Condition (-not $Condition) -Message $Message
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Expected,

        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Actual,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if ([string]$Expected -cne [string]$Actual) {
        throw "断言失败：$Message；预期 [$Expected]，实际 [$Actual]"
    }
}

function Get-RegistryValueForTest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    $key = Get-Item -LiteralPath $Path
    try {
        if (@($key.GetValueNames()) -notcontains $Name) {
            return $null
        }
        return $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    }
    finally {
        $key.Close()
    }
}

function Get-RegistryValueKindForTest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name
    )
    $key = Get-Item -LiteralPath $Path
    try {
        return $key.GetValueKind($Name).ToString()
    }
    finally {
        $key.Close()
    }
}

function Set-RegistryValueForTest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Value
    )

    $relativePath = $Path.Substring($registryPrefix.Length).TrimStart('\')
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($relativePath)
    if ($null -eq $key) {
        throw "测试无法创建注册表键：$Path"
    }
    try {
        $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String)
    }
    finally {
        $key.Close()
    }
}

function Remove-TestRegistryRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not $Path.StartsWith($registryPrefix + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "测试清理路径不在 HKCU：$Path"
    }

    $relativePath = $Path.Substring($registryPrefix.Length).TrimStart('\')
    $separator = $relativePath.LastIndexOf('\')
    if ($separator -lt 0) {
        throw "测试清理路径过宽：$Path"
    }

    $parentRelativePath = $relativePath.Substring(0, $separator)
    $childName = $relativePath.Substring($separator + 1)
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

function Invoke-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [hashtable]$Parameters
    )

    return @(& $Path @Parameters)
}

function Get-ToolParameterSet {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Apply,

        [Parameter(Mandatory = $false)]
        [bool]$WhatIf = $false,

        [Parameter(Mandatory = $false)]
        [bool]$AllowReplacement = $false
    )

    $parameters = @{
        RegistryRoot = $registryRoot
        HandlerPath = $handlerPath
        BackupPath = $backupPath
    }
    if ($Apply) { $parameters.Apply = $true }
    if ($WhatIf) { $parameters.WhatIf = $true }
    if ($AllowReplacement) { $parameters.AllowReplacement = $true }
    return $parameters
}

function Assert-BackupIsUtf8WithoutBom {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $bytes = [IO.File]::ReadAllBytes($Path)
    Assert-True -Condition ($bytes.Length -gt 0) -Message '备份文件不能为空'
    if ($bytes.Length -ge 3) {
        $hasBom = $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        Assert-False -Condition $hasBom -Message '备份文件不能包含 UTF-8 BOM'
    }
}

$testRootCreated = $false
try {
    Assert-True -Condition (Test-Path -LiteralPath $registerScript -PathType Leaf) -Message '注册脚本存在'
    Assert-True -Condition (Test-Path -LiteralPath $unregisterScript -PathType Leaf) -Message '卸载脚本存在'
    Assert-True -Condition (Test-Path -LiteralPath $handlerPath -PathType Leaf) -Message '测试使用的系统 DLL 存在'
    Assert-False -Condition (Test-Path -LiteralPath $registryRoot) -Message '临时注册表根键初始不存在'

    # -Apply -WhatIf 不能写注册表，也不能创建备份。
    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true -WhatIf $true)
    Assert-False -Condition (Test-Path -LiteralPath $registryRoot) -Message '-WhatIf 不创建临时注册表键'
    Assert-False -Condition ([IO.File]::Exists($backupPath)) -Message '-WhatIf 不创建备份'

    # 预先放置一个默认打开程序和图标，注册后必须保持原样。
    $mdRoot = Join-RegistryPath -Parent $classesRoot -Child '.md'
    $mdIconRoot = Join-RegistryPath -Parent $mdRoot -Child 'DefaultIcon'
    Set-RegistryValueForTest -Path $mdRoot -Name '' -Value 'Existing.Markdown.Association'
    Set-RegistryValueForTest -Path $mdIconRoot -Name '' -Value 'existing-icon.dll,0'
    $testRootCreated = $true
    $originalOpenProgId = Get-RegistryValueForTest -Path $mdRoot -Name ''
    $originalIcon = Get-RegistryValueForTest -Path $mdIconRoot -Name ''

    # 默认演练仍不写入。
    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $false)
    Assert-Equal -Expected $originalOpenProgId -Actual (Get-RegistryValueForTest -Path $mdRoot -Name '') -Message '演练不改变默认打开程序'
    Assert-False -Condition (Test-Path -LiteralPath (Join-RegistryPath -Parent $mdRoot -Child ('shellex\' + $previewHandlerCategory))) -Message '演练不创建预览映射'

    # 正常注册到临时根键。
    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true)
    $mdMapping = Join-RegistryPath -Parent $mdRoot -Child ('shellex\' + $previewHandlerCategory)
    $markdownRoot = Join-RegistryPath -Parent $classesRoot -Child '.markdown'
    $markdownMapping = Join-RegistryPath -Parent $markdownRoot -Child ('shellex\' + $previewHandlerCategory)
    $progidMapping = Join-RegistryPath -Parent (Join-RegistryPath -Parent (Join-RegistryPath -Parent $classesRoot -Child $handlerProgId) -Child 'shellex') -Child $previewHandlerCategory
    $previewList = "$registryRoot\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers"
    $clsidRoot = Join-RegistryPath -Parent $classesRoot -Child ('CLSID\' + $handlerClsid)
    $inprocRoot = Join-RegistryPath -Parent $clsidRoot -Child 'InprocServer32'
    $appIdRoot = Join-RegistryPath -Parent $classesRoot -Child ('AppID\' + $handlerAppId)

    Assert-Equal -Expected $handlerClsid -Actual (Get-RegistryValueForTest -Path $mdMapping -Name '') -Message '.md 预览映射正确'
    Assert-Equal -Expected $handlerClsid -Actual (Get-RegistryValueForTest -Path $markdownMapping -Name '') -Message '.markdown 预览映射正确'
    Assert-Equal -Expected $handlerClsid -Actual (Get-RegistryValueForTest -Path $progidMapping -Name '') -Message 'ProgID 预览映射正确'
    Assert-Equal -Expected 'Local Markdown Preview Handler' -Actual (Get-RegistryValueForTest -Path $previewList -Name $handlerClsid) -Message 'PreviewHandlers 清单正确'
    Assert-Equal -Expected $handlerAppId -Actual (Get-RegistryValueForTest -Path $clsidRoot -Name 'AppID') -Message 'CLSID 的 AppID 正确'
    Assert-Equal -Expected $handlerPath -Actual (Get-RegistryValueForTest -Path $inprocRoot -Name '') -Message 'InprocServer32 路径正确'
    Assert-Equal -Expected 'Apartment' -Actual (Get-RegistryValueForTest -Path $inprocRoot -Name 'ThreadingModel') -Message '线程模型正确'
    Assert-Equal -Expected $handlerProgId -Actual (Get-RegistryValueForTest -Path $inprocRoot -Name 'ProgID') -Message '版本 ProgID 正确'
    Assert-Equal -Expected $handlerVersionIndependentProgId -Actual (Get-RegistryValueForTest -Path $inprocRoot -Name 'VersionIndependentProgID') -Message '版本无关 ProgID 正确'
    Assert-Equal -Expected $surrogatePath -Actual (Get-RegistryValueForTest -Path $appIdRoot -Name 'DllSurrogate') -Message '预览宿主正确'
    Assert-Equal -Expected 'String' -Actual (Get-RegistryValueKindForTest -Path $appIdRoot -Name 'DllSurrogate') -Message '预览宿主路径类型正确'
    Assert-False -Condition ($null -ne (Get-RegistryValueForTest -Path $clsidRoot -Name 'DisableLowILProcessIsolation')) -Message '没有关闭低完整性隔离'
    Assert-Equal -Expected $originalOpenProgId -Actual (Get-RegistryValueForTest -Path $mdRoot -Name '') -Message '注册不改变默认打开程序'
    Assert-Equal -Expected $originalIcon -Actual (Get-RegistryValueForTest -Path $mdIconRoot -Name '') -Message '注册不改变图标'

    Assert-True -Condition ([IO.File]::Exists($backupPath)) -Message '注册前后备份已生成'
    Assert-BackupIsUtf8WithoutBom -Path $backupPath
    $backup = (Get-Content -LiteralPath $backupPath -Encoding UTF8 -Raw | ConvertFrom-Json)
    Assert-Equal -Expected 1 -Actual $backup.schemaVersion -Message '备份版本正确'
    Assert-Equal -Expected 'HKEY_CURRENT_USER' -Actual $backup.scope -Message '备份范围为当前用户'
    $mdCapture = @($backup.values | Where-Object { $_.id -eq 'mapping.md' })[0]
    Assert-False -Condition ([bool]$mdCapture.existsBefore) -Message '备份记录 .md 原值不存在'

    # 已完整注册时重复执行不会覆盖备份。
    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true)

    # 用户把 .md 换成别的处理器后，卸载必须保留这个新选择。
    $foreignClsid = '{11111111-2222-3333-4444-555555555555}'
    Set-RegistryValueForTest -Path $mdMapping -Name '' -Value $foreignClsid
    $null = Invoke-Tool -Path $unregisterScript -Parameters @{
        RegistryRoot = $registryRoot
        BackupPath = $backupPath
        KeepBackup = $true
    }
    Assert-Equal -Expected $foreignClsid -Actual (Get-RegistryValueForTest -Path $mdMapping -Name '') -Message '演练不改变用户新选择'
    $null = Invoke-Tool -Path $unregisterScript -Parameters @{
        RegistryRoot = $registryRoot
        BackupPath = $backupPath
        Apply = $true
        KeepBackup = $true
    }
    Assert-Equal -Expected $foreignClsid -Actual (Get-RegistryValueForTest -Path $mdMapping -Name '') -Message '卸载保留用户新选择'
    Assert-False -Condition (Test-Path -LiteralPath $markdownMapping) -Message '卸载清理仍属于本项目的 .markdown 映射'
    Assert-False -Condition (Test-Path -LiteralPath $progidMapping) -Message '卸载清理 ProgID 映射'
    Assert-False -Condition (Test-Path -LiteralPath $clsidRoot) -Message '卸载清理 CLSID 键'
    Assert-False -Condition (Test-Path -LiteralPath $appIdRoot) -Message '卸载清理 AppID 键'
    Assert-Equal -Expected $originalOpenProgId -Actual (Get-RegistryValueForTest -Path $mdRoot -Name '') -Message '卸载不改变默认打开程序'
    Assert-Equal -Expected $originalIcon -Actual (Get-RegistryValueForTest -Path $mdIconRoot -Name '') -Message '卸载不改变图标'

    # 冲突默认取消；明确允许后替换，并保存冲突原值。
    Remove-TestRegistryRoot -Path $registryRoot
    $testRootCreated = $false
    if ([IO.File]::Exists($backupPath)) {
        [IO.File]::Delete($backupPath)
    }
    $markdownRoot = Join-RegistryPath -Parent $classesRoot -Child '.markdown'
    $markdownMapping = Join-RegistryPath -Parent $markdownRoot -Child ('shellex\' + $previewHandlerCategory)
    $foreignOriginal = '{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}'
    Set-RegistryValueForTest -Path $markdownMapping -Name '' -Value $foreignOriginal
    $testRootCreated = $true
    $cancelOutput = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true)
    Assert-Equal -Expected $foreignOriginal -Actual (Get-RegistryValueForTest -Path $markdownMapping -Name '') -Message '冲突时默认取消且不覆盖'
    Assert-False -Condition ([IO.File]::Exists($backupPath)) -Message '冲突取消时不生成备份'

    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true -AllowReplacement $true)
    Assert-Equal -Expected $handlerClsid -Actual (Get-RegistryValueForTest -Path $markdownMapping -Name '') -Message '显式允许后替换冲突映射'
    $backup = (Get-Content -LiteralPath $backupPath -Encoding UTF8 -Raw | ConvertFrom-Json)
    $markdownCapture = @($backup.values | Where-Object { $_.id -eq 'mapping.markdown' })[0]
    Assert-True -Condition ([bool]$markdownCapture.existsBefore) -Message '冲突原值存在性已保存'
    Assert-Equal -Expected $foreignOriginal -Actual $markdownCapture.valueBefore -Message '冲突原值已保存'

    # 卸载时如果当前映射变成另一个新选择，则保留；恢复到本项目值后才恢复备份原值。
    $foreignNew = '{99999999-8888-7777-6666-555555555555}'
    Set-RegistryValueForTest -Path $markdownMapping -Name '' -Value $foreignNew
    $null = Invoke-Tool -Path $unregisterScript -Parameters @{
        RegistryRoot = $registryRoot
        BackupPath = $backupPath
        Apply = $true
        KeepBackup = $true
    }
    Assert-Equal -Expected $foreignNew -Actual (Get-RegistryValueForTest -Path $markdownMapping -Name '') -Message '卸载保留替换后的新选择'
    Set-RegistryValueForTest -Path $markdownMapping -Name '' -Value $handlerClsid
    $null = Invoke-Tool -Path $unregisterScript -Parameters @{
        RegistryRoot = $registryRoot
        BackupPath = $backupPath
        Apply = $true
        KeepBackup = $true
    }
    Assert-Equal -Expected $foreignOriginal -Actual (Get-RegistryValueForTest -Path $markdownMapping -Name '') -Message '映射仍属于本项目时恢复原值'

    # 已有 CLSID 明确关闭低完整性隔离时，即使显式允许替换也必须取消。
    Remove-TestRegistryRoot -Path $registryRoot
    $testRootCreated = $false
    if ([IO.File]::Exists($backupPath)) {
        [IO.File]::Delete($backupPath)
    }
    $lowIlClsidRoot = Join-RegistryPath -Parent $classesRoot -Child ('CLSID\' + $handlerClsid)
    Set-RegistryValueForTest -Path $lowIlClsidRoot -Name 'DisableLowILProcessIsolation' -Value '1'
    $testRootCreated = $true
    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true -AllowReplacement $true)
    Assert-False -Condition ([IO.File]::Exists($backupPath)) -Message '低完整性关闭时不创建备份'
    Assert-False -Condition (Test-Path -LiteralPath $mdMapping) -Message '低完整性关闭时不写入扩展映射'

    # 正常撤销默认清理备份；测试专用 -KeepBackup 仅用于前面的重复恢复场景。
    Remove-TestRegistryRoot -Path $registryRoot
    $testRootCreated = $false
    $null = Invoke-Tool -Path $registerScript -Parameters (Get-ToolParameterSet -Apply $true)
    $testRootCreated = $true
    Assert-True -Condition ([IO.File]::Exists($backupPath)) -Message '清理场景已生成备份'
    $null = Invoke-Tool -Path $unregisterScript -Parameters @{
        RegistryRoot = $registryRoot
        BackupPath = $backupPath
        Apply = $true
    }
    Assert-False -Condition ([IO.File]::Exists($backupPath)) -Message '正常撤销清理注册备份'

    # 机器范围或合并根路径必须被拒绝。
    $guarded = $false
    try {
        $null = & $registerScript -RegistryRoot 'Registry::HKEY_LOCAL_MACHINE' -HandlerPath $handlerPath -BackupPath $backupPath
    }
    catch {
        $guarded = $true
    }
    Assert-True -Condition $guarded -Message '拒绝 HKEY_LOCAL_MACHINE 注册范围'

    Write-Output '注册脚本测试通过：WhatIf、演练、当前用户注册、低完整性标志、备份编码、冲突取消、条件恢复和临时键清理均已验证。'
}
finally {
    if ($testRootCreated -or (Test-Path -LiteralPath $registryRoot)) {
        Remove-TestRegistryRoot -Path $registryRoot
    }
    if ([IO.File]::Exists($backupPath)) {
        [IO.File]::Delete($backupPath)
    }
    if (Test-Path -LiteralPath $backupDirectory) {
        [IO.Directory]::Delete($backupDirectory, $false)
    }
}
