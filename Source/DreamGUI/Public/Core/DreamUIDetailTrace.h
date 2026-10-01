// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.h"

/**
 * The trace channel of DreamGUI's finer timing scopes: the ones a frame runs once for every canvas, section or widget, which
 * a wall of thousands of them runs thousands of times. Each scope reads the clock twice, and on a machine whose clock is slow
 * to read that came to milliseconds a frame -- enough for a traced frame to say little about an untraced one. So they are
 * off unless asked for: -trace=default,DreamUIDetail on the command line, or Trace.Enable DreamUIDetail. The scopes around
 * whole passes stay on the CPU channel, and say what the passes cost together.
 */
#if CPUPROFILERTRACE_ENABLED
UE_TRACE_CHANNEL_EXTERN(DreamUIDetailChannel, DREAMGUI_API);
/** A timing scope on DreamUIDetailChannel, named as TRACE_CPUPROFILER_EVENT_SCOPE names one. */
#define DREAMUI_DETAIL_SCOPE(Name) TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, DreamUIDetailChannel)
#else
#define DREAMUI_DETAIL_SCOPE(Name)
#endif
