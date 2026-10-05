# AviUtl2 メインウィンドウのメニュー構造を読み取り専用で一覧化する調査スクリプト
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class M {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr m);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr m, int pos);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr m, int pos);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr m, uint id, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr m, uint id, StringBuilder s, int n, uint flags);
}
"@

$proc = Get-Process aviutl2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "aviutl2 が起動していません"; exit 1 }

$main = [IntPtr]::Zero
$cb = [M+EnumProc]{ param($h, $l)
    $p = 0; [void][M]::GetWindowThreadProcessId($h, [ref]$p)
    if ($p -eq $proc.Id) {
        $c = New-Object System.Text.StringBuilder 64
        [void][M]::GetClassName($h, $c, 64)
        if ($c.ToString() -eq 'aviutl2Manager' -and [M]::GetWindow($h, 4) -eq [IntPtr]::Zero) { $script:main = $h; return $false }
    }
    return $true }
[void][M]::EnumWindows($cb, [IntPtr]::Zero)
"main=0x{0:X}" -f $main.ToInt64()

function Dump([IntPtr]$menu, [int]$depth) {
    $n = [M]::GetMenuItemCount($menu)
    for ($i = 0; $i -lt $n; $i++) {
        $s = New-Object System.Text.StringBuilder 256
        [void][M]::GetMenuString($menu, [uint32]$i, $s, 256, 0x400)
        $id = [M]::GetMenuItemID($menu, $i)
        $state = [M]::GetMenuState($menu, [uint32]$i, 0x400)
        $checked = if ($state -band 0x8) { "[x]" } else { "   " }
        ("  " * $depth) + ("{0} id={1} '{2}'" -f $checked, $(if ($id -eq 0xFFFFFFFF) { "sub" } else { $id }), $s)
        $sub = [M]::GetSubMenu($menu, $i)
        if ($sub -ne [IntPtr]::Zero) { Dump $sub ($depth + 1) }
    }
}

$menu = [M]::GetMenu($main)
"menu=0x{0:X}" -f $menu.ToInt64()
if ($menu -ne [IntPtr]::Zero) { Dump $menu 0 }
