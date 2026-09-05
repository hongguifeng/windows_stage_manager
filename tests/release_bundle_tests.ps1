param(
    [Parameter(Mandatory = $true)][string]$ScriptPath,
    [Parameter(Mandatory = $true)][string]$BuildDirectory
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class StageManagerResourceMethods
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr LoadImage(
        IntPtr instance, IntPtr name, uint type, int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr icon);

    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
}
'@

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("windows-stage-manager-release-test-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    & $ScriptPath -BuildDirectory $BuildDirectory -OutputDirectory $testRoot -Version "test-1" -Commit "1111111" | Out-Null
    & $ScriptPath -BuildDirectory $BuildDirectory -OutputDirectory $testRoot -Version "test-2" -Commit "2222222" | Out-Null

    $current = Get-Content -LiteralPath (Join-Path $testRoot "current.json") -Raw | ConvertFrom-Json
    $rollback = Get-Content -LiteralPath (Join-Path $testRoot "rollback.json") -Raw | ConvertFrom-Json
    if ($current.version -ne "test-2" -or $current.commit -ne "2222222") { throw "current release mismatch" }
    if ($rollback.version -ne "test-1" -or $rollback.commit -ne "1111111") { throw "rollback release mismatch" }

    $expanded = Join-Path $testRoot "expanded"
    Expand-Archive -LiteralPath (Join-Path $testRoot $current.package) -DestinationPath $expanded
    $expandedExecutable = Join-Path $expanded "stage_manager.exe"
    if (-not (Test-Path -LiteralPath $expandedExecutable -PathType Leaf)) { throw "executable missing" }
    if (-not (Test-Path -LiteralPath (Join-Path $expanded "assets\feature-overview.svg") -PathType Leaf)) { throw "README illustration missing" }
    if (-not (Test-Path -LiteralPath (Join-Path $expanded "assets\windows-stage-manager.svg") -PathType Leaf)) { throw "icon source missing" }
    if (-not (Test-Path -LiteralPath (Join-Path $expanded "assets\windows-stage-manager.ico") -PathType Leaf)) { throw "icon file missing" }
    $module = [StageManagerResourceMethods]::LoadLibraryEx($expandedExecutable, [IntPtr]::Zero, 2)
    if ($module -eq [IntPtr]::Zero) { throw "executable resources could not be loaded" }
    try {
        $icon = [StageManagerResourceMethods]::LoadImage(
            $module, [IntPtr]102, 1, 32, 32, 0)
        if ($icon -eq [IntPtr]::Zero) { throw "application icon resource missing" }
        [StageManagerResourceMethods]::DestroyIcon($icon) | Out-Null
    } finally {
        [StageManagerResourceMethods]::FreeLibrary($module) | Out-Null
    }
    $settings = Get-Content -LiteralPath (Join-Path $expanded "settings.example.ini") -Raw
    if ($settings -notmatch '(?m)^dry_run=false$') { throw "release default is not active" }
    if ($settings -notmatch '(?m)^place_activated_window=true$') { throw "activation placement default is missing" }
    if ($settings -notmatch '(?m)^affordance_preset=1$') { throw "balanced affordance preset is missing" }
    if ($settings -notmatch '(?m)^top_depth_dip=32$') { throw "top affordance is missing" }
    if ($settings -notmatch '(?m)^right_depth_dip=64$') { throw "right affordance is missing" }
    $manifest = Get-Content -LiteralPath (Join-Path $expanded "manifest.json") -Raw | ConvertFrom-Json
    if ($manifest.dryRunDefault -ne $false) { throw "release manifest default mismatch" }
    if ($manifest.placeActivatedWindowDefault -ne $true) { throw "activation placement manifest default mismatch" }
    if ($manifest.affordancePresetDefault -ne "balanced") { throw "affordance preset manifest mismatch" }
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
