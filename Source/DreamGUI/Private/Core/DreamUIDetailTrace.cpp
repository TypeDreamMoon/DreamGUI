// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIDetailTrace.h"

#if CPUPROFILERTRACE_ENABLED
UE_TRACE_CHANNEL_DEFINE(DreamUIDetailChannel, "DreamGUI's timing scopes that run once for every canvas, section or widget in a frame (see DreamUIDetailTrace.h).");
#endif
