param(
    [Parameter(Mandatory = $true)][string]$ScriptPath,
    [Parameter(Mandatory = $true)][string]$BuildDirectory
)

$ErrorActionPreference = "Stop"
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
    if (-not (Test-Path -LiteralPath (Join-Path $expanded "stage_manager.exe") -PathType Leaf)) { throw "executable missing" }
    $settings = Get-Content -LiteralPath (Join-Path $expanded "settings.example.ini") -Raw
    if ($settings -notmatch '(?m)^dry_run=false$') { throw "release default is not active" }
    if ($settings -notmatch '(?m)^preferred_exposed_edges=2$') { throw "preferred edge count is missing" }
    if ($settings -notmatch '(?m)^minimum_exposed_edges=1$') { throw "minimum edge count is missing" }
    $manifest = Get-Content -LiteralPath (Join-Path $expanded "manifest.json") -Raw | ConvertFrom-Json
    if ($manifest.dryRunDefault -ne $false) { throw "release manifest default mismatch" }
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
