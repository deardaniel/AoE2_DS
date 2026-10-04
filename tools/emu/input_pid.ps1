param([int]$ProcId, [string]$Action, [int]$X = 0, [int]$Y = 0, [int]$X2 = -1, [int]$Y2 = -1, [int]$Key = 0, [int]$HoldMs = 120, [int]$OffY = 0)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Inp {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
    public static IntPtr LP(int x, int y) { return (IntPtr)((y << 16) | (x & 0xFFFF)); }
}
'@
$h = (Get-Process -Id $ProcId -ErrorAction Stop).MainWindowHandle
$cr = New-Object Inp+RECT; [Inp]::GetClientRect($h, [ref]$cr) | Out-Null
$wr = New-Object Inp+RECT; [Inp]::GetWindowRect($h, [ref]$wr) | Out-Null
$pt = New-Object Inp+POINT; [Inp]::ClientToScreen($h, [ref]$pt) | Out-Null
Write-Output ("client {0}x{1} window {2}x{3} clientOrigin +{4},+{5}" -f $cr.R, $cr.B, ($wr.R-$wr.L), ($wr.B-$wr.T), ($pt.X-$wr.L), ($pt.Y-$wr.T))
if ($Action -eq 'tap' -or $Action -eq 'drag') {
    [Inp]::PostMessage($h, 0x0200, [IntPtr]0, [Inp]::LP($X, $Y + $OffY)) | Out-Null
    [Inp]::PostMessage($h, 0x0201, [IntPtr]1, [Inp]::LP($X, $Y + $OffY)) | Out-Null
    Start-Sleep -Milliseconds $HoldMs
    if ($Action -eq 'drag') {
        for ($i = 1; $i -le 8; $i++) {
            $mx = $X + ($X2 - $X) * $i / 8; $my = $Y + ($Y2 - $Y) * $i / 8
            [Inp]::PostMessage($h, 0x0200, [IntPtr]1, [Inp]::LP([int]$mx, [int]$my + $OffY)) | Out-Null
            Start-Sleep -Milliseconds 30
        }
        [Inp]::PostMessage($h, 0x0202, [IntPtr]0, [Inp]::LP($X2, $Y2 + $OffY)) | Out-Null
    } else {
        [Inp]::PostMessage($h, 0x0202, [IntPtr]0, [Inp]::LP($X, $Y + $OffY)) | Out-Null
    }
} elseif ($Action -eq 'key') {
    [Inp]::PostMessage($h, 0x0100, [IntPtr]$Key, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds $HoldMs
    [Inp]::PostMessage($h, 0x0101, [IntPtr]$Key, [IntPtr]0xC0000001) | Out-Null
}
