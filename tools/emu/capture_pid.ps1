param([int]$ProcId, [string]$Out)
[System.Reflection.Assembly]::LoadWithPartialName('System.Drawing') | Out-Null

Add-Type -Language CSharp -ReferencedAssemblies System.Drawing @'
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
using System.Text;
public class Cap {
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, StringBuilder b, int c);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr obj);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int w, int h, IntPtr hdcSrc, int xSrc, int ySrc, uint rop);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }

    public static IntPtr FindByTitle(string search) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var sb = new StringBuilder(256);
            GetWindowText(h, sb, 256);
            if (sb.ToString().Contains(search)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static void Capture(string outPath, IntPtr target) {
        // Make DPI-aware so GetClientRect returns physical pixels
        SetProcessDPIAware();

        IntPtr hwnd = target;
        if (hwnd == IntPtr.Zero) { Console.WriteLine("No melonDS window"); return; }
        RECT r; GetClientRect(hwnd, out r);
        int w = r.R > 0 ? r.R : 256; int h = r.B > 0 ? r.B : 384;
        Console.WriteLine("Size: " + w + "x" + h);
        IntPtr dc = GetDC(hwnd);
        IntPtr mdc = CreateCompatibleDC(dc);
        IntPtr hbmp = CreateCompatibleBitmap(dc, w, h);
        IntPtr old = SelectObject(mdc, hbmp);
        // PW_RENDERFULLCONTENT = 2 captures the full client area including DPI-scaled content
        PrintWindow(hwnd, mdc, 2);
        SelectObject(mdc, old);
        Bitmap bmp = Image.FromHbitmap(hbmp);
        bmp.Save(outPath, ImageFormat.Png);
        bmp.Dispose();
        DeleteObject(hbmp); DeleteDC(mdc); ReleaseDC(hwnd, dc);
        Console.WriteLine("Saved: " + outPath);
    }
}
'@

$p = Get-Process -Id $ProcId -ErrorAction Stop
[Cap]::Capture($Out, $p.MainWindowHandle)
