// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "DreamFocusReentryTestTypes.generated.h"

enum class EDreamFocusReentryCallback : uint8
{
	Select,
	Deselect,
	Received
};

/** A focus listener that uses the public widget focus API once from inside a real dispatched callback. */
UCLASS()
class UDreamFocusReentryProbe : public UDreamUIBehaviour, public IDreamPointerSelectDeselectInterface
{
	GENERATED_BODY()

public:
	EDreamFocusReentryCallback Trigger = EDreamFocusReentryCallback::Select;
	TFunction<void()> Action;
	int32 MutationCount = 0;
	int32 SelectCount = 0;
	int32 DeselectCount = 0;
	int32 ReceivedCount = 0;
	int32 LostCount = 0;
	bool bEveryReceivedHadFocus = true;

	virtual bool OnPointerSelect_Implementation(UDreamBaseEventData* EventData) override
	{
		++SelectCount;
		RunAction(EDreamFocusReentryCallback::Select);
		return false;
	}

	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData) override
	{
		++DeselectCount;
		RunAction(EDreamFocusReentryCallback::Deselect);
		return false;
	}

	UFUNCTION()
	void OnReceived(int32 InUserIndex, int32 InPointerId)
	{
		++ReceivedCount;
		bEveryReceivedHadFocus = bEveryReceivedHadFocus && GetWidget() != nullptr && GetWidget()->HasFocus(InUserIndex, InPointerId);
		RunAction(EDreamFocusReentryCallback::Received);
	}

	UFUNCTION()
	void OnLost(int32 InUserIndex, int32 InPointerId) { ++LostCount; }

private:
	void RunAction(EDreamFocusReentryCallback InCallback)
	{
		if (Trigger == InCallback && Action)
		{
			TFunction<void()> RunOnce = MoveTemp(Action);
			Action = nullptr;
			++MutationCount;
			RunOnce();
		}
	}
};
