# RTR-Bench - captures a bench window to a PNG, for the README and the docs.
#
#   scripts\screenshot.ps1 -Title "RTR-Bench Rack" -Out docs\screenshots\rack.png
#
# The window is found by a substring of its title, brought to the front and
# copied from the screen (so the transparent corners show the desktop behind,
# as the user sees them). The bench must be running.
param(
    [string]$Title = 'RTR-Bench Rack',
    [string]$Out = "$env:TEMP\rtr-bench.png"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Collections.Generic;
public static class RtrShot {
    public delegate bool CB(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(CB cb, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static List<long> Find(string part) {
        var res = new List<long>();
        EnumWindows((h, l) => { var sb = new StringBuilder(256); GetWindowText(h, sb, 256);
            if (IsWindowVisible(h) && sb.ToString().Contains(part)) res.Add((long)h); return true; }, IntPtr.Zero);
        return res;
    }
    public static int[] Rect(long h) { RECT r; GetWindowRect(new IntPtr(h), out r); return new[] { r.L, r.T, r.R, r.B }; }
}
"@

$handles = [RtrShot]::Find($Title)
if ($handles.Count -eq 0) {
    Write-Error "No visible window with '$Title' in its title. Is the bench running?"
}
$h = $handles[0]
[RtrShot]::SetForegroundWindow([IntPtr]::new($h)) | Out-Null
Start-Sleep -Milliseconds 700
$r = [RtrShot]::Rect($h)
$w = $r[2] - $r[0]
$hgt = $r[3] - $r[1]
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r[0], $r[1], 0, 0, [System.Drawing.Size]::new($w, $hgt))
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output $Out
