using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

// Native observations of the real app, not a duplicated cursor-policy model.
public static class VaporCruiseCursorProbe {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] public struct CURSORINFO {
        public uint size, flags; public IntPtr cursor; public POINT position;
    }
    [DllImport("user32.dll", SetLastError=true)] static extern bool GetCursorInfo(ref CURSORINFO info);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT point);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr SendMessageTimeout(IntPtr window, uint message, UIntPtr wp, IntPtr lp,
        uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr LoadCursor(IntPtr instance, IntPtr name);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    public static CURSORINFO Observe() {
        var info = new CURSORINFO(); info.size = (uint)Marshal.SizeOf(info);
        if (!GetCursorInfo(ref info)) throw new Win32Exception();
        return info;
    }
    public static long Send(IntPtr window, uint message, UIntPtr wp, IntPtr lp) {
        UIntPtr result;
        if (SendMessageTimeout(window, message, wp, lp, 2, 1000, out result) == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Owned-window message failed or timed out.");
        return unchecked((long)result.ToUInt64());
    }
    public static IntPtr PointParameter(int x, int y) {
        return new IntPtr(unchecked((int)(((uint)y & 65535) << 16 | ((uint)x & 65535))));
    }
    public static IntPtr CursorParameter(int hit) { return new IntPtr((0x200 << 16) | (hit & 65535)); }
    public static UIntPtr WindowParameter(IntPtr window) { return new UIntPtr(unchecked((ulong)window.ToInt64())); }
    public static IntPtr StandardCursor(int resource) { return LoadCursor(IntPtr.Zero, new IntPtr(resource)); }
}
