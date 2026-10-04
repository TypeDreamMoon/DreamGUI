// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamGUI.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Core/DreamUIWorldContext.h"

int32 UDreamStandaloneInputModule::GetTouchPointerID(int32 InTouchID)
{
	return DreamUIPointerIds::ForTouch(InTouchID);
}

void UDreamStandaloneInputModule::InputScroll(const FVector2D& InAxisValue, int InPointerID)
{
	UDreamUIInputUser* User = GetInputUser();
	if (User == nullptr)return;

	UDreamPointerEventData* EventData = User->GetPointerEventData(InPointerID, true);
	if (EventData == nullptr)return;
	if (!InAxisValue.IsZero())
	{
		// Turning a wheel is pointer input, and saying so matters: InputType is a sticky mode bit, and a pointer left in
		// Navigation mode is not traced, so the wheel would go to whatever navigation last left behind.
		User->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
	}
	// Dispatched on the next frame, after the pointer has been traced there: the wheel goes to what the pointer is
	// over by then, not to what it was over before it moved.
	User->QueuePointerScroll(InPointerID, InAxisValue);
}

void UDreamStandaloneInputModule::InputTrigger(const FVector& InMousePosition, bool InTriggerPress, EDreamUIMouseButtonType InMouseButtonType)
{
	CommonInputTrigger(InMousePosition, InTriggerPress, DreamUIPointerIds::Mouse, InMouseButtonType);
}

void UDreamStandaloneInputModule::GetMousePosition(FVector2D& OutMousePos)const
{
	// Written before any early-out: with no viewport to ask, the mouse is over none, and (-1,-1) is where
	// FSceneViewport itself parks a cursor that is over no part of it. (0,0) is the top-left pixel -- the presets push
	// this answer into pointer 0 every frame, which hovered, and dropped a drag onto, whatever was drawn there.
	OutMousePos = FVector2D(-1.0, -1.0);
	if (bOverrideMousePosition)
	{
		OutMousePos = OverridePointerPosition;
		return;
	}
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (World == nullptr)return;
	UGameViewportClient* Client = World->GetGameViewport();
	if (Client == nullptr || Client->GetMousePosition(OutMousePos))
	{
		return;
	}
	// The client gives no position for a cursor its viewport's cache puts off it -- (-1,-1) once the cursor has left
	// the viewport or the last finger has lifted (SceneViewport.cpp, OnMouseLeave and OnTouchEnded), left of or above
	// it while a drag the viewport captured goes past an edge -- nor for any position while no mouse is attached. The
	// cache is the viewport's own reckoning of where the cursor is, and that is the answer: off the viewport, so over
	// nothing, wherever the cursor is not on it.
	OutMousePos = FVector2D(-1.0, -1.0);
	if (FViewport* ClientViewport = Client->Viewport)
	{
		FIntPoint CachedCursor = FIntPoint(-1, -1);
		ClientViewport->GetMousePos(CachedCursor);
		OutMousePos = FVector2D(CachedCursor);
	}
}

void UDreamStandaloneInputModule::SetOverrideMousePosition(bool bInOverride)
{
	if (bOverrideMousePosition == bInOverride)
	{
		return;
	}
	bOverrideMousePosition = bInOverride;
	if (bInOverride)
	{
		// Start where the real mouse is, so the substituted pointer does not teleport.
		FVector2D Current = FVector2D::ZeroVector;
		const UWorld* World = DreamUI::GetWorldSafe(this);
		if (auto Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
		{
			Viewport->GetMousePosition(Current);
		}
		OverridePointerPosition = Current;
	}
}

void UDreamStandaloneInputModule::SetOverridePointerPosition(const FVector2D& InPosition)
{
	OverridePointerPosition = InPosition;
	if (bOverrideMousePosition)
	{
		InputMouseMove(FVector(InPosition.X, InPosition.Y, 0.0f));
	}
}

void UDreamStandaloneInputModule::CommonInputTrigger(const FVector& InPointerPosition, bool InTriggerPress,
	int InPointerID, EDreamUIMouseButtonType InMouseButtonType, bool bInIsTouch)
{
	UDreamUIInputUser* User = GetInputUser();
	if (User == nullptr)return;
	UDreamPointerEventData* EventData = User->GetPointerEventData(InPointerID, true);
	// A press is pointer input. The queue is no longer thrown away when a pointer changes mode: a press and the
	// release that follows it in the same frame both used to vanish if the pointer had been navigating.
	User->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
	User->QueuePointerButton(InPointerID, InPointerPosition, InTriggerPress, InMouseButtonType, bInIsTouch);
}

void UDreamStandaloneInputModule::InputMouseMove(const FVector& InMousePosition)
{
	// This runs from a bound axis delegate that keeps firing while the world tears down; no player, no move.
	UDreamUIInputUser* User = GetInputUser();
	if (User == nullptr)return;

	User->MovePointer(DreamUIPointerIds::Mouse, InMousePosition);
}

void UDreamStandaloneInputModule::InputTouchTrigger(bool InTouchPress, int InTouchID, const FVector& InTouchPointPosition)
{
	CommonInputTrigger(InTouchPointPosition, InTouchPress, GetTouchPointerID(InTouchID), EDreamUIMouseButtonType::Left, true);
}

void UDreamStandaloneInputModule::InputTouchMoved(int InTouchID, const FVector& InTouchPointPosition)
{
	UDreamUIInputUser* User = GetInputUser();
	if (User == nullptr)return;

	UDreamPointerEventData* EventData = User->GetPointerEventData(GetTouchPointerID(InTouchID), true);
	if (EventData == nullptr)return;
	// Same reason as InputMouseMove: a moving touch is pointer input and has to say so.
	User->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
	// Through the player, as the mouse's moves are: a finger that really moved lets its hover take the navigation highlight.
	User->MovePointer(GetTouchPointerID(InTouchID), InTouchPointPosition);
}

void UDreamStandaloneInputModule::InputNavigation(EDreamUINavigationDirection InDirection, bool InPressOrRelease, int InPointerID)
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->InputNavigation(InDirection, InPressOrRelease, InPointerID);
	}
}

void UDreamStandaloneInputModule::InputTriggerForNavigation(bool InTriggerPress, int InPointerID)
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->InputTriggerForNavigation(InTriggerPress, InPointerID);
	}
}
