[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$textExtensions = @(
    '.bat', '.c', '.cmd', '.cpp', '.cs', '.csproj', '.def', '.editorconfig',
    '.filters', '.gitattributes', '.gitignore', '.h', '.hpp', '.iss', '.json',
    '.md', '.props', '.ps1', '.rc', '.sln', '.txt', '.vcxproj', '.xml', '.yml', '.yaml'
)

$excludedDirectories = @('.git', 'artifacts', 'build', 'bin', 'obj', 'TestResults')
$utf8Strict = [System.Text.UTF8Encoding]::new($false, $true)
$problems = [System.Collections.Generic.List[object]]::new()
$checkedCount = 0

$files = Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Where-Object {
    $relative = $_.FullName.Substring([System.IO.Path]::GetFullPath($Root).TrimEnd('\').Length).TrimStart('\')
    $segments = $relative -split '[\\/]'
    $excluded = @($segments | Where-Object { $excludedDirectories -contains $_ }).Count -gt 0
    -not $excluded -and ($textExtensions -contains $_.Extension -or $textExtensions -contains $_.Name)
}

foreach ($file in $files) {
    $checkedCount++
    $relativePath = $file.FullName.Substring([System.IO.Path]::GetFullPath($Root).TrimEnd('\').Length).TrimStart('\')
    $content = Get-Content -LiteralPath $file.FullName -Encoding UTF8 -Raw
    if ($null -eq $content) {
        $content = ''
    }
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $problems.Add([ordered]@{ file = $relativePath; problem = 'UTF-8 BOM' })
        continue
    }

    try {
        $strictContent = $utf8Strict.GetString($bytes)
    }
    catch [System.Text.DecoderFallbackException] {
        $problems.Add([ordered]@{ file = $relativePath; problem = '不是有效 UTF-8' })
        continue
    }

    if ($content.Contains([char]0xFFFD) -or $strictContent.Contains([char]0xFFFD)) {
        $problems.Add([ordered]@{ file = $relativePath; problem = '包含替换字符 U+FFFD' })
    }
}

$result = [ordered]@{
    root = [System.IO.Path]::GetFullPath($Root)
    checked = $checkedCount
    passed = $problems.Count -eq 0
    problems = @($problems)
}

$result | ConvertTo-Json -Depth 5
if ($problems.Count -gt 0) {
    exit 1
}
