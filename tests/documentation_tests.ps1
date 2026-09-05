param([Parameter(Mandatory = $true)][string]$Workspace)

$ErrorActionPreference = "Stop"
$readme = Get-Content -LiteralPath (Join-Path $Workspace "README.md") -Raw -Encoding UTF8
$guide = Get-Content -LiteralPath (Join-Path $Workspace "USER_GUIDE.md") -Raw -Encoding UTF8
$limits = Get-Content -LiteralPath (Join-Path $Workspace "KNOWN_LIMITATIONS.md") -Raw -Encoding UTF8
$versionHeader = Get-Content -LiteralPath (Join-Path $Workspace "src\app\version.h") -Raw -Encoding UTF8
$allDocuments = (Get-ChildItem -LiteralPath $Workspace -Filter '*.md' -File |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 }) -join "`n"

if ($readme -notmatch 'dry_run=false') { throw "README lost the active default" }
if ($guide -notmatch 'dry_run=false') { throw "guide lost the active default" }
if ($readme -notmatch 'stage_release\.ps1') { throw "README release command is missing" }
if ($guide -notmatch 'preferred_exposed_edges=2') { throw "preferred edge behavior is missing" }
if ($guide -notmatch 'edge_goal_degraded') { throw "edge degradation diagnostics are missing" }
if ($guide -notmatch 'activation_centering_used') { throw "activation centering diagnostics are missing" }
if ($guide -match 'left\+top > right\+bottom > top-only > unranked') { throw "obsolete edge preference order remains" }
if ($allDocuments -notmatch 'distanceFromWorkAreaCenter') { throw "center-first repair ranking is missing" }
if ($allDocuments -notmatch 'M12') { throw "center-oriented layout plan is missing" }
if ($guide -notmatch 'z_order_fallback_used') { throw "Z-order fallback diagnostics are missing" }
if ($guide -notmatch 'bottom-first') { throw "bottom-first Z-order policy is missing" }
if ($guide -notmatch 'top-prefix') { throw "top-prefix Z-order policy is missing" }
if ($guide -notmatch 'Ctrl\+Alt\+F12') { throw "emergency disable instructions are missing" }
if ($guide -notmatch 'rollback\.json') { throw "rollback instructions are missing" }
if ($readme -notmatch 'Settings > Run mode') { throw "README tray setting instructions are missing" }
if ($guide -notmatch 'minimum_exposed_edges <= preferred_exposed_edges') { throw "setting dependency behavior is missing" }
if ($allDocuments -notmatch 'FR-11') { throw "tray settings requirement is missing" }
if ($allDocuments -notmatch 'custom current value' -or $allDocuments -notmatch 'setting_changed') { throw "tray settings design is missing" }
if ($allDocuments -notmatch 'M11' -or $allDocuments -notmatch '15') { throw "tray settings plan is missing" }
if ($allDocuments -notmatch 'stage_manager_tray_settings_integration') { throw "tray settings acceptance coverage is missing" }
if ($limits -notmatch 'UAC') { throw "privilege limitation is missing" }
if ($limits -notmatch 'owned window') { throw "window classification limitations are missing" }
if ($limits -notmatch 'scenario_runner') { throw "benchmark limitation is missing" }

$versionMatch = [regex]::Match($versionHeader, 'kVersion\s*=\s*"([^"]+)"')
if (-not $versionMatch.Success) { throw "application version is missing" }
$documentedVersion = [regex]::Escape("-Version " + $versionMatch.Groups[1].Value)
if ($readme -notmatch $documentedVersion) { throw "README release version does not match the application" }
