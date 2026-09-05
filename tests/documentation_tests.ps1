param([Parameter(Mandatory = $true)][string]$Workspace)

$ErrorActionPreference = "Stop"
$readme = Get-Content -LiteralPath (Join-Path $Workspace "README.md") -Raw -Encoding UTF8
$guide = Get-Content -LiteralPath (Join-Path $Workspace "USER_GUIDE.md") -Raw -Encoding UTF8
$limits = Get-Content -LiteralPath (Join-Path $Workspace "KNOWN_LIMITATIONS.md") -Raw -Encoding UTF8
$feature = Get-Content -LiteralPath (Join-Path $Workspace "assets\feature-overview.svg") -Raw -Encoding UTF8
$versionHeader = Get-Content -LiteralPath (Join-Path $Workspace "src\app\version.h") -Raw -Encoding UTF8
$allDocuments = (Get-ChildItem -LiteralPath $Workspace -Filter '*.md' -File |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 }) -join "`n"

if ($readme -notmatch 'dry_run=false') { throw "README lost the active default" }
if ($guide -notmatch 'dry_run=false') { throw "guide lost the active default" }
if ($readme -notmatch 'stage_release\.ps1') { throw "README release command is missing" }
if ($readme -notmatch 'assets/feature-overview\.svg') { throw "README feature illustration is missing" }
if (-not (Test-Path -LiteralPath (Join-Path $Workspace 'assets\feature-overview.svg') -PathType Leaf)) { throw "README feature illustration file is missing" }
if ($feature -notmatch '<svg' -or $feature -notmatch 'viewBox="0 0 1400 780"' -or $feature -notmatch '<title') { throw "README feature illustration is invalid" }
if (-not (Test-Path -LiteralPath (Join-Path $Workspace 'assets\windows-stage-manager.svg') -PathType Leaf)) { throw "application icon source is missing" }
if (-not (Test-Path -LiteralPath (Join-Path $Workspace 'assets\windows-stage-manager.ico') -PathType Leaf)) { throw "application icon file is missing" }
if ($guide -notmatch 'affordance_preset=1') { throw "balanced affordance preset is missing" }
if ($guide -notmatch 'place_activated_window=true') { throw "activation placement default is missing" }
if ($guide -notmatch 'affordance_goal_degraded') { throw "affordance degradation diagnostics are missing" }
if ($guide -notmatch 'activation_placement_used') { throw "activation placement diagnostics are missing" }
if ($guide -match 'left\+top > right\+bottom > top-only > unranked') { throw "obsolete edge preference order remains" }
if ($allDocuments -notmatch 'distanceFromWorkAreaCenter') { throw "center-first repair ranking is missing" }
if ($allDocuments -notmatch 'M12') { throw "center-oriented layout plan is missing" }
if ($guide -notmatch 'z_order_fallback_used') { throw "Z-order fallback diagnostics are missing" }
if ($guide -notmatch 'bottom-first') { throw "bottom-first Z-order policy is missing" }
if ($guide -notmatch 'top-prefix') { throw "top-prefix Z-order policy is missing" }
if ($guide -notmatch 'Ctrl\+Alt\+F12') { throw "emergency disable instructions are missing" }
if ($guide -notmatch 'rollback\.json') { throw "rollback instructions are missing" }
if ($readme -notmatch '\u53c2\u6570\u8bbe\u7f6e > \u8fd0\u884c\u6a21\u5f0f') { throw "README Chinese tray setting instructions are missing" }
if ($guide -notmatch '\u6700\u5c0f\u957f\u5ea6\u8d85\u8fc7\u5f53\u524d\u6700\u5927\u957f\u5ea6') { throw "setting dependency behavior is missing" }
if ($allDocuments -notmatch 'FR-11') { throw "tray settings requirement is missing" }
if ($allDocuments -notmatch '\u5f53\u524d\u81ea\u5b9a\u4e49\u503c' -or $allDocuments -notmatch 'setting_changed') { throw "tray settings design is missing" }
if ($allDocuments -notmatch 'M13') { throw "independent edge and localized settings plan is missing" }
if ($guide -notmatch '\u81ea\u5b9a\u4e49\u2026') { throw "custom numeric setting instructions are missing" }
if ($guide -notmatch '\u6263\u9664\u5171\u4eab\u89d2\u90e8') { throw "independent edge behavior is missing" }
if ($guide -notmatch '\u5173\u95ed\u8be5\u5f00\u5173\u65f6\u4ecd\u4f1a\u4fee\u590d\u88ab\u906e\u6321\u7a97\u53e3') { throw "disabled placement behavior is missing" }
if ($guide -notmatch '\u5b9e\u9645\u6807\u9898\u680f\u9ad8\u5ea6') { throw "title bar affordance behavior is missing" }
if ($guide -notmatch '\u5de6\u4e0a\u6216\u53f3\u4e0a' -or $guide -notmatch '\u901a\u9053\u8d1f\u8f7d') { throw "balanced top channel behavior is missing" }
if ($allDocuments -notmatch 'stage_manager_tray_settings_integration') { throw "tray settings acceptance coverage is missing" }
if ($limits -notmatch 'UAC') { throw "privilege limitation is missing" }
if ($limits -notmatch 'owned window') { throw "window classification limitations are missing" }
if ($limits -notmatch 'scenario_runner') { throw "benchmark limitation is missing" }

$versionMatch = [regex]::Match($versionHeader, 'kVersion\s*=\s*"([^"]+)"')
if (-not $versionMatch.Success) { throw "application version is missing" }
$documentedVersion = [regex]::Escape("-Version " + $versionMatch.Groups[1].Value)
if ($readme -notmatch $documentedVersion) { throw "README release version does not match the application" }
