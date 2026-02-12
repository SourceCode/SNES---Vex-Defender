Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class ScanSender {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);

    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public uint type;
        public INPUTUNION u;
    }
    [StructLayout(LayoutKind.Explicit)]
    public struct INPUTUNION {
        [FieldOffset(0)] public KEYBDINPUT ki;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    public const uint KEYEVENTF_SCANCODE = 0x0008;
    public const uint KEYEVENTF_KEYUP = 0x0002;

    public static void Send(ushort vk, int holdMs) {
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
        if (found == IntPtr.Zero) { Console.WriteLine("Not found"); return; }

        SetForegroundWindow(found);
        System.Threading.Thread.Sleep(300);

        ushort scan = (ushort)MapVirtualKey(vk, 0); // MAPVK_VK_TO_VSC

        // Key down with both VK and scan code
        var inputs = new INPUT[2];
        inputs[0].type = 1;
        inputs[0].u.ki.wVk = vk;
        inputs[0].u.ki.wScan = scan;
        inputs[0].u.ki.dwFlags = KEYEVENTF_SCANCODE;

        inputs[1].type = 1;
        inputs[1].u.ki.wVk = vk;
        inputs[1].u.ki.wScan = scan;
        inputs[1].u.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        uint sent = SendInput(1, new INPUT[] { inputs[0] }, Marshal.SizeOf(typeof(INPUT)));
        Console.WriteLine("Down sent=" + sent + " vk=0x" + vk.ToString("X2") + " scan=0x" + scan.ToString("X2"));
        System.Threading.Thread.Sleep(holdMs);
        sent = SendInput(1, new INPUT[] { inputs[1] }, Marshal.SizeOf(typeof(INPUT)));
        Console.WriteLine("Up sent=" + sent);
    }
}
"@

$key = $args[0]
$hold = if ($args[1]) { [int]$args[1] } else { 200 }
switch ($key) {
    "enter"  { [ScanSender]::Send(0x0D, $hold) }
    "x"      { [ScanSender]::Send(0x58, $hold) }
    "z"      { [ScanSender]::Send(0x5A, $hold) }
    "up"     { [ScanSender]::Send(0x26, $hold) }
    "down"   { [ScanSender]::Send(0x28, $hold) }
    "left"   { [ScanSender]::Send(0x25, $hold) }
    "right"  { [ScanSender]::Send(0x27, $hold) }
    "a"      { [ScanSender]::Send(0x41, $hold) }
    "s"      { [ScanSender]::Send(0x53, $hold) }
    default  { Write-Host "Unknown key: $key" }
}
