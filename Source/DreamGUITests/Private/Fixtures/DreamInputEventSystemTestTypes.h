// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "DreamInputEventSystemTestTypes.generated.h"

/**
 * The preset actor with its key-to-direction mapping made reachable.
 *
 * Both resolvers are protected on the actor, which is right -- they are the preset's own business --
 * and it also means the mapping table has no test surface at all. A subclass that does nothing but
 * forward is the cheapest way to assert on it without widening the real class's interface, and the
 * forwards being one-liners is what keeps this a window rather than a second implementation.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ADreamNavigationKeyProbeActor : public ADreamStandaloneInputEventSystemActor
{
	GENERATED_BODY()

public:
	/** What the table alone says a key means, modifiers ignored. */
	static EDreamUINavigationDirection TableDirectionFor(const FKey& Key)
	{
		return GetNavigationDirectionForKey(Key);
	}

	/** What the key means for a press happening now, modifiers included. */
	EDreamUINavigationDirection DirectionFor(const FKey& Key) const
	{
		return ResolveNavigationDirection(Key);
	}
};
