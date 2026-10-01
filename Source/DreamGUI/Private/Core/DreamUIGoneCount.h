// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A count that moves on whenever an object may have stopped being one to write: any object deleted, a DreamGUI widget or
 * behaviour unregistered or collected, a destroyed tree's widgets and parts marked garbage. A DreamGUI widget or behaviour a weak look-up
 * found alive while the count read N is still that object, alive and as registered as it was, while the count reads N: what
 * lets an animation's write of it skip the look-up frame after frame (FDreamUIDirectAnimationEvaluation's bound objects).
 */
namespace DreamUIGone
{
	/** The count. Deletions are counted from the first time it is read, on the game thread. */
	uint64 Read();
	/** The count as it is, from any thread, without starting to count: 0 until the game thread first read it. */
	uint64 Peek();
	/** Moves the count on. Any thread. */
	void Note();
	/** Stops counting deletions: the module is going. */
	void StopListening();

	/**
	 * r.DreamUI.VerifyKeptPointers. With it on, every place that uses an object kept by the count instead of looking it up
	 * looks it up as well, and a disagreement is reported (CheckKept). Off by default: the test host turns it on for the
	 * whole suite, and the benchmarks turn it off for themselves, since it costs the look-ups the kept pointers save.
	 */
	inline bool IsVerifyingKept();
	/**
	 * With IsVerifyingKept: Kept, an object kept while the count read the same, against LookedUp, what its weak pointer
	 * answers now. Any thread. Kept is never dereferenced -- it may be just what the check is about. A disagreement is
	 * counted (GetKeptDisagreements), logged as an error naming Where, and an ensure.
	 */
	DREAMGUI_API void CheckKept(const void* Kept, const UObject* LookedUp, const TCHAR* Where);
	/** How many disagreements CheckKept has found since the process began. */
	DREAMGUI_API uint64 GetKeptDisagreements();
}

/** r.DreamUI.VerifyKeptPointers; see DreamUIGone::IsVerifyingKept. */
extern DREAMGUI_API int32 GDreamUIVerifyKeptPointers;

inline bool DreamUIGone::IsVerifyingKept()
{
	return GDreamUIVerifyKeptPointers != 0;
}
