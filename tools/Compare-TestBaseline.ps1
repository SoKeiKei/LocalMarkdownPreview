[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BaselinePath,

    [Parameter(Mandatory = $true)]
    [string]$CurrentPath
)

$ErrorActionPreference = 'Stop'

function Read-Snapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $text = Get-Content -LiteralPath $Path -Encoding UTF8 -Raw
    return $text | ConvertFrom-Json
}

function Convert-ValuesToComparableText {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Values
    )

    $normalized = @(
        $Values |
            Sort-Object -Property name |
            ForEach-Object {
                [ordered]@{
                    name = $_.name
                    kind = $_.kind
                    value = $_.value
                }
            }
    )

    return ($normalized | ConvertTo-Json -Depth 5 -Compress)
}

function Get-RegistryMap {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Entries
    )

    $map = @{}
    foreach ($entry in $Entries) {
        $map[$entry.path] = [ordered]@{
            exists = [bool]$entry.exists
            values = Convert-ValuesToComparableText -Values @($entry.values)
        }
    }

    return $map
}

function Get-NamedItemsText {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Items,

        [Parameter(Mandatory = $true)]
        [string[]]$Properties
    )

    $normalized = foreach ($item in $Items) {
        $record = [ordered]@{}
        foreach ($property in $Properties) {
            $record[$property] = $item.$property
        }
        $record
    }

    return (@($normalized | Sort-Object -Property name, path) | ConvertTo-Json -Depth 4 -Compress)
}

$baseline = Read-Snapshot -Path $BaselinePath
$current = Read-Snapshot -Path $CurrentPath
$changes = [System.Collections.Generic.List[object]]::new()

$baselineRegistry = Get-RegistryMap -Entries @($baseline.registry)
$currentRegistry = Get-RegistryMap -Entries @($current.registry)
$allRegistryPaths = @($baselineRegistry.Keys + $currentRegistry.Keys | Sort-Object -Unique)

foreach ($path in $allRegistryPaths) {
    $before = $baselineRegistry[$path]
    $after = $currentRegistry[$path]
    $beforeText = $before | ConvertTo-Json -Compress
    $afterText = $after | ConvertTo-Json -Compress

    if ($beforeText -ne $afterText) {
        $changes.Add([ordered]@{
            category = 'registry'
            item = $path
            before = $before
            after = $after
        })
    }
}

$comparisons = @(
    [ordered]@{
        category = 'processes'
        before = Get-NamedItemsText -Items @($baseline.processes) -Properties @('name', 'path')
        after = Get-NamedItemsText -Items @($current.processes) -Properties @('name', 'path')
    },
    [ordered]@{
        category = 'services'
        before = Get-NamedItemsText -Items @($baseline.services) -Properties @('name', 'state', 'startMode', 'path')
        after = Get-NamedItemsText -Items @($current.services) -Properties @('name', 'state', 'startMode', 'path')
    },
    [ordered]@{
        category = 'scheduledTasks'
        before = Get-NamedItemsText -Items @($baseline.scheduledTasks) -Properties @('name', 'path', 'state')
        after = Get-NamedItemsText -Items @($current.scheduledTasks) -Properties @('name', 'path', 'state')
    }
)

foreach ($comparison in $comparisons) {
    if ($comparison.before -ne $comparison.after) {
        $changes.Add([ordered]@{
            category = $comparison.category
            item = $comparison.category
            before = $comparison.before
            after = $comparison.after
        })
    }
}

$result = [ordered]@{
    matches = $changes.Count -eq 0
    changeCount = $changes.Count
    changes = @($changes)
}

$result | ConvertTo-Json -Depth 10

if ($changes.Count -gt 0) {
    exit 1
}
