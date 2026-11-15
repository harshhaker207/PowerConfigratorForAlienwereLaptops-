# Phase 0 baseline capture for PowerConfigurator UI audit
# - launches the app, waits for the window, screenshots it, measures RAM/CPU
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$exe = 'C:\Users\harsh\PowerConfigurator\PowerConfigurator.exe'
$docs = 'C:\Users\harsh\PowerConfigurator\docs\ui-baseline'
New-Item -ItemType Directory -Force -Path $docs | Out-Null

Write-Host "DPI of primary screen (this session):"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DpiHelper {
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

Write-Host ("GetDpiForSystem: " + [DpiHelper]::GetDpiForSystem())

# Launch app
$proc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 6

# Find window
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Win32Find {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$target = [IntPtr]::Zero
$cb = {
    param($hWnd, $lp)
    $sb = New-Object System.Text.StringBuilder 256
    [Win32Find]::GetWindowText($hWnd, $sb, 256) | Out-Null
    if ($sb.ToString() -like '*Power Configurator*' -and [Win32Find]::IsWindowVisible($hWnd)) {
        $script:target = $hWnd
        return $false
    }
    return $true
}
[Win32Find]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

if ($target -eq [IntPtr]::Zero) {
    Write-Host "Window not found; dumping process list:"
    Get-Process | Where-Object { $_.ProcessName -like '*Power*' } | Format-Table Id, ProcessName, MainWindowTitle
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

$rect = New-Object Win32Find+RECT
[Win32Find]::GetWindowRect($target, [ref]$rect) | Out-Null
Write-Host ("Window rect: L={0} T={1} R={2} B={3} (w={4} h={5})" -f $rect.Left, $rect.Top, $rect.Right, $rect.Bottom, ($rect.Right-$rect.Left), ($rect.Bottom-$rect.Top))

$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$snap = Join-Path $docs 'baseline-100dpi.png'
$bmp.Save($snap, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "Screenshot saved: $snap"

# Metrics: RAM + CPU over 10 seconds
$proc.Refresh()
$ramMB = $proc.WorkingSet64 / 1MB
$cpuSample1 = $proc.TotalProcessorTime.TotalMilliseconds
Start-Sleep -Seconds 10
$proc.Refresh()
$cpuSample2 = $proc.TotalProcessorTime.TotalMilliseconds
$ramMB2 = $proc.WorkingSet64 / 1MB
$cores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$cpuPct = (($cpuSample2 - $cpuSample1) / 1000.0) / 10.0 / $cores * 100.0

$metrics = @"
PowerConfigurator Phase 0 baseline ($((Get-Date).ToString('yyyy-MM-dd HH:mm:ss')))
Binary size: $((Get-Item $exe).Length) bytes
RAM (WorkingSet): $([math]::Round($ramMB2,1)) MB
CPU over 10s sample: $([math]::Round($cpuPct,3))% (of all $cores logical cores)
Window size: $w x $h px
"@
$metrics | Out-File -FilePath (Join-Path $docs 'metrics.txt') -Encoding utf8
Write-Host $metrics

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Write-Host "Done."
