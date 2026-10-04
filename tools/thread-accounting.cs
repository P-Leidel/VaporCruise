using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

// Read-only diagnostic handles. Start-module is provenance, not sampled execution.
public sealed class VaporCruiseThreadAccounting : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct FileTime { public uint Low, High; public ulong Value { get { return ((ulong)High << 32) | Low; } } }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetThreadTimes(IntPtr thread, out FileTime created, out FileTime exited, out FileTime kernel, out FileTime user);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr process, out FileTime created, out FileTime exited, out FileTime kernel, out FileTime user);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("ntdll.dll")] static extern int NtQueryInformationThread(IntPtr thread, int kind, out IntPtr value, uint length, out uint returned);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr GetSystemMenu(IntPtr window, bool revert);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint command, uint flags);
    public sealed class Module { public string Name, Path; public ulong BaseAddress, Size; }
    public sealed class Reading {
        public string Identity, StartModule, StartModulePath, StartAddress, StartAddressStatus;
        public uint ThreadId; public ulong CreationFileTime;
        public double UserTotalSeconds, KernelTotalSeconds, UserDeltaSeconds, KernelDeltaSeconds;
        public bool Enumerated; public int TimeQueryError;
    }
    public sealed class Sample {
        public double MonotonicSeconds, ProcessUserSeconds, ProcessKernelSeconds;
        public long PrivateBytes, WorkingSetBytes; public int Handles, ThreadEnumerationError;
        public int? EnumeratedThreadCount;
        public Reading[] Threads; public string[] OpenErrors;
    }
    sealed class Track {
        public IntPtr Handle, Address; public uint Id; public ulong Created, PreviousUser, PreviousKernel;
        public string Status;
    }
    readonly Process process;
    readonly Dictionary<string, Track> tracks = new Dictionary<string, Track>();
    readonly List<Module> modules = new List<Module>();
    ulong captureStartFileTime;
    public VaporCruiseThreadAccounting(int pid) { process=Process.GetProcessById(pid); var retained=process.Handle; }
    public Module[] Modules { get { return modules.ToArray(); } }
    void RefreshModules() {
        process.Refresh(); modules.Clear();
        foreach(ProcessModule m in process.Modules) modules.Add(new Module {Name=m.ModuleName,Path=m.FileName,BaseAddress=unchecked((ulong)m.BaseAddress.ToInt64()),Size=(ulong)m.ModuleMemorySize});
    }
    Module FindModule(IntPtr address) {
        var value=unchecked((ulong)address.ToInt64());
        foreach(var m in modules) if(value>=m.BaseAddress && value-m.BaseAddress<m.Size) return m;
        return null;
    }
    public Sample Read(bool baseline) {
        if(baseline) { captureStartFileTime=(ulong)DateTime.UtcNow.ToFileTimeUtc(); RefreshModules(); }
        var ids=new HashSet<uint>(); var errors=new List<string>(); int enumerationError=0;
        process.Refresh();
        try { foreach(ProcessThread t in process.Threads) ids.Add((uint)t.Id); }
        catch(Exception ex) { enumerationError=ex.HResult; }
        foreach(var tid in ids) {
            var handle=OpenThread(0x40,false,tid);
            if(handle==IntPtr.Zero) { errors.Add(tid+": OpenThread error "+Marshal.GetLastWin32Error()); continue; }
            FileTime created,exited,kernel,user;
            if(!GetThreadTimes(handle,out created,out exited,out kernel,out user)) { errors.Add(tid+": GetThreadTimes error "+Marshal.GetLastWin32Error()); CloseHandle(handle); continue; }
            var key=tid+"-"+created.Value;
            if(tracks.ContainsKey(key)) { CloseHandle(handle); continue; }
            IntPtr address=IntPtr.Zero; string status;
            try { uint length; int nt=NtQueryInformationThread(handle,9,out address,(uint)IntPtr.Size,out length); status="NTSTATUS 0x"+unchecked((uint)nt).ToString("X8"); if(nt<0) address=IntPtr.Zero; }
            catch(EntryPointNotFoundException) { status="NtQueryInformationThread unavailable"; }
            catch(DllNotFoundException) { status="ntdll unavailable"; }
            var before=baseline || created.Value<captureStartFileTime;
            tracks.Add(key,new Track {Handle=handle,Address=address,Id=tid,Created=created.Value,PreviousUser=before?user.Value:0,PreviousKernel=before?kernel.Value:0,Status=status});
            if(address!=IntPtr.Zero && FindModule(address)==null) RefreshModules();
        }
        var readings=new List<Reading>();
        foreach(var pair in tracks) {
            var t=pair.Value; FileTime created,exited,kernel,user;
            var reading=new Reading {Identity=pair.Key,ThreadId=t.Id,CreationFileTime=t.Created,Enumerated=ids.Contains(t.Id),StartAddress=t.Address==IntPtr.Zero?null:"0x"+unchecked((ulong)t.Address.ToInt64()).ToString("X"),StartAddressStatus=t.Status};
            var module=FindModule(t.Address); if(module!=null) { reading.StartModule=module.Name; reading.StartModulePath=module.Path; }
            if(GetThreadTimes(t.Handle,out created,out exited,out kernel,out user)) {
                reading.UserTotalSeconds=user.Value/10000000.0; reading.KernelTotalSeconds=kernel.Value/10000000.0;
                reading.UserDeltaSeconds=baseline?0:(user.Value>=t.PreviousUser?(user.Value-t.PreviousUser)/10000000.0:0);
                reading.KernelDeltaSeconds=baseline?0:(kernel.Value>=t.PreviousKernel?(kernel.Value-t.PreviousKernel)/10000000.0:0);
                t.PreviousUser=user.Value; t.PreviousKernel=kernel.Value;
            } else reading.TimeQueryError=Marshal.GetLastWin32Error();
            readings.Add(reading);
        }
        FileTime pc,pe,pk,pu;
        if(!GetProcessTimes(process.Handle,out pc,out pe,out pk,out pu)) throw new System.ComponentModel.Win32Exception();
        return new Sample {MonotonicSeconds=Stopwatch.GetTimestamp()/(double)Stopwatch.Frequency,ProcessUserSeconds=pu.Value/10000000.0,ProcessKernelSeconds=pk.Value/10000000.0,PrivateBytes=process.PrivateMemorySize64,WorkingSetBytes=process.WorkingSet64,Handles=process.HandleCount,ThreadEnumerationError=enumerationError,EnumeratedThreadCount=enumerationError==0?(int?)ids.Count:null,Threads=readings.ToArray(),OpenErrors=errors.ToArray()};
    }
    public void Dispose() { foreach(var t in tracks.Values) CloseHandle(t.Handle); tracks.Clear(); process.Dispose(); }
}
