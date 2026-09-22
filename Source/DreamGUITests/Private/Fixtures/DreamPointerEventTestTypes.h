// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamGestureEventData.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
#include "Event/Interface/DreamPointerLongPressInterface.h"
#include "Event/Interface/DreamPointerGestureInterface.h"
#include "Interaction/DreamUIActionTrigger.h"
#include "DreamPointerEventTestTypes.generated.h"

/**
 * Counts double clicks that were actually dispatched to a widget.
 *
 * UUIEventTrigger covers the other pointer events and a test can read it for those, but it predates
 * the double-click event; asserting on the pointer's own ClickCount would only prove that the counting
 * arithmetic ran, not that anything was told about it.
 */
UCLASS()
class UDreamDoubleClickCounter : public UDreamUIBehaviour, public IDreamPointerDoubleClickInterface
{
	GENERATED_BODY()

public:
	int32 DoubleClickCount = 0;
	/** What the pointer said its run was up to when this arrived. 2 for an ordinary double click. */
	int32 LastReportedClickCount = 0;

	virtual bool OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData) override
	{
		++DoubleClickCount;
		LastReportedClickCount = EventData != nullptr ? EventData->ClickCount : 0;
		return true;
	}
};

/**
 * Something for an authored event binding to call, with one entry point per numeric width.
 *
 * FDreamUIEventDelegateData resolves its target by reflection and invokes it through ProcessEvent, so
 * what it hands the function can only be checked by having a real UFUNCTION on a real behaviour read
 * its own parameter back.
 */
UCLASS()
class UDreamEventDelegateReceiver : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	float LastFloat = 0.0f;
	double LastDouble = 0.0;
	int32 FloatCallCount = 0;
	int32 DoubleCallCount = 0;

	UFUNCTION()
	void TakeFloat(float InValue) { LastFloat = InValue; ++FloatCallCount; }

	UFUNCTION()
	void TakeDouble(double InValue) { LastDouble = InValue; ++DoubleCallCount; }
};

/** Counts long presses dispatched to a widget, and remembers which pointer carried each one. */
UCLASS()
class UDreamLongPressCounter : public UDreamUIBehaviour, public IDreamPointerLongPressInterface
{
	GENERATED_BODY()

public:
	int32 LongPressCount = 0;
	int32 LastPointerID = INDEX_NONE;

	virtual bool OnPointerLongPress_Implementation(UDreamPointerEventData* EventData) override
	{
		++LongPressCount;
		LastPointerID = EventData != nullptr ? EventData->PointerID : INDEX_NONE;
		return true;
	}
};

/** Counts pinches and swipes, keeping the last of each so a test can read what was recognized. */
UCLASS()
class UDreamGestureCounter : public UDreamUIBehaviour, public IDreamPointerGestureInterface
{
	GENERATED_BODY()

public:
	int32 PinchCount = 0;
	int32 SwipeCount = 0;
	float LastPinchScale = 1.0f;
	float LastPinchDistanceDelta = 0.0f;
	EDreamUINavigationDirection LastSwipeDirection = EDreamUINavigationDirection::None;
	FVector2D LastSwipeDelta = FVector2D::ZeroVector;

	virtual bool OnPointerPinch_Implementation(UDreamGestureEventData* EventData) override
	{
		++PinchCount;
		if (EventData != nullptr)
		{
			LastPinchScale = EventData->PinchScale;
			LastPinchDistanceDelta = EventData->PinchDistanceDelta;
		}
		return true;
	}
	virtual bool OnPointerSwipe_Implementation(UDreamGestureEventData* EventData) override
	{
		++SwipeCount;
		if (EventData != nullptr)
		{
			LastSwipeDirection = EventData->SwipeDirection;
			LastSwipeDelta = EventData->SwipeDelta;
		}
		return true;
	}
};

/**
 * An action trigger whose lifecycle a test can drive.
 *
 * OnEnable and OnInteractableChanged are called by the UI manager and by the widget's own interactable
 * cache, neither of which runs in a bare test world -- and they are exactly what this class's contract
 * is about, so a test has to be able to say "now the widget went live" out loud.
 */
UCLASS()
class UDreamTestActionTrigger : public UDreamUIActionTrigger
{
	GENERATED_BODY()

public:
	void ForceEnable() { OnEnable(); }
	void ForceDisable() { OnDisable(); }
	void ForceInteractableChanged(bool bInteractable) { OnInteractableChanged(bInteractable); }
};
