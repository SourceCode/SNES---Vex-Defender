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
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    public static void Capture(string path) {
        IntPtr found = IntPtr.Zero;
        string foundTitle = "";
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd)) return true;
            var sb = new StringBuilder(256);
            GetWindowText(hWnd, sb, 256);
            string title = sb.ToString();
            if (title == "vex_defender" || (title.StartsWith("bsnes") && !title.Contains("File Explorer"))) {
                found = hWnd;
                foundTitle = title;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        if (found == IntPtr.Zero) { Console.WriteLine("bsnes window not found"); return; }
        Console.WriteLine("Found: " + foundTitle);

        MoveWindow(found, 100, 100, 530, 510, true);
        System.Threading.Thread.Sleep(300);
        SetForegroundWindow(found);
        System.Threading.Thread.Sleep(500);

        // Get client area screen coordinates
        RECT clientRect;
        GetClientRect(found, out clientRect);
        POINT topLeft = new POINT { X = clientRect.Left, Y = clientRect.Top };
        ClientToScreen(found, ref topLeft);

        int w = clientRect.Right - clientRect.Left;
        int h = clientRect.Bottom - clientRect.Top;

        Console.WriteLine("Client area: " + w + "x" + h + " at (" + topLeft.X + "," + topLeft.Y + ")");

        using (var bmp = new Bitmap(w, h)) {
            using (var g = Graphics.FromImage(bmp)) {
                g.CopyFromScreen(topLeft.X, topLeft.Y, 0, 0, new Size(w, h));
            }
            bmp.Save(path, ImageFormat.Png);
            Console.WriteLine("Captured to: " + path);
        }
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Drawing.Primitives
[ScreenCapture4]::Capture($args[0])
