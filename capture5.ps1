Add-Type @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public class ScreenCapture5 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr hdcDest, int x, int y, int w, int h, IntPtr hdcSrc, int sx, int sy, uint rop);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static void Capture(string path) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd)) return true;
            var sb = new StringBuilder(256);
            GetWindowText(hWnd, sb, 256);
            string title = sb.ToString();
            if (title == "vex_defender" || (title.StartsWith("bsnes") && !title.Contains("File Explorer"))) {
                found = hWnd;
                Console.WriteLine("Found: " + title);
                return false;
            }
            return true;
        }, IntPtr.Zero);
        if (found == IntPtr.Zero) { Console.WriteLine("Not found"); return; }

        // Move window to a clear area with padding
        MoveWindow(found, 200, 100, 540, 520, true);
        System.Threading.Thread.Sleep(500);
        SetForegroundWindow(found);
        System.Threading.Thread.Sleep(500);

        RECT r;
        GetWindowRect(found, out r);
        int w = r.Right - r.Left;
        int h = r.Bottom - r.Top;
        Console.WriteLine("Window: " + w + "x" + h + " at (" + r.Left + "," + r.Top + ")");

        IntPtr desktopDC = GetDC(IntPtr.Zero);
        using (var bmp = new Bitmap(w, h)) {
            using (var g = Graphics.FromImage(bmp)) {
                IntPtr bmpDC = g.GetHdc();
                BitBlt(bmpDC, 0, 0, w, h, desktopDC, r.Left, r.Top, 0x00CC0020);
                g.ReleaseHdc(bmpDC);
            }
            ReleaseDC(IntPtr.Zero, desktopDC);
            bmp.Save(path, ImageFormat.Png);
            Console.WriteLine("Saved: " + path + " (" + w + "x" + h + ")");
        }
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Drawing.Primitives
[ScreenCapture5]::Capture($args[0])
