param([Parameter(Mandatory = $true)][string]$Workspace)

$ErrorActionPreference = "Stop"
$readme = Get-Content -LiteralPath (Join-Path $Workspace "README.md") -Raw -Encoding UTF8
$guide = Get-Content -LiteralPath (Join-Path $Workspace "USER_GUIDE.md") -Raw -Encoding UTF8
$limits = Get-Content -LiteralPath (Join-Path $Workspace "KNOWN_LIMITATIONS.md") -Raw -Encoding UTF8

if ($readme -notmatch 'dry_run=true') { throw "README lost the safe DryRun default" }
if ($readme -notmatch 'stage_release\.ps1') { throw "README release command is missing" }
if ($guide -notmatch 'Ctrl\+Alt\+F12') { throw "emergency disable instructions are missing" }
if ($guide -notmatch 'rollback\.json') { throw "rollback instructions are missing" }
if ($limits -notmatch 'UAC') { throw "privilege limitation is missing" }
if ($limits -notmatch 'owned window') { throw "window classification limitations are missing" }
if ($limits -notmatch 'scenario_runner') { throw "benchmark limitation is missing" }
