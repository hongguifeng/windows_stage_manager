param([Parameter(Mandatory = $true)][string]$Workspace)

$ErrorActionPreference = "Stop"
$readme = Get-Content -LiteralPath (Join-Path $Workspace "README.md") -Raw -Encoding UTF8
$guide = Get-Content -LiteralPath (Join-Path $Workspace "USER_GUIDE.md") -Raw -Encoding UTF8
$limits = Get-Content -LiteralPath (Join-Path $Workspace "KNOWN_LIMITATIONS.md") -Raw -Encoding UTF8
$versionHeader = Get-Content -LiteralPath (Join-Path $Workspace "src\app\version.h") -Raw -Encoding UTF8

if ($readme -notmatch 'dry_run=false') { throw "README lost the active default" }
if ($guide -notmatch 'dry_run=false') { throw "guide lost the active default" }
if ($readme -notmatch 'stage_release\.ps1') { throw "README release command is missing" }
if ($guide -notmatch 'preferred_exposed_edges=2') { throw "preferred edge behavior is missing" }
if ($guide -notmatch 'edge_goal_degraded') { throw "edge degradation diagnostics are missing" }
if (-not $guide.Contains('left+top > right+bottom > top-only > unranked')) { throw "edge preference order is missing" }
if ($guide -notmatch 'Ctrl\+Alt\+F12') { throw "emergency disable instructions are missing" }
if ($guide -notmatch 'rollback\.json') { throw "rollback instructions are missing" }
if ($limits -notmatch 'UAC') { throw "privilege limitation is missing" }
if ($limits -notmatch 'owned window') { throw "window classification limitations are missing" }
if ($limits -notmatch 'scenario_runner') { throw "benchmark limitation is missing" }

$versionMatch = [regex]::Match($versionHeader, 'kVersion\s*=\s*"([^"]+)"')
if (-not $versionMatch.Success) { throw "application version is missing" }
$documentedVersion = [regex]::Escape("-Version " + $versionMatch.Groups[1].Value)
if ($readme -notmatch $documentedVersion) { throw "README release version does not match the application" }
