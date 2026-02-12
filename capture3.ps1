Add-Type @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public class ScreenCapture4 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
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

        MoveWindow(found, 100, 100, 530, 510, true);
        System.Threading.Thread.Sleep(200);
        SetForegroundWindow(found);
        System.Threading.Thread.Sleep(500);

        RECT rect;
        GetWindowRect(found, out rect);
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;

        // Screen capture (CopyFromScreen) instead of PrintWindow
        using (var bmp = new Bitmap(width, height)) {
            using (var g = Graphics.FromImage(bmp)) {
                g.CopyFromScreen(rect.Left, rect.Top, 0, 0, new Size(width, height));
            }
            bmp.Save(path, ImageFormat.Png);
            Console.WriteLine("Captured: " + width + "x" + height);
        }
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Drawing.Primitives
[ScreenCapture4]::Capture($args[0])
