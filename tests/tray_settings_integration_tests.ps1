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

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(
        IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

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
$process = $null
$window = [IntPtr]::Zero

try {
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($settingsPath)) | Out-Null
    [IO.File]::WriteAllText($settingsPath, "enabled=false`r`ndry_run=false`r`n")
    $env:LOCALAPPDATA = $testRoot
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
finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    if ([IO.Directory]::Exists($testRoot)) {
        [IO.Directory]::Delete($testRoot, $true)
    }
}
