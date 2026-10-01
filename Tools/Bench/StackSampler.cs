// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
// A sampling profiler for one thread of a running x64 process, without a debugger and without instrumenting it. Each
// sample suspends the thread only long enough to take its registers and one copy of its stack; the thread then runs on
// while dbghelp's StackWalk64 walks the copy (stack reads are served from it, code and unwind data from the process).
// Program counters are resolved to symbols once sampling is over. Reports self and inclusive counts per function.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class StackSampler
{
    const uint PROCESS_ALL_ACCESS = 0x1FFFFF;
    const uint THREAD_ALL_ACCESS = 0x1FFFFF;
    const uint CONTEXT_FULL_AMD64 = 0x10000B;
    const uint IMAGE_FILE_MACHINE_AMD64 = 0x8664;
    const uint SYMOPT_UNDNAME = 0x2, SYMOPT_DEFERRED_LOADS = 0x4, SYMOPT_LOAD_LINES = 0x10, SYMOPT_FAIL_CRITICAL_ERRORS = 0x200, SYMOPT_NO_PROMPTS = 0x80000;
    const int ContextSize = 1232;

    [StructLayout(LayoutKind.Sequential)]
    struct MEMORY_BASIC_INFORMATION64
    {
        public ulong BaseAddress, AllocationBase;
        public uint AllocationProtect, __alignment1;
        public ulong RegionSize;
        public uint State, Protect, Type, __alignment2;
    }

    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenThread(uint access, bool inherit, int tid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern int GetThreadDescription(IntPtr thread, out IntPtr description);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr mem);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, IntPtr buffer, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MEMORY_BASIC_INFORMATION64 info, IntPtr length);
    [DllImport("kernel32.dll")] static extern void RtlMoveMemory(IntPtr dest, IntPtr src, UIntPtr length);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr LoadLibraryW(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr mod, string name);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr CreateWaitableTimerExW(IntPtr attributes, string name, uint flags, uint access);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetWaitableTimer(IntPtr timer, ref long dueTime, int period, IntPtr completion, IntPtr argument, bool resume);
    [DllImport("kernel32.dll", SetLastError = true)] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    const uint CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x2, TIMER_ALL_ACCESS = 0x1F0003;
    [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint p);
    [DllImport("winmm.dll")] static extern uint timeEndPeriod(uint p);

    [DllImport("dbghelp.dll", SetLastError = true)] static extern uint SymSetOptions(uint opts);
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SymInitializeW(IntPtr proc, string searchPath, bool invade);
    [DllImport("dbghelp.dll", SetLastError = true)] static extern bool SymRefreshModuleList(IntPtr proc);
    [DllImport("dbghelp.dll", SetLastError = true)] static extern bool SymCleanup(IntPtr proc);
    [DllImport("dbghelp.dll", SetLastError = true)] static extern bool SymFromAddrW(IntPtr proc, ulong addr, out ulong displacement, IntPtr symbol);
    [DllImport("dbghelp.dll", SetLastError = true)] static extern bool SymGetLineFromAddrW64(IntPtr proc, ulong addr, out uint displacement, IntPtr line);
    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool StackWalk64(uint machine, IntPtr proc, IntPtr thread, IntPtr frame, IntPtr ctx,
        IntPtr readMemory, IntPtr functionTableAccess, IntPtr getModuleBase, IntPtr translateAddress);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate bool ReadMemoryRoutine(IntPtr hProcess, ulong baseAddress, IntPtr buffer, uint size, out uint bytesRead);

    // The stack copy StackWalk64 reads from while it walks one sample.
    static ulong s_snapBase;
    static IntPtr s_snap;
    static int s_snapLength;
    static bool ReadMemory(IntPtr hProcess, ulong address, IntPtr buffer, uint size, out uint bytesRead)
    {
        if (address >= s_snapBase && address + size <= s_snapBase + (ulong)s_snapLength)
        {
            RtlMoveMemory(buffer, new IntPtr(s_snap.ToInt64() + (long)(address - s_snapBase)), new UIntPtr(size));
            bytesRead = size;
            return true;
        }
        IntPtr read;
        bool ok = ReadProcessMemory(hProcess, new IntPtr((long)address), buffer, new IntPtr(size), out read);
        bytesRead = (uint)read.ToInt64();
        return ok;
    }
    static readonly ReadMemoryRoutine s_readMemory = ReadMemory;

    /// The thread whose description (the name UE gives it, e.g. "RenderThread 0") starts with namePrefix, or -1.
    static int NamedThreadId(Process process, string namePrefix)
    {
        foreach (ProcessThread t in process.Threads)
        {
            IntPtr h = OpenThread(0x0800 /*THREAD_QUERY_LIMITED_INFORMATION*/, false, t.Id);
            if (h == IntPtr.Zero) continue;
            try
            {
                IntPtr text;
                if (GetThreadDescription(h, out text) >= 0 && text != IntPtr.Zero)
                {
                    string name = Marshal.PtrToStringUni(text);
                    LocalFree(text);
                    if (name != null && name.StartsWith(namePrefix, StringComparison.OrdinalIgnoreCase)) return t.Id;
                }
            }
            finally { CloseHandle(h); }
        }
        return -1;
    }

    /// The thread to sample: the main thread, or the one named by threadName ("RenderThread", "RHIThread", ...).
    public static string ThreadName = "";

    static int MainThreadId(Process process)
    {
        int main = -1;
        DateTime earliest = DateTime.MaxValue;
        foreach (ProcessThread t in process.Threads)
        {
            try { if (t.StartTime < earliest) { earliest = t.StartTime; main = t.Id; } } catch { }
        }
        return main;
    }

    /// Samples the main thread of pid, every intervalMs, during every stretch the file flagPath exists, until maxSeconds
    /// have been sampled, flagPath + ".done" appears, or no flag comes for waitForFlagSeconds. Writes a report.
    public static string Run(int pid, string symbolPath, string flagPath, double maxSeconds, int intervalMs, int maxFrames, string reportPath, double waitForFlagSeconds)
    {
        var process = Process.GetProcessById(pid);
        int tid = MainThreadId(process);
        IntPtr hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
        if (hProcess == IntPtr.Zero) return "OpenProcess failed: " + Marshal.GetLastWin32Error();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
        // Initialized once the first flag appears, not now: the process may still be loading its modules, and an
        // initialization that enumerates them then can fail and leave every walk and name unresolved.
        bool symbolsReady = false;
        string symbolNote = "symbols not initialized";
        IntPtr dbghelp = LoadLibraryW("dbghelp.dll");
        IntPtr functionTableAccess = GetProcAddress(dbghelp, "SymFunctionTableAccess64");
        IntPtr getModuleBase = GetProcAddress(dbghelp, "SymGetModuleBase64");
        IntPtr readMemory = Marshal.GetFunctionPointerForDelegate(s_readMemory);
        IntPtr hThread = OpenThread(THREAD_ALL_ACCESS, false, tid);
        IntPtr contextRaw = Marshal.AllocHGlobal(4096);
        IntPtr context = new IntPtr((contextRaw.ToInt64() + 15) & ~15L);
        IntPtr frame = Marshal.AllocHGlobal(1024);
        const int MaxSnap = 512 * 1024;
        s_snap = Marshal.AllocHGlobal(MaxSnap);
        var samples = new List<ulong[]>();
        var pcs = new List<ulong>(maxFrames);
        timeBeginPeriod(1);
        // A sleep rounds up to the system tick (15.6 ms) when the 1 ms request is not honoured, as for a process without a
        // visible window on Windows 11: a high-resolution waitable timer keeps the interval whatever the tick is.
        IntPtr timer = CreateWaitableTimerExW(IntPtr.Zero, null, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        Action pause = () =>
        {
            long due = -10000L * intervalMs;
            if (timer != IntPtr.Zero && SetWaitableTimer(timer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false)) WaitForSingleObject(timer, 1000);
            else Thread.Sleep(intervalMs);
        };
        var clock = new Stopwatch();
        double suspendedMs = 0;
        int failedReads = 0;
        bool modulesRefreshed = false;
        string doneFlag = flagPath + ".done";
        while (clock.Elapsed.TotalSeconds < maxSeconds && !File.Exists(doneFlag))
        {
            var waitClock = Stopwatch.StartNew();
            bool flagged = false;
            while (!(flagged = File.Exists(flagPath)))
            {
                if (waitClock.Elapsed.TotalSeconds > waitForFlagSeconds || File.Exists(doneFlag)) break;
                Thread.Sleep(2);
            }
            if (!flagged) break;
            if (!modulesRefreshed && ThreadName.Length > 0)
            {
                process.Refresh();
                int named = NamedThreadId(process, ThreadName);
                if (named > 0 && named != tid)
                {
                    CloseHandle(hThread);
                    tid = named;
                    hThread = OpenThread(THREAD_ALL_ACCESS, false, tid);
                }
                else if (named <= 0) symbolNote += "; no thread named " + ThreadName;
            }
            if (!modulesRefreshed)
            {
                // Plugins load after the process starts: dbghelp learns of them here, before the first walk needs their unwind data.
                for (int attempt = 0; attempt < 5 && !symbolsReady; attempt++)
                {
                    if (SymInitializeW(hProcess, symbolPath, true))
                    {
                        symbolsReady = true;
                        symbolNote = "symbols initialized on attempt " + (attempt + 1);
                    }
                    else
                    {
                        symbolNote = "SymInitialize failed: " + Marshal.GetLastWin32Error();
                        SymCleanup(hProcess);
                        Thread.Sleep(50);
                    }
                }
                if (symbolsReady && !SymRefreshModuleList(hProcess)) symbolNote += ", SymRefreshModuleList failed: " + Marshal.GetLastWin32Error();
                modulesRefreshed = true;
            }
            clock.Start();
            while (File.Exists(flagPath) && clock.Elapsed.TotalSeconds < maxSeconds)
            {
                var one = Stopwatch.StartNew();
                bool haveContext = false;
                s_snapLength = 0;
                SuspendThread(hThread);
                try
                {
                    for (int i = 0; i < ContextSize; i += 8) Marshal.WriteInt64(context, i, 0);
                    Marshal.WriteInt32(context, 0x30, unchecked((int)CONTEXT_FULL_AMD64));
                    if (GetThreadContext(hThread, context))
                    {
                        haveContext = true;
                        ulong rsp = (ulong)Marshal.ReadInt64(context, 0x98);
                        MEMORY_BASIC_INFORMATION64 info;
                        if (VirtualQueryEx(hProcess, new IntPtr((long)rsp), out info, new IntPtr(Marshal.SizeOf(typeof(MEMORY_BASIC_INFORMATION64)))) != IntPtr.Zero)
                        {
                            ulong end = info.BaseAddress + info.RegionSize;
                            int length = (int)Math.Min((ulong)MaxSnap, end - rsp);
                            IntPtr read;
                            if (ReadProcessMemory(hProcess, new IntPtr((long)rsp), s_snap, new IntPtr(length), out read))
                            {
                                s_snapBase = rsp;
                                s_snapLength = (int)read.ToInt64();
                            }
                            else failedReads++;
                        }
                    }
                }
                finally
                {
                    ResumeThread(hThread);
                }
                suspendedMs += one.Elapsed.TotalMilliseconds;
                pcs.Clear();
                if (haveContext && s_snapLength > 0)
                {
                    ulong rip = (ulong)Marshal.ReadInt64(context, 0xF8), rsp = (ulong)Marshal.ReadInt64(context, 0x98), rbp = (ulong)Marshal.ReadInt64(context, 0xA0);
                    for (int i = 0; i < 1024; i += 8) Marshal.WriteInt64(frame, i, 0);
                    Marshal.WriteInt64(frame, 0, (long)rip); Marshal.WriteInt32(frame, 12, 3);
                    Marshal.WriteInt64(frame, 32, (long)rbp); Marshal.WriteInt32(frame, 44, 3);
                    Marshal.WriteInt64(frame, 48, (long)rsp); Marshal.WriteInt32(frame, 60, 3);
                    for (int depth = 0; depth < maxFrames; depth++)
                    {
                        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, frame, context, readMemory, functionTableAccess, getModuleBase, IntPtr.Zero)) break;
                        ulong pc = (ulong)Marshal.ReadInt64(frame, 0);
                        if (pc == 0) break;
                        pcs.Add(pc);
                    }
                }
                if (pcs.Count > 0) samples.Add(pcs.ToArray());
                pause();
            }
            clock.Stop();
        }
        double seconds = clock.Elapsed.TotalSeconds;
        timeEndPeriod(1);

        // Resolve each distinct program counter once, against the modules loaded now.
        process.Refresh();
        var modules = new List<Tuple<ulong, ulong, string>>();
        foreach (ProcessModule m in process.Modules)
            modules.Add(Tuple.Create((ulong)m.BaseAddress.ToInt64(), (ulong)m.ModuleMemorySize, m.ModuleName));
        SymRefreshModuleList(hProcess);
        IntPtr symbol = Marshal.AllocHGlobal(88 + 2 * 1024);
        var names = new Dictionary<ulong, string>();
        Func<ulong, string> resolve = pc =>
        {
            string name;
            if (names.TryGetValue(pc, out name)) return name;
            string module = "?";
            foreach (var m in modules) if (pc >= m.Item1 && pc < m.Item1 + m.Item2) { module = m.Item3; break; }
            for (int i = 0; i < 88; i += 4) Marshal.WriteInt32(symbol, i, 0);
            Marshal.WriteInt32(symbol, 0, 88);
            Marshal.WriteInt32(symbol, 80, 1024);
            ulong displacement;
            string function = SymFromAddrW(hProcess, pc, out displacement, symbol) ? Marshal.PtrToStringUni(new IntPtr(symbol.ToInt64() + 84)) : "?";
            // Engine modules come without PDBs here: the nearest export, marked so.
            bool exact = module.IndexOf("DreamGUI", StringComparison.OrdinalIgnoreCase) >= 0;
            name = module.Replace("UnrealEditor-", "").Replace(".dll", "") + "!" + function + (exact ? "" : "~");
            names[pc] = name;
            return name;
        };
        // Source lines, for DreamGUI's own modules (the only ones with PDBs here): IMAGEHLP_LINEW64 is 40 bytes on x64.
        IntPtr lineInfo = Marshal.AllocHGlobal(64);
        var lines = new Dictionary<ulong, string>();
        Func<ulong, string> resolveLine = pc =>
        {
            string text;
            if (lines.TryGetValue(pc, out text)) return text;
            text = null;
            for (int i = 0; i < 64; i += 4) Marshal.WriteInt32(lineInfo, i, 0);
            Marshal.WriteInt32(lineInfo, 0, 40);
            uint lineDisplacement;
            if (SymGetLineFromAddrW64(hProcess, pc, out lineDisplacement, lineInfo))
            {
                int number = Marshal.ReadInt32(lineInfo, 16);
                string file = Marshal.PtrToStringUni(Marshal.ReadIntPtr(lineInfo, 24)) ?? "?";
                text = Path.GetFileName(file) + ":" + number;
            }
            lines[pc] = text;
            return text;
        };
        Func<string, bool> isDream = name => name.StartsWith("DreamGUI", StringComparison.OrdinalIgnoreCase);
        var selfLines = new Dictionary<string, int>();
        var firstDreamLines = new Dictionary<string, int>();
        var self = new Dictionary<string, int>();
        var inclusive = new Dictionary<string, int>();
        var callers = new Dictionary<string, Dictionary<string, int>>();
        foreach (var s in samples)
        {
            // Where DreamGUI's own code was when the sample was taken: the leaf's line when the leaf is DreamGUI's, and the
            // first DreamGUI frame up from the leaf, whatever the leaf, with the function the leaf was in.
            for (int i = 0; i < s.Length; i++)
            {
                string frameName = resolve(s[i]);
                if (!isDream(frameName)) continue;
                string at = resolveLine(s[i]);
                string key = frameName + " @ " + (at ?? "?") + (i == 0 ? "" : "  -> " + resolve(s[0]));
                int count;
                firstDreamLines[key] = firstDreamLines.TryGetValue(key, out count) ? count + 1 : 1;
                if (i == 0)
                {
                    string selfKey = frameName + " @ " + (at ?? "?");
                    selfLines[selfKey] = selfLines.TryGetValue(selfKey, out count) ? count + 1 : 1;
                }
                break;
            }
            var seen = new HashSet<string>();
            string leaf = resolve(s[0]);
            int v; self[leaf] = self.TryGetValue(leaf, out v) ? v + 1 : 1;
            for (int i = 0; i < s.Length; i++)
            {
                string f = resolve(s[i]);
                if (seen.Add(f)) inclusive[f] = inclusive.TryGetValue(f, out v) ? v + 1 : 1;
                if (i + 1 < s.Length)
                {
                    string caller = resolve(s[i + 1]);
                    Dictionary<string, int> map;
                    if (!callers.TryGetValue(f, out map)) callers[f] = map = new Dictionary<string, int>();
                    map[caller] = map.TryGetValue(caller, out v) ? v + 1 : 1;
                }
            }
        }
        var report = new StringBuilder();
        report.AppendLine(string.Format("samples {0} over {1:F1} s (thread {2}), suspended {3:F1} ms in all ({4:F3} ms each), failed stack reads {5}; ~ marks a nearest-export name; {6}",
            samples.Count, seconds, tid, suspendedMs, samples.Count > 0 ? suspendedMs / samples.Count : 0, failedReads, symbolNote));
        report.AppendLine("== self");
        foreach (var kv in self.OrderByDescending(k => k.Value).Take(80))
            report.AppendLine(string.Format("{0,6:F2}% {1}", 100.0 * kv.Value / samples.Count, kv.Key));
        report.AppendLine("== inclusive");
        foreach (var kv in inclusive.OrderByDescending(k => k.Value).Take(200))
        {
            Dictionary<string, int> map;
            string top = callers.TryGetValue(kv.Key, out map) ? string.Join(" | ", map.OrderByDescending(k => k.Value).Take(3).Select(k => k.Key + " " + k.Value)) : "";
            report.AppendLine(string.Format("{0,6:F2}% {1}    <- {2}", 100.0 * kv.Value / samples.Count, kv.Key, top));
        }
        report.AppendLine("== self lines (DreamGUI leaf)");
        foreach (var kv in selfLines.OrderByDescending(k => k.Value).Take(80))
            report.AppendLine(string.Format("{0,6:F2}% {1}", 100.0 * kv.Value / samples.Count, kv.Key));
        report.AppendLine("== first DreamGUI line from the leaf (-> the leaf when it is not DreamGUI's)");
        foreach (var kv in firstDreamLines.OrderByDescending(k => k.Value).Take(120))
            report.AppendLine(string.Format("{0,6:F2}% {1}", 100.0 * kv.Value / samples.Count, kv.Key));
        // The raw stacks too, resolved, for any other cut of them later.
        using (var raw = new StreamWriter(Path.ChangeExtension(reportPath, ".stacks.txt")))
        {
            foreach (var s in samples)
                raw.WriteLine(string.Join(";", s.Reverse().Select(pc => resolve(pc))));
        }
        File.WriteAllText(reportPath, report.ToString());
        // The editor waits for this before it quits: symbols are resolved against the live process.
        File.WriteAllText(flagPath + ".resolved", "");
        Marshal.FreeHGlobal(symbol);
        Marshal.FreeHGlobal(lineInfo);
        Marshal.FreeHGlobal(contextRaw); Marshal.FreeHGlobal(frame); Marshal.FreeHGlobal(s_snap);
        CloseHandle(hThread);
        if (timer != IntPtr.Zero) CloseHandle(timer);
        SymCleanup(hProcess);
        CloseHandle(hProcess);
        return "wrote " + reportPath + " (" + samples.Count + " samples)";
    }
}
