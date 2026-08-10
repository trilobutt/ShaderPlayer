<#
.SYNOPSIS
    Runs ShaderPlayer under the frame profiler while synthesising mouse input, then
    reports averaged per-section timings.

.DESCRIPTION
    Launches build/ShaderPlayer.exe with SHADERPLAYER_PROFILE=1, waits for it to reach
    its steady state (it restores the last session, so it should be presenting frames),
    sweeps the cursor over the window on a Lissajous path so InputLatency gets real
    samples, then closes it and averages every 5-second block profile.log recorded.
#>
param([int]$Seconds = 20)

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class SPWin32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);

    public struct RECT  { public int Left, Top, Right, Bottom; }
    public struct POINT { public int X, Y; }
}
"@

$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot 'build'
$exePath  = Join-Path $buildDir 'ShaderPlayer.exe'
$logPath  = Join-Path $buildDir 'profile.log'

if (-not (Test-Path $exePath)) {
    Write-Error "ERROR: '$exePath' not found — run tools\build.ps1 first"
    exit 1
}

if (Test-Path $logPath) { Remove-Item $logPath -Force }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName               = $exePath
$psi.WorkingDirectory        = $buildDir
$psi.UseShellExecute         = $false
$psi.EnvironmentVariables['SHADERPLAYER_PROFILE'] = '1'

$proc = [System.Diagnostics.Process]::Start($psi)

Start-Sleep -Seconds 8

if ($proc.HasExited) {
    Write-Error "ShaderPlayer.exe exited during startup with code $($proc.ExitCode)"
    exit 1
}

# MainWindowHandle can still read zero immediately after startup; give it a little
# more room before giving up.
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(5)
while ($hwnd -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline) {
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 200 }
}
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Error "Could not obtain ShaderPlayer's main window handle"
    if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null }
    exit 1
}

[SPWin32]::SetForegroundWindow($hwnd) | Out-Null

$rect = New-Object SPWin32+RECT
[SPWin32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null

$originalPos = New-Object SPWin32+POINT
[SPWin32]::GetCursorPos([ref]$originalPos) | Out-Null

$cx = ($rect.Left + $rect.Right) / 2.0
$cy = ($rect.Top + $rect.Bottom) / 2.0
$rx = ([Math]::Abs($rect.Right - $rect.Left) / 2.0) * 0.8
$ry = ([Math]::Abs($rect.Bottom - $rect.Top) / 2.0) * 0.8

$stepMs = 8
$steps  = [int]([double]$Seconds * 1000.0 / $stepMs)

for ($i = 0; $i -lt $steps; $i++) {
    if ($proc.HasExited) { break }
    $t = $i * $stepMs / 1000.0
    # Lissajous path (differing axis frequencies) so the sweep doesn't retrace itself.
    $x = [int]($cx + $rx * [Math]::Sin(2.0 * $t))
    $y = [int]($cy + $ry * [Math]::Sin(3.0 * $t + 1.0))
    [SPWin32]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds $stepMs
}

[SPWin32]::SetCursorPos($originalPos.X, $originalPos.Y) | Out-Null

if ($proc.HasExited) {
    Write-Error "ShaderPlayer.exe exited during the measurement sweep with code $($proc.ExitCode)"
    exit 1
}

$proc.CloseMainWindow() | Out-Null
if (-not $proc.WaitForExit(5000)) {
    $proc.Kill()
}

if (-not (Test-Path $logPath)) {
    Write-Error "profile.log was not written to '$buildDir' — the profiler never fired"
    exit 1
}

# --- Parse every interval block and average each section across blocks -------------

$sections = @{}   # name -> list of @{ Avg; Worst; Calls }
$lines = Get-Content -Path $logPath

$i = 0
while ($i -lt $lines.Count) {
    if ($lines[$i] -match '^=== ShaderPlayer frame profile:') {
        $i++
        if ($i -lt $lines.Count -and $lines[$i] -match '^name\s') { $i++ }
        while ($i -lt $lines.Count -and $lines[$i] -notmatch '^===' -and $lines[$i].Trim() -ne '') {
            if ($lines[$i] -match '^(\S+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s*$') {
                $name = $Matches[1]
                if (-not $sections.ContainsKey($name)) { $sections[$name] = New-Object System.Collections.Generic.List[object] }
                $sections[$name].Add([PSCustomObject]@{
                    Avg   = [double]$Matches[2]
                    Worst = [double]$Matches[3]
                    Calls = [long]$Matches[4]
                })
            }
            $i++
        }
    } else {
        $i++
    }
}

$order = @('Tick', 'ProcessFrame', 'CheckForChanges', 'VideoUpload', 'Render',
           'MainWindowTick', 'Present', 'EventLoopGap', 'InputLatency')

$results = @{}
foreach ($name in $sections.Keys) {
    $entries = $sections[$name]
    $results[$name] = [PSCustomObject]@{
        Name  = $name
        Avg   = ($entries | Measure-Object -Property Avg -Average).Average
        Worst = ($entries | Measure-Object -Property Worst -Maximum).Maximum
        Calls = ($entries | Measure-Object -Property Calls -Sum).Sum
    }
}

$present = $results['Present']
if (-not $present -or $present.Calls -eq 0) {
    Write-Host "NO PRESENT -- open a video or activate a generative shader, then re-run"
    exit 1
}

"{0,-20}{1,10}{2,12}{3,12}" -f 'name', 'avg_ms', 'worst_ms', 'calls'
foreach ($name in $order) {
    if ($results.ContainsKey($name)) {
        $r = $results[$name]
        "{0,-20}{1,10:N3}{2,12:N3}{3,12}" -f $r.Name, $r.Avg, $r.Worst, $r.Calls
    }
}
