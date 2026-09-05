param([Parameter(Mandatory = $true)][string]$Workspace)

$ErrorActionPreference = "Stop"

function Read-WorkspaceFile([string]$RelativePath) {
    $path = Join-Path $Workspace $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required documentation input is missing: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Assert-Match(
    [string]$Text,
    [string]$Pattern,
    [string]$FailureMessage
) {
    if ($Text -notmatch $Pattern) {
        throw $FailureMessage
    }
}

$readme = Read-WorkspaceFile "README.md"
$agentInstructions = Read-WorkspaceFile "AGENTS.md"
$requiredAgentInstruction = '\u6bcf\u6b21\u66f4\u65b0\u529f\u80fd\u4e4b\u540e\uff0c\u90fd\u8981\u540c\u6b65\u4fee\u6539\u4ee3\u7801\u6587\u6863\u3002'
$functions = Read-WorkspaceFile "docs\SOFTWARE_FEATURES.md"
$design = Read-WorkspaceFile "docs\SOFTWARE_DESIGN.md"
$feature = Read-WorkspaceFile "assets\feature-overview.svg"
$versionHeader = Read-WorkspaceFile "src\app\version.h"
$settingsHeader = Read-WorkspaceFile "src\app\settings.h"
$settingsSource = Read-WorkspaceFile "src\app\settings.cpp"
$coordinatorSource = Read-WorkspaceFile "src\window\mvp_coordinator.cpp"
$lifecycleSource = Read-WorkspaceFile "src\app\app_lifecycle.cpp"
$eventHookSource = Read-WorkspaceFile "src\platform\win32\win_event_hook.cpp"

$versionMatch = [regex]::Match($versionHeader, 'kVersion\s*=\s*"([^"]+)"')
if (-not $versionMatch.Success) {
    throw "application version is missing"
}
$version = $versionMatch.Groups[1].Value
$documentVersion = [regex]::Match($functions, '\u9002\u7528\u7248\u672c\uff1a([^\s]+)')
if (-not $documentVersion.Success -or $documentVersion.Groups[1].Value -ne $version) {
    throw "software feature document version does not match the application"
}
$designVersion = [regex]::Match($design, '\u9002\u7528\u7248\u672c\uff1a([^\s]+)')
if (-not $designVersion.Success -or $designVersion.Groups[1].Value -ne $version) {
    throw "software design document version does not match the application"
}
Assert-Match $readme ([regex]::Escape("-Version $version")) `
    "README release version does not match the application"

Assert-Match $readme 'docs/SOFTWARE_FEATURES\.md' `
    "README does not link to the current software feature document"
Assert-Match $readme 'docs/SOFTWARE_DESIGN\.md' `
    "README does not link to the current software design document"
Assert-Match $readme '\u5f53\u524d\u5b9e\u73b0\u7684\u552f\u4e00\u57fa\u51c6' `
    "README does not identify the authoritative current document"
Assert-Match $agentInstructions $requiredAgentInstruction `
    "AGENTS.md does not require documentation updates after feature changes"
Assert-Match $functions 'As-built' `
    "software feature document is not identified as an as-built document"
Assert-Match $functions '\u552f\u4e00\u57fa\u51c6' `
    "software feature document does not distinguish historical documents"

$expectedDocuments = @('SOFTWARE_DESIGN.md', 'SOFTWARE_FEATURES.md')
$actualDocuments = @(Get-ChildItem -LiteralPath (Join-Path $Workspace 'docs') -Filter '*.md' -File |
    ForEach-Object { $_.Name } |
    Sort-Object)
if (@(Compare-Object $expectedDocuments $actualDocuments).Count -ne 0) {
    throw "docs must contain only the maintained feature and design documents"
}

foreach ($pattern in @(
    'As-built Design',
    'stage_manager_core',
    'AppLifecycle::coordinator_loop',
    'WinEventHook',
    'EventQueue',
    'WindowKey',
    'TrackingWindowProvider',
    'MvpCoordinator',
    'VisibilityGoal::TopAndSide',
    'VisibilityGoal::AnyRecognizableEdge',
    'requireStableLayout=true',
    'solve_layout_incrementally',
    'solve_layout_prioritized',
    'VerifiedMoveApplier',
    'InternalMoveTracker',
    'planned_reorders',
    'SettingField',
    'SOFTWARE_FEATURES\.md',
    'ctest --preset windows-debug',
    'ctest --preset windows-release'
)) {
    Assert-Match $design $pattern "software design document is missing architecture detail: $pattern"
}

foreach ($pattern in @(
    '\u4e0d\u6539\u53d8\u7a97\u53e3\u5bbd\u5ea6\u6216\u9ad8\u5ea6',
    '\u4e0d\u8c03\u6574\u7a97\u53e3 Z-order',
    'planned_reorders.*\u56fa\u5b9a\u4e3a `0`',
    '\u5de6\u4e0a / \u53f3\u4e0a\uff08\u540c\u7ea7\uff09',
    '\u4ec5\u9876\u90e8 > \u4ec5\u5de6\u4fa7 > \u4ec5\u53f3\u4fa7 > \u4ec5\u5e95\u90e8',
    '\u6309\u4e0a\u5c42\u7a97\u53e3\u4f18\u5148\u9010\u7a97\u4fee\u590d',
    '\u5df2\u7ecf\u6ee1\u8db3\u7684\u4e0a\u5c42\u7a97\u53e3\u4e0d\u4f1a\u4e3a\u4e86\u66f4\u4e0b\u5c42\u7a97\u53e3\u8ba9\u4f4d',
    '\u4e2d\u5fc3\u8ddd\u79bb\u4f1a\u5206\u522b\u9664\u4ee5\u5de5\u4f5c\u533a\u5bbd\u5ea6\u548c\u9ad8\u5ea6\u8fdb\u884c\u5f52\u4e00\u5316',
    '\u5171\u4eab\u7684\u89d2\u90e8\u4f1a\u4ece\u4e24\u4e2a\u533a\u57df\u4e2d\u6263\u9664',
    '\u771f\u5b9e\u62d6\u52a8\u540e\uff0c\u6d3b\u52a8\u7a97\u53e3\u6700\u7ec8\u4f4d\u7f6e\u4f18\u5148',
    'WH_MOUSE_LL',
    '\u91ca\u653e\u540e 1500 ms \u5185',
    'Ctrl\+Alt\+F12',
    '\u7a97\u53e3\u5e03\u5c40\u6682\u65f6\u65e0\u89e3',
    'dry_run=false',
    'stage_manager_tray_settings_integration',
    'UAC',
    'owned window',
    'scenario_runner'
)) {
    Assert-Match $functions $pattern "software feature document is missing current behavior: $pattern"
}

# Every setting persisted by the application must be discoverable in the
# authoritative feature document. This automatically catches newly added keys.
$persistedKeys = [regex]::Matches($settingsSource, '<<\s*"([a-z0-9_]+)=') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique
if ($persistedKeys.Count -eq 0) {
    throw "no persisted settings were discovered"
}
foreach ($key in $persistedKeys) {
    Assert-Match $functions ([regex]::Escape("``$key``")) `
        "software feature document is missing setting: $key"
}

