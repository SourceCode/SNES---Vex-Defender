Add-Type @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public class ScreenCapture2 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hDC, uint nFlags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static void Capture(string path) {
        IntPtr found = IntPtr.Zero;
        string foundTitle = "";
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd)) return true;
            var sb = new StringBuilder(256);
            GetWindowText(hWnd, sb, 256);
            string title = sb.ToString();
            if (title == "vex_defender" || title == "VEX DEFENDER" || (title.StartsWith("bsnes") && !title.Contains("File Explorer") && !title.Contains("windows"))) {
                found = hWnd;
                foundTitle = title;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        if (found == IntPtr.Zero) { Console.WriteLine("bsnes window not found"); return; }
        Console.WriteLine("Found: " + foundTitle);

        SetWindowPos(found, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002);
        SetForegroundWindow(found);
        System.Threading.Thread.Sleep(1000);

        RECT rect;
        GetWindowRect(found, out rect);
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;

        using (var bmp = new Bitmap(width, height)) {
            using (var g = Graphics.FromImage(bmp)) {
                IntPtr hdc = g.GetHdc();
                bool ok = PrintWindow(found, hdc, 2);
                g.ReleaseHdc(hdc);
                Console.WriteLine("PrintWindow: " + ok);
            }
            bmp.Save(path, ImageFormat.Png);
            Console.WriteLine("Captured: " + width + "x" + height);
        }

        SetWindowPos(found, new IntPtr(-2), 0, 0, 0, 0, 0x0001 | 0x0002);
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Drawing.Primitives
[ScreenCapture2]::Capture($args[0])
