# AviUtl2 の窓構造を読み取り専用で一覧化する調査スクリプト
# 使い方: AviUtl2 起動中に pwsh -File tools\probe_windows.ps1 > probe.txt
param([int]$MaxDepth = 4)

Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint f);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetLong(IntPtr h, int i);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
}
"@

$proc = Get-Process aviutl2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "aviutl2 が起動していません"; exit 1 }
"aviutl2 pid=$($proc.Id) path=$($proc.Path)"

function Describe([IntPtr]$h) {
    $c = New-Object System.Text.StringBuilder 256
    $t = New-Object System.Text.StringBuilder 256
    [void][W]::GetClassName($h, $c, 256)
    [void][W]::GetWindowText($h, $t, 256)
    $r = New-Object W+RECT
    [void][W]::GetWindowRect($h, [ref]$r)
    $style = [W]::GetLong($h, -16).ToInt64()
    $ex = [W]::GetLong($h, -20).ToInt64()
    $id = [W]::GetLong($h, -12).ToInt64()
    $vis = if ([W]::IsWindowVisible($h)) { "V" } else { "-" }
    $owner = [W]::GetWindow($h, 4)
    "{0} 0x{1:X} class='{2}' text='{3}' id={4} style=0x{5:X8} ex=0x{6:X8} rect=({7},{8})-({9},{10}) {11}x{12} owner=0x{13:X}" -f `
        $vis, $h.ToInt64(), $c, $t, $id, $style, $ex, $r.L, $r.T, $r.R, $r.B, ($r.R - $r.L), ($r.B - $r.T), $owner.ToInt64()
}

function Walk([IntPtr]$parent, [int]$depth) {
    if ($depth -gt $MaxDepth) { return }
    $children = New-Object System.Collections.Generic.List[IntPtr]
    $cb = [W+EnumProc]{ param($h, $l)
        if ([W]::GetAncestor($h, 1) -eq $parent) { $children.Add($h) }
        return $true }
    [void][W]::EnumChildWindows($parent, $cb, [IntPtr]::Zero)
    foreach ($ch in $children) {
        ("  " * $depth) + (Describe $ch)
        Walk $ch ($depth + 1)
    }
}

$tops = New-Object System.Collections.Generic.List[IntPtr]
$cbTop = [W+EnumProc]{ param($h, $l)
    $p = 0; [void][W]::GetWindowThreadProcessId($h, [ref]$p)
    if ($p -eq $proc.Id) { $tops.Add($h) }
    return $true }
[void][W]::EnumWindows($cbTop, [IntPtr]::Zero)

foreach ($t in $tops) {
    "=== TOP " + (Describe $t)
    Walk $t 1
}
