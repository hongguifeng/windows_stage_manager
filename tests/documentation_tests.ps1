param([Parameter(Mandatory = $true)][string]$Workspace)

$ErrorActionPreference = "Stop"

function Read-WorkspaceFile([string]$RelativePath) {
    $path = Join-Path $Workspace $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $path = Join-Path (Join-Path $Workspace 'docs') $RelativePath
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

$readme = Read-WorkspaceFile "README.md"
$guide = Read-WorkspaceFile "USER_GUIDE.md"
$limits = Read-WorkspaceFile "KNOWN_LIMITATIONS.md"
$todo = Read-WorkspaceFile "TODO.md"
$feature = Read-WorkspaceFile "assets\feature-overview.svg"
$versionHeader = Read-WorkspaceFile "src\app\version.h"

$markdownRoots = @($Workspace)
$docsRoot = Join-Path $Workspace 'docs'
if (Test-Path -LiteralPath $docsRoot -PathType Container) {
    $markdownRoots += $docsRoot
}
$markdownDocuments = Get-ChildItem -LiteralPath $markdownRoots -Filter '*.md' -File | ForEach-Object {
    [PSCustomObject]@{
        Path = $_.FullName
        Text = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
    }
}
function Find-SingleMarkdown([scriptblock]$Predicate, [string]$Description) {
    $matches = @($markdownDocuments | Where-Object $Predicate)
    if ($matches.Count -ne 1) {
        throw "expected one $Description document, found $($matches.Count)"
    }
    return $matches[0].Text
}

$requirements = Find-SingleMarkdown {
    $_.Text -match 'TopAndSide' -and
    $_.Text -match 'AnyRecognizableEdge' -and
    $_.Text -notmatch 'EdgeAffordanceRule'
} 'requirements'
$design = Find-SingleMarkdown {
    $_.Text -match 'EdgeAffordanceRule' -and
    $_.Text -match 'TopLeftChannel' -and
    $_.Text -notmatch 'M16-01'
} 'design'
$plan = Find-SingleMarkdown {
    $_.Text -match 'M16-01' -and
    $_.Text -match 'M17-02' -and
    $_.Text -match 'EdgeAffordanceRule'
} 'development plan'
$acceptance = Find-SingleMarkdown { $_.Text -match 'MVP-A23' } 'acceptance'
$maintainedDocuments = @(
    $readme,
    $guide,
    $limits,
    $requirements,
    $design,
    $plan,
    $todo,
    $acceptance
) -join "`n"

if ($readme -notmatch 'dry_run=false') { throw "README lost the active default" }
if ($readme -notmatch 'activation_horizontal_alignment=1' -or
    $readme -notmatch 'activation_vertical_alignment=2') {
    throw "README lost configurable activation alignment defaults"
}
if ($readme -notmatch '\u6c34\u5e73\u53ef\u9009\u9760\u5de6\u3001\u5c45\u4e2d\u3001\u9760\u53f3' -or
    $readme -notmatch '\u7ad6\u76f4\u53ef\u9009\u9760\u4e0a\u3001\u5c45\u4e2d\u3001\u9760\u4e0b') {
    throw "README lost activation alignment choices"
}
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
if ($guide -notmatch '\u5b9e\u9645\u6807\u9898\u680f\u9ad8\u5ea6') { throw "title bar affordance behavior is missing" }
if ($guide -notmatch '\u5de6\u4e0a\u6216\u53f3\u4e0a' -or $guide -notmatch '\u901a\u9053\u8d1f\u8f7d') { throw "balanced top channel behavior is missing" }
if ($guide -notmatch '\u5173\u95ed\u8be5\u5f00\u5173\u65f6\u4ecd\u4f1a\u4fee\u590d\u88ab\u906e\u6321\u7a97\u53e3') { throw "disabled placement behavior is missing" }
if ($readme -notmatch '\u4e0d\u8c03\u6574\u7a97\u53e3\u5c3a\u5bf8\u6216 Z-order' -or
    $readme -notmatch '\u4f18\u5148\u4fdd\u8bc1\u6700\u8fd1\u4f7f\u7528\u7684\u4e0a\u5c42\u7a97\u53e3') {
    throw "README lost immutable Z-order or upper-window priority policy"
}
if ($guide -notmatch 'Ctrl\+Alt\+F12') { throw "emergency disable instructions are missing" }
if ($guide -notmatch 'rollback\.json') { throw "rollback instructions are missing" }
if ($guide -notmatch '\u81ea\u5b9a\u4e49\u2026') { throw "custom numeric setting instructions are missing" }
if ($guide -notmatch '\u6700\u5c0f\u957f\u5ea6\u8d85\u8fc7\u5f53\u524d\u6700\u5927\u957f\u5ea6') { throw "setting dependency behavior is missing" }

if ($requirements -notmatch 'TopAndSide' -or
    $requirements -notmatch 'AnyRecognizableEdge') {
    throw "requirements lost the two-level affordance goal"
}
if ($requirements -notmatch '\u5de6\u4e0a\u4e0e\u53f3\u4e0a' -or $requirements -notmatch '\u5f52\u4e00\u5316\u8ddd\u79bb') {
    throw "requirements lost channel balancing or normalized ranking"
}
if ($requirements -notmatch '\u5e95\u8fb9\u5bf9\u9f50' -or $requirements -notmatch '\u975e\u6d3b\u52a8\s*\u2192\s*\u6d3b\u52a8') {
    throw "requirements lost activation placement semantics"
}

foreach ($term in @(
    'EdgeAffordanceRule',
    'PixelEdgeAffordance',
    'titleBarHeight',
    'VisibilityGoal::TopAndSide',
    'VisibilityGoal::AnyRecognizableEdge',
    'TopLeftChannel',
    'TopRightChannel',
    'channelImbalance',
    'centerDistance',
    'placeActivatedWindow'
)) {
    if ($design -notmatch [regex]::Escape($term)) {
        throw "design is missing current algorithm term: $term"
    }
}

if ($plan -notmatch 'M16-01' -or $plan -notmatch 'M16-05') {
    throw "M16 implementation plan is missing"
}
foreach ($commit in @('15e7429', '45db15e', 'f94b87d', '835d319')) {
    if ($plan -notmatch $commit -or $todo -notmatch $commit) {
        throw "M16 commit trace is missing: $commit"
    }
}
if ($todo -notmatch 'M16-01' -or $todo -notmatch 'M16-05') {
    throw "M16 TODO section is missing"
}
if ($todo -notmatch 'M17-01' -or
    $todo -notmatch 'd19a771' -or
    $todo -notmatch '3E0FB61F0FCCA37DB9030D40CF8FB626B66A8BA3DF93A4DFC55F8315ACEF374C') {
    throw "RC15 release trace is missing"
}
if ($acceptance -notmatch 'MVP-A23' -or
    $acceptance -notmatch 'activation_placement_used=true' -or
    $acceptance -notmatch '\u6c34\u5e73\u5c45\u4e2d\u5e76\u4e0e\u5f53\u524d\u663e\u793a\u5668\u5de5\u4f5c\u533a\u5e95\u90e8\u5bf9\u9f50') {
    throw "current acceptance scenarios are missing"
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
    if ($maintainedDocuments -match [regex]::Escape($pattern)) {
        throw "obsolete layout setting or diagnostic remains: $pattern"
    }
}

if ($maintainedDocuments -notmatch 'stage_manager_tray_settings_integration') { throw "tray settings acceptance coverage is missing" }
if ($limits -notmatch 'UAC') { throw "privilege limitation is missing" }
if ($limits -notmatch 'owned window') { throw "window classification limitations are missing" }
if ($limits -notmatch 'scenario_runner') { throw "benchmark limitation is missing" }

$versionMatch = [regex]::Match($versionHeader, 'kVersion\s*=\s*"([^"]+)"')
if (-not $versionMatch.Success) { throw "application version is missing" }
if ($versionMatch.Groups[1].Value -ne '0.1.0-rc20') { throw "application version was not advanced to rc20" }
$documentedVersion = [regex]::Escape("-Version " + $versionMatch.Groups[1].Value)
if ($readme -notmatch $documentedVersion) { throw "README release version does not match the application" }
