// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamKeyInterface.h"
#include "DreamPlayerScreenTestTypes.generated.h"

/**
 * A behaviour that speaks the key channel and records what it was offered.
 *
 * The key dispatch is a bubble walk with a stop condition, and both halves are invisible from
 * outside: a handler that keeps a key must end the walk, and one that does not must let it carry on
 * up. Counting is how a test sees which of the two happened.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamKeyRecordingBehaviour : public UDreamUIBehaviour, public IDreamKeyInterface
{
	GENERATED_BODY()
public:
	/** Set before dispatching: whether this handler keeps the key it is offered. */
	bool bKeepTheKey = false;

	int32 KeyDownCount = 0;
	int32 KeyUpCount = 0;
	int32 KeyCharCount = 0;
	int32 AnalogCount = 0;
	FKey LastKey;

	virtual void OnKeyDown_Implementation(UDreamKeyEventData* EventData) override
	{
		++KeyDownCount;
		if (EventData != nullptr)
		{
			LastKey = EventData->Key;
			EventData->bHandled = EventData->bHandled || bKeepTheKey;
		}
	}
	virtual void OnKeyUp_Implementation(UDreamKeyEventData* EventData) override
	{
		++KeyUpCount;
		if (EventData != nullptr && bKeepTheKey)
		{
			EventData->bHandled = true;
		}
	}
	virtual void OnKeyChar_Implementation(UDreamKeyEventData* EventData) override
	{
		++KeyCharCount;
		if (EventData != nullptr && bKeepTheKey)
		{
			EventData->bHandled = true;
		}
	}
	virtual void OnAnalogValueChanged_Implementation(UDreamKeyEventData* EventData) override
	{
		++AnalogCount;
		if (EventData != nullptr && bKeepTheKey)
		{
			EventData->bHandled = true;
		}
	}
};
