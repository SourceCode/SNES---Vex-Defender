Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class KeySender2 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;

    public static IntPtr FindWindow() {
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
        return found;
    }

    public static void Send(byte vk) {
        IntPtr hWnd = FindWindow();
        if (hWnd == IntPtr.Zero) { Console.WriteLine("Not found"); return; }
        SetForegroundWindow(hWnd);
        System.Threading.Thread.Sleep(300);

        // Use PostMessage to send keys directly to the window
        PostMessage(hWnd, WM_KEYDOWN, (IntPtr)vk, IntPtr.Zero);
        System.Threading.Thread.Sleep(100);
        PostMessage(hWnd, WM_KEYUP, (IntPtr)vk, IntPtr.Zero);
        Console.WriteLine("Sent VK=" + vk);
    }
}
"@

# bsnes v115 default: Enter = Start
$key = $args[0]
switch ($key) {
    "enter"  { [KeySender2]::Send(0x0D) }  # Enter = Start
    "x"      { [KeySender2]::Send(0x58) }  # X = A button
    "z"      { [KeySender2]::Send(0x5A) }  # Z = B button
    "up"     { [KeySender2]::Send(0x26) }  # Up arrow
    "down"   { [KeySender2]::Send(0x28) }  # Down arrow
    "left"   { [KeySender2]::Send(0x25) }  # Left arrow
    "right"  { [KeySender2]::Send(0x27) }  # Right arrow
    "a"      { [KeySender2]::Send(0x41) }  # A = Y button
    "s"      { [KeySender2]::Send(0x53) }  # S = X button
    default  { Write-Host "Unknown key: $key" }
}