# Protect the release defaults that materially change out-of-box behavior.
foreach ($sourcePattern in @(
    'bool enabled = true;',
    'bool dryRun = false;',
    'bool placeActivatedWindow = true;',
    'ActivationHorizontalAlignment::Center',
    'ActivationVerticalAlignment::Bottom',
    'AffordancePreset::Balanced',
    'std::uint32_t maxManagedWindows = 20;',
    'std::uint32_t maxConsecutiveFailures = 3;'
)) {
    Assert-Match $settingsHeader ([regex]::Escape($sourcePattern)) `
        "settings defaults changed; review the software feature document: $sourcePattern"
}
foreach ($documentPattern in @(
    '\| `enabled` \| `true` \|',
    '\| `dry_run` \| `false` \|',
    '\| `place_activated_window` \| `true` \|',
    '\| `activation_horizontal_alignment` \| `1` \|',
    '\| `activation_vertical_alignment` \| `2` \|',
    '\| `affordance_preset` \| `1` \|',
    '\| `max_managed_windows` \| 20 \|',
    '\| `max_consecutive_failures` \| 3 \|'
)) {
    Assert-Match $functions $documentPattern `
        "software feature document has lost an important default: $documentPattern"
}

# These source assertions keep the central policy statements tied to executable
# behavior instead of allowing the document and test to drift together.
Assert-Match $lifecycleSource 'planned_reorders = std::string\{"0"\}' `
    "runtime no longer guarantees zero planned Z-order changes"
Assert-Match $coordinatorSource 'solve_layout_prioritized' `
    "upper-window-priority partial solver is no longer wired into the coordinator"
Assert-Match $coordinatorSource 'affordanceGoalDegraded = true' `
    "two-edge to one-edge degradation is no longer wired into the coordinator"
Assert-Match $lifecycleSource 'RegisterHotKey\(' `
    "emergency hotkey is no longer registered"
Assert-Match $eventHookSource 'WH_MOUSE_LL' `
    "delayed right-click activation tracking is no longer installed"

Assert-Match $readme 'dry_run=false' "README lost the active default"
Assert-Match $readme 'activation_horizontal_alignment=1' `
    "README lost the default horizontal activation alignment"
Assert-Match $readme 'activation_vertical_alignment=2' `
    "README lost the default vertical activation alignment"
Assert-Match $readme 'stage_release\.ps1' "README release command is missing"
Assert-Match $readme 'assets/feature-overview\.svg' `
    "README feature illustration is missing"

if ($feature -notmatch '<svg' -or
    $feature -notmatch 'viewBox="0 0 1400 780"' -or
    $feature -notmatch '<title') {
    throw "README feature illustration is invalid"
}
foreach ($asset in @(
    'assets\feature-overview.svg',
    'assets\windows-stage-manager.svg',
    'assets\windows-stage-manager.ico'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $Workspace $asset) -PathType Leaf)) {
        throw "required product asset is missing: $asset"
    }
}

$obsoletePatterns = @(
    'center_activated_window',
    'min_exposed_edge_dip',
    'min_exposed_depth_dip',
    'preferred_exposed_edges',
    'minimum_exposed_edges',
    'repair_target_edge_dip',
    'activation_centering_used',
    'CenterActivatedWindow',
    'MinExposedEdge',
    'MinExposedDepth',
    'RepairTargetEdge'
)
foreach ($pattern in $obsoletePatterns) {
    if ($functions -match [regex]::Escape($pattern)) {
        throw "obsolete setting or diagnostic remains in the current document: $pattern"
    }
}
