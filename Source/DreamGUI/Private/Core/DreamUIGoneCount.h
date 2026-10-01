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
}
