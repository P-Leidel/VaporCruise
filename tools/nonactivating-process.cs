using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class VaporCruiseMeasurementLaunch {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct STARTUPINFO {
        public uint cb; public string reserved; public string desktop; public string title;
        public uint x,y,xSize,ySize,xChars,yChars,fill,flags; public ushort show,reserved2;
        public IntPtr reservedBytes,input,output,error;
    }
    [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION {
        public IntPtr process,thread; public uint processId,threadId;
    }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left,top,right,bottom; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateProcess(string app, StringBuilder command, IntPtr pa, IntPtr ta,
        bool inherit, uint flags, IntPtr environment, string cwd, ref STARTUPINFO startup,
        out PROCESS_INFORMATION info);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    delegate bool EnumWindowsCallback(IntPtr window, IntPtr lp);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr lp);
    public static IntPtr FindWindow(int processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, lp) => { uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == (uint)processId) { found = window; return false; } return true; }, IntPtr.Zero);
        return found;
    }
    public static int Start(string app, string command, string cwd) {
        var startup = new STARTUPINFO(); startup.cb=(uint)Marshal.SizeOf(startup);
        startup.flags=1; startup.show=4;
        PROCESS_INFORMATION info;
        if (!CreateProcess(app, new StringBuilder(command), IntPtr.Zero, IntPtr.Zero, false, 0,
            IntPtr.Zero, cwd, ref startup, out info)) throw new Win32Exception();
        CloseHandle(info.thread); CloseHandle(info.process); return (int)info.processId;
    }
}
