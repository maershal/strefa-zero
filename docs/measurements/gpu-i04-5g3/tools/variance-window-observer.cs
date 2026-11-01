using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace StrefaVariance {
    public class WindowSample {
        public int Count, Visible, Minimized, LargestOuterWidth, LargestOuterHeight;
        public uint ForegroundPid;
        public bool NativeForeground;
        public int AcLine = -1, BatteryPercent = -1;
    }
    public static class WindowObserver {
        private delegate bool EnumProc(IntPtr hwnd, IntPtr data);
        [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] private struct PowerStatus {
            public byte AcLine, BatteryFlag, BatteryPercent, SystemStatus;
            public uint BatteryLife, FullBatteryLife;
        }
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr data);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
        [DllImport("kernel32.dll")] private static extern bool GetSystemPowerStatus(out PowerStatus status);
        public static WindowSample Read(int processId) {
            WindowSample sample = new WindowSample();
            uint foreground; GetWindowThreadProcessId(GetForegroundWindow(), out foreground);
            sample.ForegroundPid = foreground;
            sample.NativeForeground = foreground == (uint)processId;
            long largest = -1;
            EnumWindows(delegate(IntPtr hwnd, IntPtr ignored) {
                uint owner; GetWindowThreadProcessId(hwnd, out owner);
                if (owner != (uint)processId) return true;
                ++sample.Count;
                if (IsWindowVisible(hwnd)) ++sample.Visible;
                if (IsIconic(hwnd)) ++sample.Minimized;
                Rect rect;
                if (GetWindowRect(hwnd, out rect)) {
                    int width = Math.Max(0, rect.Right - rect.Left), height = Math.Max(0, rect.Bottom - rect.Top);
                    long area = (long)width * height;
                    if (area > largest) { largest = area; sample.LargestOuterWidth = width; sample.LargestOuterHeight = height; }
                }
                return true;
            }, IntPtr.Zero);
            PowerStatus power;
            if (GetSystemPowerStatus(out power)) { sample.AcLine = power.AcLine; sample.BatteryPercent = power.BatteryPercent; }
            return sample;
        }
    }
}
