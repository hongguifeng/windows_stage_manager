param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class StageManagerNativeMethods
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindWindowEx(
        IntPtr parent, IntPtr childAfter, string className, string windowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr window, StringBuilder className, int length);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr window, StringBuilder text, int length);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr dialog, int itemId);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(
        IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool PostMessage(
        IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool SetWindowText(IntPtr window, string text);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr window);

    private delegate bool EnumWindowsProc(IntPtr window, IntPtr lParam);

    public static IntPtr FindMessageWindow(uint expectedProcessId)
    {
        IntPtr messageRoot = new IntPtr(-3);
        IntPtr window = IntPtr.Zero;
        while ((window = FindWindowEx(messageRoot, window, null, null)) != IntPtr.Zero)
        {
            var className = new StringBuilder(256);
            uint processId;
            GetClassName(window, className, className.Capacity);
            GetWindowThreadProcessId(window, out processId);
            if (processId == expectedProcessId &&
                className.ToString() == "WindowsStageManager.MessageWindow")
            {
                return window;
            }
        }
        return IntPtr.Zero;
    }

    public static IntPtr FindDialog(uint expectedProcessId)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr ignored)
        {
            var className = new StringBuilder(256);
            var windowText = new StringBuilder(256);
            uint processId;
            GetClassName(window, className, className.Capacity);
            GetWindowText(window, windowText, windowText.Capacity);
            GetWindowThreadProcessId(window, out processId);
            if (processId == expectedProcessId && IsWindowVisible(window) &&
                className.ToString() == "#32770" &&
                windowText.ToString() == "\u81ea\u5b9a\u4e49\u53c2\u6570")
            {
                found = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindSettingsDialog(uint expectedProcessId)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr ignored)
        {
            var className = new StringBuilder(256);
            var windowText = new StringBuilder(256);
            uint processId;
            GetClassName(window, className, className.Capacity);
            GetWindowText(window, windowText, windowText.Capacity);
            GetWindowThreadProcessId(window, out processId);
            if (processId == expectedProcessId && IsWindowVisible(window) &&
                className.ToString() == "#32770" &&
                windowText.ToString() == "\u7a97\u53e3\u7ba1\u7406\u5668\u8bbe\u7f6e")
            {
                found = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Wait-Until {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$FailureMessage,
        [int]$TimeoutMilliseconds = 10000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (& $Condition) {
            return
        }
        Start-Sleep -Milliseconds 50
    }
    throw $FailureMessage
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("windows-stage-manager-tray-" + [Guid]::NewGuid())
$settingsPath = Join-Path $testRoot 'WindowsStageManager\settings.ini'
$logPath = Join-Path $testRoot 'WindowsStageManager\logs\manager.log'
$previousLocalAppData = $env:LOCALAPPDATA
$previousTestInstanceId = $env:WINDOWS_STAGE_MANAGER_TEST_INSTANCE_ID
$process = $null
$window = [IntPtr]::Zero

try {
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($settingsPath)) | Out-Null
    [IO.File]::WriteAllText($settingsPath, "enabled=false`r`ndry_run=false`r`n")
    $env:LOCALAPPDATA = $testRoot
    $env:WINDOWS_STAGE_MANAGER_TEST_INSTANCE_ID = [Guid]::NewGuid().ToString('N')
    $process = Start-Process -FilePath $Executable -PassThru

    Wait-Until -FailureMessage 'message-only window was not created' -Condition {
        if ($process.HasExited) {
            throw "process exited before creating its message window (exit code $($process.ExitCode))"
        }
        $script:window = [StageManagerNativeMethods]::FindMessageWindow($process.Id)
        $script:window -ne [IntPtr]::Zero
    }

    # WM_COMMAND, DryRun field, choice index 1 (preview only).
    [StageManagerNativeMethods]::SendMessage(
        $script:window, 0x0111, [IntPtr]2001, [IntPtr]::Zero) | Out-Null
    Wait-Until -FailureMessage 'DryRun=true was not persisted' -Condition {
        (Test-Path -LiteralPath $settingsPath) -and
            ((Get-Content -Raw -LiteralPath $settingsPath) -match '(?m)^dry_run=true\r?$')
    }
    Wait-Until -FailureMessage 'setting_changed log was not written' -Condition {
        (Test-Path -LiteralPath $logPath) -and
            ((Get-Content -Raw -LiteralPath $logPath) -match
                'setting_changed.*field="dry_run".*value="1"')
    }
    if ($process.HasExited) {
        throw 'process exited while rebuilding after the setting change'
    }

    # Switch back to the release-safe active default and verify a second live reload.
    [StageManagerNativeMethods]::SendMessage(
        $script:window, 0x0111, [IntPtr]2000, [IntPtr]::Zero) | Out-Null
    Wait-Until -FailureMessage 'DryRun=false was not persisted' -Condition {
        (Get-Content -Raw -LiteralPath $settingsPath) -match '(?m)^dry_run=false\r?$'
    }

    # Select horizontal right and vertical center through the live tray menu.
    # The alignment fields are appended to the stable command model as fields 28 and 29.
    [StageManagerNativeMethods]::SendMessage(
        $script:window, 0x0111, [IntPtr]2450, [IntPtr]::Zero) | Out-Null
    Wait-Until -FailureMessage 'horizontal activation alignment was not persisted' -Condition {
        (Get-Content -Raw -LiteralPath $settingsPath) -match
            '(?m)^activation_horizontal_alignment=2\r?$'
    }
    [StageManagerNativeMethods]::SendMessage(
        $script:window, 0x0111, [IntPtr]2465, [IntPtr]::Zero) | Out-Null
    Wait-Until -FailureMessage 'vertical activation alignment was not persisted' -Condition {
        (Get-Content -Raw -LiteralPath $settingsPath) -match
            '(?m)^activation_vertical_alignment=1\r?$'
    }

    # Open the custom-value dialog for MaximumManagedWindows and enter 7.
    # Command = base 2000 + field 26 * stride 16 + custom slot 15.
    if (-not [StageManagerNativeMethods]::PostMessage(
        $script:window, 0x0111, [IntPtr]2431, [IntPtr]::Zero)) {
        throw 'custom setting command could not be posted'
    }
    $dialog = [IntPtr]::Zero
    Wait-Until -FailureMessage 'custom setting dialog was not created' -Condition {
        $script:dialog = [StageManagerNativeMethods]::FindDialog($process.Id)
        $script:dialog -ne [IntPtr]::Zero
    }
    $edit = [StageManagerNativeMethods]::GetDlgItem($script:dialog, 1002)
    if ($edit -eq [IntPtr]::Zero) {
        throw 'custom setting edit control was not created'
    }
    # Avoid cross-process text buffers here. The initial value 20 has two characters,
    # while the submitted value 7 has one, so WM_GETTEXTLENGTH provides a safe sync point.
    Wait-Until -FailureMessage 'custom setting dialog was not initialized' -Condition {
        [StageManagerNativeMethods]::SendMessage(
            $edit, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 2
    }
    # WM_CHAR '7' exercises the same edit-control path as real keyboard input.
    [StageManagerNativeMethods]::SendMessage(
        $edit, 0x0102, [IntPtr]55, [IntPtr]::Zero) | Out-Null
    Wait-Until -FailureMessage 'custom setting edit did not retain 7' -Condition {
        [StageManagerNativeMethods]::SendMessage(
            $edit, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
    }
    [StageManagerNativeMethods]::SendMessage(
        $script:dialog, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 100
    if ([StageManagerNativeMethods]::IsWindow($script:dialog)) {
        throw 'custom setting dialog did not accept the entered value'
    }
    Wait-Until -FailureMessage 'custom setting was not persisted' -Condition {
        (Get-Content -Raw -LiteralPath $settingsPath) -match
            '(?m)^max_managed_windows=7\r?$'
    }
    Wait-Until -FailureMessage 'custom setting change was not logged' -Condition {
        (Get-Content -Raw -LiteralPath $logPath) -match
            'setting_changed.*field="max_managed_windows".*value="7"'
    }
    if ($process.HasExited) {
        throw 'process exited while rebuilding after the custom setting change'
    }

    # Open the visual settings dialog. Change horizontal and vertical placement,
    # then select the top fallback depth and choose 64 DIP.
    if (-not [StageManagerNativeMethods]::PostMessage(
        $script:window, 0x0111, [IntPtr]1003, [IntPtr]::Zero)) {
        throw 'visual settings command could not be posted'
    }
    $settingsDialog = [IntPtr]::Zero
    Wait-Until -FailureMessage 'visual settings dialog was not created' -Condition {
        $script:settingsDialog = [StageManagerNativeMethods]::FindSettingsDialog($process.Id)
        $script:settingsDialog -ne [IntPtr]::Zero
    }
    $fields = [StageManagerNativeMethods]::GetDlgItem($script:settingsDialog, 1101)
    $valueControl = [StageManagerNativeMethods]::GetDlgItem($script:settingsDialog, 1104)
    $preview = [StageManagerNativeMethods]::GetDlgItem($script:settingsDialog, 1106)
    if ($fields -eq [IntPtr]::Zero -or $valueControl -eq [IntPtr]::Zero -or
        $preview -eq [IntPtr]::Zero) {
        throw 'visual settings dialog is missing required controls'
    }
    # LB_SETCURSEL = 0x0186; LBN_SELCHANGE = 1 in the high word.
    # Horizontal and vertical placement are list positions 2 and 3.
    [StageManagerNativeMethods]::SendMessage(
        $fields, 0x0186, [IntPtr]2, [IntPtr]::Zero) | Out-Null
    [StageManagerNativeMethods]::SendMessage(
        $script:settingsDialog, 0x0111, [IntPtr]66637, [IntPtr]::Zero) | Out-Null
    # Change horizontal placement from right to left.
    [StageManagerNativeMethods]::SendMessage(
        $valueControl, 0x014E, [IntPtr]0, [IntPtr]::Zero) | Out-Null
    [StageManagerNativeMethods]::SendMessage(
        $fields, 0x0186, [IntPtr]3, [IntPtr]::Zero) | Out-Null
    [StageManagerNativeMethods]::SendMessage(
        $script:settingsDialog, 0x0111, [IntPtr]66637, [IntPtr]::Zero) | Out-Null
    # Change vertical placement from center to top.
    [StageManagerNativeMethods]::SendMessage(
        $valueControl, 0x014E, [IntPtr]0, [IntPtr]::Zero) | Out-Null
    # Top fallback depth is list position 7 after the two placement fields.
    [StageManagerNativeMethods]::SendMessage(
        $fields, 0x0186, [IntPtr]7, [IntPtr]::Zero) | Out-Null
    [StageManagerNativeMethods]::SendMessage(
        $script:settingsDialog, 0x0111, [IntPtr]66637, [IntPtr]::Zero) | Out-Null
    # CB_SETCURSEL = 0x014E; 64 DIP is preset index 4 for this field.
    [StageManagerNativeMethods]::SendMessage(
        $valueControl, 0x014E, [IntPtr]4, [IntPtr]::Zero) | Out-Null
    [StageManagerNativeMethods]::SendMessage(
        $script:settingsDialog, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null
    Wait-Until -FailureMessage 'visual settings dialog did not close' -Condition {
        -not [StageManagerNativeMethods]::IsWindow($script:settingsDialog)
    }
    Wait-Until -FailureMessage 'visual settings value was not persisted' -Condition {
        $content = Get-Content -Raw -LiteralPath $settingsPath
        $content -match '(?m)^top_depth_dip=64\r?$' -and
            $content -match '(?m)^affordance_preset=3\r?$' -and
            $content -match '(?m)^activation_horizontal_alignment=0\r?$' -and
            $content -match '(?m)^activation_vertical_alignment=0\r?$'
    }
    Wait-Until -FailureMessage 'visual settings change was not logged' -Condition {
        (Get-Content -Raw -LiteralPath $logPath) -match 'settings_dialog_applied'
    }

    # Tray Exit command.
    [StageManagerNativeMethods]::SendMessage(
        $script:window, 0x0111, [IntPtr]1002, [IntPtr]::Zero) | Out-Null
    if (-not $process.WaitForExit(10000)) {
        throw 'process did not exit after the tray Exit command'
    }
    if ($process.ExitCode -ne 0) {
        throw "process returned exit code $($process.ExitCode)"
    }
}
catch {
    if (Test-Path -LiteralPath $settingsPath) {
        Write-Host 'settings at failure:'
        Get-Content -LiteralPath $settingsPath
    }
    if (Test-Path -LiteralPath $logPath) {
        Write-Host 'log tail at failure:'
        Select-String -LiteralPath $logPath -Pattern 'custom_setting' |
            ForEach-Object { Write-Host $_.Line }
        Get-Content -LiteralPath $logPath -Tail 10
    }
    throw
}
finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    $env:WINDOWS_STAGE_MANAGER_TEST_INSTANCE_ID = $previousTestInstanceId
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    if ([IO.Directory]::Exists($testRoot)) {
        [IO.Directory]::Delete($testRoot, $true)
    }
}
