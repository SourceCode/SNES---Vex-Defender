Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class InputSender {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

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
        System.Threading.Thread.Sleep(200);

        var down = new INPUT[1];
        down[0].type = 1; // INPUT_KEYBOARD
        down[0].u.ki.wVk = vk;
        down[0].u.ki.dwFlags = 0; // KEYEVENTF_KEYDOWN

        var up = new INPUT[1];
        up[0].type = 1;
        up[0].u.ki.wVk = vk;
        up[0].u.ki.dwFlags = 2; // KEYEVENTF_KEYUP

        SendInput(1, down, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(holdMs);
        SendInput(1, up, Marshal.SizeOf(typeof(INPUT)));
        Console.WriteLine("Sent VK=" + vk + " hold=" + holdMs + "ms");
    }
}
"@

$key = $args[0]
$hold = if ($args[1]) { [int]$args[1] } else { 150 }
switch ($key) {
    "enter"  { [InputSender]::Send(0x0D, $hold) }
    "x"      { [InputSender]::Send(0x58, $hold) }
    "z"      { [InputSender]::Send(0x5A, $hold) }
    "up"     { [InputSender]::Send(0x26, $hold) }
    "down"   { [InputSender]::Send(0x28, $hold) }
    "left"   { [InputSender]::Send(0x25, $hold) }
    "right"  { [InputSender]::Send(0x27, $hold) }
    "a"      { [InputSender]::Send(0x41, $hold) }
    "s"      { [InputSender]::Send(0x53, $hold) }
    default  { Write-Host "Unknown key: $key. Use: enter, x, z, up, down, left, right, a, s" }
}
