param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Commit = "unknown"
)

$ErrorActionPreference = "Stop"
if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z._-]*$') {
    throw "Version contains unsupported characters."
}

$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$executable = Join-Path $buildRoot "stage_manager.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "stage_manager.exe was not found in the build directory."
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$packageName = "WindowsStageManager-$Version.zip"
$packagePath = Join-Path $outputRoot $packageName
if (Test-Path -LiteralPath $packagePath) {
    throw "A package with this version already exists."
}

$stagingRoot = Join-Path $outputRoot (".staging-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
try {
    Copy-Item -LiteralPath $executable -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\README.md") -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\README.zh-CN.md") -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\assets") -Destination $stagingRoot -Recurse
    $sampleSettings = @'
enabled=true
dry_run=false
place_activated_window=true
activation_horizontal_alignment=1
activation_vertical_alignment=2
affordance_preset=1
ui_language=1
top_depth_dip=32
left_depth_dip=40
right_depth_dip=64
bottom_depth_dip=64
max_managed_windows=20
max_consecutive_failures=3
'@
    $sampleSettings -split "`r?`n" | Set-Content -LiteralPath (Join-Path $stagingRoot "settings.example.ini") -Encoding utf8
    [ordered]@{
        product = "WindowsStageManager"
        version = $Version
        commit = $Commit
        createdUtc = [DateTime]::UtcNow.ToString("o")
        dryRunDefault = $false
        placeActivatedWindowDefault = $true
        activationHorizontalAlignmentDefault = "center"
        activationVerticalAlignmentDefault = "bottom"
        affordancePresetDefault = "balanced"
        uiLanguageDefault = "english"
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stagingRoot "manifest.json") -Encoding utf8

    $temporaryPackage = "$packagePath.tmp.zip"
    Compress-Archive -Path (Join-Path $stagingRoot "*") -DestinationPath $temporaryPackage
    Move-Item -LiteralPath $temporaryPackage -Destination $packagePath

    $currentPath = Join-Path $outputRoot "current.json"
    $rollbackPath = Join-Path $outputRoot "rollback.json"
    if (Test-Path -LiteralPath $currentPath -PathType Leaf) {
        $previous = Get-Content -LiteralPath $currentPath -Raw | ConvertFrom-Json
        $previousName = [string]$previous.package
        if ([IO.Path]::GetFileName($previousName) -ne $previousName) {
            throw "The current release manifest contains an unsafe package path."
        }
        $previousPackage = Join-Path $outputRoot $previousName
        if (-not (Test-Path -LiteralPath $previousPackage -PathType Leaf)) {
            throw "The current release package is missing; rollback was not changed."
        }
        Copy-Item -LiteralPath $currentPath -Destination $rollbackPath -Force
    }

    $currentTemporary = Join-Path $outputRoot "current.json.tmp"
    [ordered]@{
        package = $packageName
        version = $Version
        commit = $Commit
    } | ConvertTo-Json | Set-Content -LiteralPath $currentTemporary -Encoding utf8
    Move-Item -LiteralPath $currentTemporary -Destination $currentPath -Force
} finally {
    $resolvedStaging = [IO.Path]::GetFullPath($stagingRoot)
    $resolvedOutput = [IO.Path]::GetFullPath($outputRoot) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedStaging.StartsWith($resolvedOutput, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedStaging)) {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
    }
}

Write-Output $packagePath
