[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function Get-RegistryKeySnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return [ordered]@{
            path = $Path
            exists = $false
            values = @()
        }
    }

    $key = Get-Item -LiteralPath $Path
    $values = foreach ($name in $key.GetValueNames()) {
        [ordered]@{
            name = $name
            kind = $key.GetValueKind($name).ToString()
            value = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        }
    }

    return [ordered]@{
        path = $Path
        exists = $true
        values = @($values)
    }
}

function Get-ProductProcesses {
    $processNames = @('explorer', 'prevhost', 'LocalMarkdownPreview')
    $results = foreach ($name in $processNames) {
        foreach ($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            $path = $null
            try {
                $path = $process.Path
            }
            catch {
                $path = $null
            }

            [ordered]@{
                name = $process.ProcessName
                id = $process.Id
                path = $path
            }
        }
    }

    return @($results)
}

$previewHandlerGuid = '{8895b1c6-b41f-4c1c-a562-0d564250836f}'
$handlerClsid = '{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}'
$handlerAppId = '{E328A06A-6CEF-4BE8-9D6B-36F1DD16B8E5}'

$registryPaths = @(
    'Registry::HKEY_CURRENT_USER\Software\Classes\.md',
    "Registry::HKEY_CURRENT_USER\Software\Classes\.md\shellex\$previewHandlerGuid",
    'Registry::HKEY_CURRENT_USER\Software\Classes\.markdown',
    "Registry::HKEY_CURRENT_USER\Software\Classes\.markdown\shellex\$previewHandlerGuid",
    'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.md',
    "Registry::HKEY_LOCAL_MACHINE\Software\Classes\.md\shellex\$previewHandlerGuid",
    'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.markdown',
    "Registry::HKEY_LOCAL_MACHINE\Software\Classes\.markdown\shellex\$previewHandlerGuid",
    'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers',
    'Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\PreviewHandlers',
    "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\$handlerClsid",
    "Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\$handlerClsid",
    "Registry::HKEY_CURRENT_USER\Software\Classes\AppID\$handlerAppId",
    "Registry::HKEY_LOCAL_MACHINE\Software\Classes\AppID\$handlerAppId",
    'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run',
    'Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Run'
)

$services = @(
    Get-CimInstance -ClassName Win32_Service -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like '*LocalMarkdownPreview*' -or $_.PathName -like '*LocalMarkdownPreview*' } |
        ForEach-Object {
            [ordered]@{
                name = $_.Name
                state = $_.State
                startMode = $_.StartMode
                path = $_.PathName
            }
        }
)

$scheduledTasks = @()
if (Get-Command -Name Get-ScheduledTask -ErrorAction SilentlyContinue) {
    $scheduledTasks = @(
        Get-ScheduledTask -ErrorAction SilentlyContinue |
            Where-Object { $_.TaskName -like '*LocalMarkdownPreview*' -or $_.TaskPath -like '*LocalMarkdownPreview*' } |
            ForEach-Object {
                [ordered]@{
                    name = $_.TaskName
                    path = $_.TaskPath
                    state = $_.State.ToString()
                }
            }
    )
}

$snapshot = [ordered]@{
    schemaVersion = 1
    capturedAt = [DateTimeOffset]::Now.ToString('o')
    machine = [ordered]@{
        name = $env:COMPUTERNAME
        os = (Get-CimInstance -ClassName Win32_OperatingSystem).Caption
        version = [Environment]::OSVersion.Version.ToString()
        architecture = $env:PROCESSOR_ARCHITECTURE
    }
    registry = @($registryPaths | ForEach-Object { Get-RegistryKeySnapshot -Path $_ })
    processes = @(Get-ProductProcesses)
    services = $services
    scheduledTasks = $scheduledTasks
}

$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($resolvedOutputPath)
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$json = $snapshot | ConvertTo-Json -Depth 8
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($resolvedOutputPath, $json + [Environment]::NewLine, $utf8WithoutBom)

Write-Output $resolvedOutputPath
