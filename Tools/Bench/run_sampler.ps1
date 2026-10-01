# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
param([int]$TargetPid, [string]$Flag, [string]$Report, [double]$Seconds = 6.0, [int]$IntervalMs = 2, [int]$MaxFrames = 48,
    [string]$Thread = "", [string]$Symbols = "")
# One sampling session in a process of its own: dbghelp keeps process-wide state that a second session in the same
# process does not get back. Symbols: the folders holding the PDBs (the plugin's binaries, the engine's), ';'-separated.
Add-Type -Path (Join-Path $PSScriptRoot "StackSampler.cs")
[StackSampler]::ThreadName = $Thread
[StackSampler]::Run($TargetPid, $Symbols, $Flag, $Seconds, $IntervalMs, $MaxFrames, $Report, 600.0)
