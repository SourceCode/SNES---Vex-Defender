Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class KeySender {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    public const byte VK_RETURN = 0x0D;
    public const uint KEYEVENTF_KEYUP = 0x0002;

    public static void SendKey(string keyName) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd)) return true;
            var sb = new StringBuilder(256);
            GetWindowText(hWnd, sb, 256);
            string title = sb.ToString();
            if (title == "vex_defender" || (title.StartsWith("bsnes") && !title.Contains("File Explorer"))) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        if (found == IntPtr.Zero) { Console.WriteLine("Window not found"); return; }
        SetForegroundWindow(found);
        System.Threading.Thread.Sleep(200);

        byte vk = VK_RETURN;
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(50);
        keybd_event(vk, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        Console.WriteLine("Sent Enter key");
    }
}
"@
[KeySender]::SendKey("enter")
