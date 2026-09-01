// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamUMGWidgetInteraction.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWorldContext.h"
#include "Extensions/DreamUMGWidget.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Application/SlateApplication.h"
#include "Event/DreamBaseRaycaster.h"

#define LOCTEXT_NAMESPACE "UIWidgetInteraction"

UDreamUMGWidgetInteractionManager* UDreamUMGWidgetInteractionManager::Instance = nullptr;

UDreamUMGWidgetInteraction::UDreamUMGWidgetInteraction()
{
	
}

UDreamUMGWidgetInteractionManager::FInteractionContainer* UDreamUMGWidgetInteraction::FindEnrolledInteractions()
{
	if (UDreamUMGWidgetInteractionManager::Instance == nullptr)
	{
		return nullptr;
	}
	return UDreamUMGWidgetInteractionManager::Instance->MapVirtualUserIndexToInteraction.Find(VirtualUserIndex);
}

bool UDreamUMGWidgetInteraction::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	if (CurrentPointerEventData == nullptr)
	{
		CurrentPointerEventData = EventData;

		// Claiming the shared virtual user is only meaningful for a component that has one to claim.
		// Un-enrolled, there is no cursor to contend for and nothing downstream would do anything
		// with the tick either -- SimulatePointerMovement refuses on its first line without a
		// virtual user -- so the hover is recorded and the arbitration is skipped entirely. Both
		// halves of this used to be unconditional, which is why a hover on a build with no Slate
		// application was fatal twice over: a null Instance, and then a key the map never got.
		if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions())
		{
			if (Interactions->CurrentInteraction == nullptr)
			{
				Interactions->CurrentInteraction = this;
				this->SetCanExecuteTick(true);//hover in, enable update
			}
		}
	}
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	if (CurrentPointerEventData == EventData)
	{
		CurrentPointerEventData = nullptr;

		if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions())
		{
			if (Interactions->CurrentInteraction == this)
			{
				SimulatePointerMovement();//pointer exit;
				Interactions->CurrentInteraction = nullptr;
				this->SetCanExecuteTick(false);//hover out, disable update
			}
		}
	}
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	FKey PressKey;
	switch (EventData->MouseButtonType)
	{
	case EDreamUIMouseButtonType::Left:
		PressKey = EKeys::LeftMouseButton;
		break;
	case EDreamUIMouseButtonType::Middle:
		PressKey = EKeys::MiddleMouseButton;
		break;
	case EDreamUIMouseButtonType::Right:
		PressKey = EKeys::RightMouseButton;
		break;
	}
	if (PressKey.IsValid())
	{
		PressPointerKey(PressKey);
	}
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	FKey ReleaseKey;
	switch (EventData->MouseButtonType)
	{
	case EDreamUIMouseButtonType::Left:
		ReleaseKey = EKeys::LeftMouseButton;
		break;
	case EDreamUIMouseButtonType::Middle:
		ReleaseKey = EKeys::MiddleMouseButton;
		break;
	case EDreamUIMouseButtonType::Right:
		ReleaseKey = EKeys::RightMouseButton;
		break;
	}
	if (ReleaseKey.IsValid())
	{
		ReleasePointerKey(ReleaseKey);
	}
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	auto inAxisValue = EventData->ScrollAxisValue;
	ScrollWheel(inAxisValue.Y);
	return bAllowEventBubbleUp;
}





void UDreamUMGWidgetInteraction::Awake()
{
	Super::Awake();

	// Only create another user in a real world. FindOrCreateVirtualUser changes focus, so a preview
	// world is excluded -- and so, now, is having no world at all, which is the state of every
	// widget in an authoring tree and of every widget a headless test builds.
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (FSlateApplication::IsInitialized() && World != nullptr && !World->IsPreviewWorld())
	{
		if (!VirtualUser.IsValid())
		{
			VirtualUser = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualUserIndex);
			// The manager comes into being here, as part of enrolling, and not a line earlier. See
			// its class comment: a component that reaches Awake without getting this far has nothing
			// to arbitrate, and letting it create the manager anyway is what made the manager's
			// existence and its contents two separate facts.
			if (UDreamUMGWidgetInteractionManager::Instance == nullptr)
			{
				UDreamUMGWidgetInteractionManager::Instance = NewObject<UDreamUMGWidgetInteractionManager>();
				UDreamUMGWidgetInteractionManager::Instance->AddToRoot();
			}
			Helper = UDreamUMGWidgetInteractionManager::Instance;
			auto& Interactions = UDreamUMGWidgetInteractionManager::Instance->MapVirtualUserIndexToInteraction.FindOrAdd(VirtualUserIndex);
			Interactions.AllInteractions.Add(this);
		}
	}
	WidgetComponent = Cast<UDreamUMGWidget>(GetWidget()->GetVisual());
	this->SetCanExecuteTick(false);//disable update by default
}

void UDreamUMGWidgetInteraction::OnDestroy()
{
	Super::OnDestroy();

	if (VirtualUser.IsValid())
	{
		// Slate can be gone before the component is, on a shutdown that tears the application down
		// first, so handing the user back is conditional while forgetting it is not.
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterUser(VirtualUser->GetUserIndex());
		}
		VirtualUser.Reset();
	}

	// A component that never enrolled has no manager to leave and, more to the point, no standing to
	// destroy one. This early return is the other half of the fix in Awake: while un-enrolled
	// components fell through to the teardown below, the first one destroyed found the map empty and
	// took the Instance with it, leaving the enrolled components that were still hovering to
	// dereference a null static.
	if (UDreamUMGWidgetInteractionManager::Instance == nullptr)
	{
		Helper = nullptr;
		return;
	}

	auto& MapVirtualUserIndexToInteraction = UDreamUMGWidgetInteractionManager::Instance->MapVirtualUserIndexToInteraction;
	if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = MapVirtualUserIndexToInteraction.Find(VirtualUserIndex))
	{
		// Giving the shared cursor back matters more than leaving the list tidy. CurrentInteraction
		// is exactly what the next component's hover tests for null, so a destroyed component still
		// named there does not leak an entry -- it makes the whole virtual user index permanently
		// deaf, because no later hover can ever claim a cursor that is already spoken for.
		if (Interactions->CurrentInteraction == this)
		{
			Interactions->CurrentInteraction = nullptr;
		}
		Interactions->AllInteractions.Remove(this);
		if (Interactions->AllInteractions.Num() == 0)
		{
			MapVirtualUserIndexToInteraction.Remove(VirtualUserIndex);
		}
	}

	if (MapVirtualUserIndexToInteraction.Num() == 0)
	{
		// RemoveFromRoot before ConditionalBeginDestroy, because the root set is what has kept this
		// object alive since Awake. Marking it for destruction while it is still rooted leaves the
		// root set holding an object garbage collection is not allowed to collect.
		UDreamUMGWidgetInteractionManager::Instance->RemoveFromRoot();
		UDreamUMGWidgetInteractionManager::Instance->ConditionalBeginDestroy();
		UDreamUMGWidgetInteractionManager::Instance = nullptr;
	}
	Helper = nullptr;
}

void UDreamUMGWidgetInteraction::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	SimulatePointerMovement();
}

bool UDreamUMGWidgetInteraction::CanSendInput()
{
	return FSlateApplication::IsInitialized() && VirtualUser.IsValid() && WidgetComponent != nullptr;
}

void UDreamUMGWidgetInteraction::SetFocus(UWidget* FocusWidget)
{
	if (VirtualUser.IsValid())
	{
		FSlateApplication::Get().SetUserFocus(VirtualUser->GetUserIndex(), FocusWidget->GetCachedWidget(), EFocusCause::SetDirectly);
	}
}

bool UDreamUMGWidgetInteraction::CanInteractWithComponent(UDreamUMGWidget* Component) const
{
	bool bCanInteract = false;

	if (Component)
	{
		// No world reads as "cannot interact" rather than as "not paused". Nothing here has a caller
		// today, but the naked GetWorld() that used to be on this line is precisely the shape that
		// gets copied into one, and a component with no world could not send the input anyway.
		const UWorld* World = DreamUI::GetWorldSafe(this);
		bCanInteract = World != nullptr && !World->IsPaused()
		//|| Component->PrimaryComponentTick.bTickEvenWhenPaused;
		;
	}

	return bCanInteract;
}

FWidgetPath UDreamUMGWidgetInteraction::DetermineWidgetUnderPointer()
{
	FWidgetPath WidgetPathUnderPointer;

	bIsHoveredWidgetInteractable = false;
	bIsHoveredWidgetFocusable = false;
	bIsHoveredWidgetHitTestVisible = false;

	LastLocalHitLocation = LocalHitLocation;
	FWidgetTraceResult TraceResult;
	if (CurrentPointerEventData != nullptr && CurrentPointerEventData->Raycaster != nullptr)
	{
		auto RayOrigin = CurrentPointerEventData->Raycaster->GetRayOrigin();
		auto RayDirection = CurrentPointerEventData->Raycaster->GetRayDirection();
		auto RayEnd = RayOrigin + RayDirection * CurrentPointerEventData->Raycaster->GetRayLength();

		WidgetComponent->GetLocalHitLocation(CurrentPointerEventData->FaceIndex, CurrentPointerEventData->WorldPoint, RayOrigin, RayEnd, TraceResult.LocalHitLocation);
		TraceResult.HitWidgetPath = FWidgetPath(WidgetComponent->GetHitWidgetPath(TraceResult.LocalHitLocation, /*bIgnoreEnabledStatus*/ false));

		LocalHitLocation = TraceResult.LocalHitLocation;
	}
	WidgetPathUnderPointer = TraceResult.HitWidgetPath;

	WidgetComponent->RequestRenderUpdate();

	if (WidgetPathUnderPointer.IsValid())
	{
		const FArrangedChildren::FArrangedWidgetArray& AllArrangedWidgets = WidgetPathUnderPointer.Widgets.GetInternalArray();
		for (const FArrangedWidget& ArrangedWidget : AllArrangedWidgets)
		{
			const TSharedRef<SWidget>& Widget = ArrangedWidget.Widget;
			if (Widget->IsEnabled())
			{
				if (Widget->IsInteractable())
				{
					bIsHoveredWidgetInteractable = true;
				}

				if (Widget->SupportsKeyboardFocus())
				{
					bIsHoveredWidgetFocusable = true;
				}
			}

			if (Widget->GetVisibility().IsHitTestVisible())
			{
				bIsHoveredWidgetHitTestVisible = true;
			}
		}
	}

	return WidgetPathUnderPointer;
}

void UDreamUMGWidgetInteraction::SimulatePointerMovement()
{
	if (!CanSendInput())
	{
		return;
	}

	FWidgetPath WidgetPathUnderFinger = DetermineWidgetUnderPointer();
	if (CurrentPointerEventData != nullptr)
	{
		PrevPointerIndex = CurrentPointerEventData->PointerID;
	}
	if (PrevPointerIndex >= 0)
	{
		FPointerEvent PointerEvent(
			VirtualUser->GetUserIndex(),
			(uint32)PrevPointerIndex,
			LocalHitLocation,
			LastLocalHitLocation,
			PressedKeys,
			FKey(),
			0.0f,
			ModifierKeys);

		if (WidgetPathUnderFinger.IsValid())
		{
			check(WidgetComponent);
			LastWidgetPath = WidgetPathUnderFinger;
			FSlateApplication::Get().RoutePointerMoveEvent(WidgetPathUnderFinger, PointerEvent, false);
		}
		else
		{
			FWidgetPath EmptyWidgetPath;
			FSlateApplication::Get().RoutePointerMoveEvent(EmptyWidgetPath, PointerEvent, false);

			LastWidgetPath = FWeakWidgetPath();
		}
	}
}

void UDreamUMGWidgetInteraction::PressPointerKey(FKey Key)
{
	if (!CanSendInput())
	{
		return;
	}

	if (PressedKeys.Contains(Key))
	{
		return;
	}

	PressedKeys.Add(Key);

	if (!LastWidgetPath.IsValid())
	{
		// If the cached widget path isn't valid, attempt to find a valid widget since we might have received a touch input
		LastWidgetPath = DetermineWidgetUnderPointer();
	}

	FWidgetPath WidgetPathUnderFinger = LastWidgetPath.ToWidgetPath();
	if (PrevPointerIndex >= 0)
	{
		FPointerEvent PointerEvent;
		if (Key.IsTouch())
		{
			PointerEvent = FPointerEvent(
				VirtualUser->GetUserIndex(),
				(uint32)PrevPointerIndex,
				LocalHitLocation,
				LastLocalHitLocation,
				1.0f,
				false);

		}
		else
		{
			PointerEvent = FPointerEvent(
				VirtualUser->GetUserIndex(),
				(uint32)PrevPointerIndex,
				LocalHitLocation,
				LastLocalHitLocation,
				PressedKeys,
				Key,
				0.0f,
				ModifierKeys);
		}


		FReply Reply = FSlateApplication::Get().RoutePointerDownEvent(WidgetPathUnderFinger, PointerEvent);

		// @TODO Something about double click, expose directly, or automatically do it if key press happens within
		// the double click timeframe?
		//Reply = FSlateApplication::Get().RoutePointerDoubleClickEvent( WidgetPathUnderFinger, PointerEvent );
	}
}

void UDreamUMGWidgetInteraction::ReleasePointerKey(FKey Key)
{
	if (!CanSendInput())
	{
		return;
	}

	if (!PressedKeys.Contains(Key))
	{
		return;
	}

	PressedKeys.Remove(Key);

	FWidgetPath WidgetPathUnderFinger = LastWidgetPath.ToWidgetPath();
	// Need to clear the widget path for cases where the component isn't ticking/clearing itself.
	LastWidgetPath = FWeakWidgetPath();
	if (PrevPointerIndex >= 0)
	{
		FPointerEvent PointerEvent;
		if (Key.IsTouch())
		{
			PointerEvent = FPointerEvent(
				VirtualUser->GetUserIndex(),
				(uint32)PrevPointerIndex,
				LocalHitLocation,
				LastLocalHitLocation,
				1.0f,
				false);
		}
		else
		{
			PointerEvent = FPointerEvent(
				VirtualUser->GetUserIndex(),
				(uint32)PrevPointerIndex,
				LocalHitLocation,
				LastLocalHitLocation,
				PressedKeys,
				Key,
				0.0f,
				ModifierKeys);
		}

		FReply Reply = FSlateApplication::Get().RoutePointerUpEvent(WidgetPathUnderFinger, PointerEvent);
	}
}

bool UDreamUMGWidgetInteraction::PressKey(FKey Key, bool bRepeat)
{
	if (!CanSendInput())
	{
		return false;
	}

	bool bHasKeyCode, bHasCharCode;
	uint32 KeyCode, CharCode;
	GetKeyAndCharCodes(Key, bHasKeyCode, KeyCode, bHasCharCode, CharCode);

	FKeyEvent KeyEvent(Key, ModifierKeys, VirtualUser->GetUserIndex(), bRepeat, CharCode, KeyCode);
	bool bDownResult = FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);

	bool bKeyCharResult = false;
	if (bHasCharCode)
	{
		FCharacterEvent CharacterEvent(CharCode, ModifierKeys, VirtualUser->GetUserIndex(), bRepeat);
		bKeyCharResult = FSlateApplication::Get().ProcessKeyCharEvent(CharacterEvent);
	}

	return bDownResult || bKeyCharResult;
}

bool UDreamUMGWidgetInteraction::ReleaseKey(FKey Key)
{
	if (!CanSendInput())
	{
		return false;
	}

	bool bHasKeyCode, bHasCharCode;
	uint32 KeyCode, CharCode;
	GetKeyAndCharCodes(Key, bHasKeyCode, KeyCode, bHasCharCode, CharCode);

	FKeyEvent KeyEvent(Key, ModifierKeys, VirtualUser->GetUserIndex(), false, CharCode, KeyCode);
	return FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);
}

void UDreamUMGWidgetInteraction::GetKeyAndCharCodes(const FKey& Key, bool& bHasKeyCode, uint32& KeyCode, bool& bHasCharCode, uint32& CharCode)
{
	const uint32* KeyCodePtr;
	const uint32* CharCodePtr;
	FInputKeyManager::Get().GetCodesFromKey(Key, KeyCodePtr, CharCodePtr);

	bHasKeyCode = KeyCodePtr ? true : false;
	bHasCharCode = CharCodePtr ? true : false;

	KeyCode = KeyCodePtr ? *KeyCodePtr : 0;
	CharCode = CharCodePtr ? *CharCodePtr : 0;

	// These special keys are not handled by the platform layer, and while not printable
	// have character mappings that several widgets look for, since the hardware sends them.
	if (CharCodePtr == nullptr)
	{
		if (Key == EKeys::Tab)
		{
			CharCode = '\t';
			bHasCharCode = true;
		}
		else if (Key == EKeys::BackSpace)
		{
			CharCode = '\b';
			bHasCharCode = true;
		}
		else if (Key == EKeys::Enter)
		{
			CharCode = '\n';
			bHasCharCode = true;
		}
	}
}

bool UDreamUMGWidgetInteraction::PressAndReleaseKey(FKey Key)
{
	const bool PressResult = PressKey(Key, false);
	const bool ReleaseResult = ReleaseKey(Key);

	return PressResult || ReleaseResult;
}

bool UDreamUMGWidgetInteraction::SendKeyChar(FString Characters, bool bRepeat)
{
	if (!CanSendInput())
	{
		return false;
	}

	bool bProcessResult = false;

	for (int32 CharIndex = 0; CharIndex < Characters.Len(); CharIndex++)
	{
		TCHAR CharKey = Characters[CharIndex];

		FCharacterEvent CharacterEvent(CharKey, ModifierKeys, VirtualUser->GetUserIndex(), bRepeat);
		bProcessResult |= FSlateApplication::Get().ProcessKeyCharEvent(CharacterEvent);
	}

	return bProcessResult;
}

void UDreamUMGWidgetInteraction::ScrollWheel(float ScrollDelta)
{
	if (!CanSendInput())
	{
		return;
	}

	if (PrevPointerIndex >= 0)
	{
		FWidgetPath WidgetPathUnderFinger = LastWidgetPath.ToWidgetPath();
		FPointerEvent MouseWheelEvent(
			VirtualUser->GetUserIndex(),
			(uint32)PrevPointerIndex,
			LocalHitLocation,
			LastLocalHitLocation,
			PressedKeys,
			EKeys::MouseWheelAxis,
			ScrollDelta,
			ModifierKeys);

		FSlateApplication::Get().RouteMouseWheelOrGestureEvent(WidgetPathUnderFinger, MouseWheelEvent, nullptr);
	}
}

bool UDreamUMGWidgetInteraction::IsOverInteractableWidget() const
{
	return bIsHoveredWidgetInteractable;
}

bool UDreamUMGWidgetInteraction::IsOverFocusableWidget() const
{
	return bIsHoveredWidgetFocusable;
}

bool UDreamUMGWidgetInteraction::IsOverHitTestVisibleWidget() const
{
	return bIsHoveredWidgetHitTestVisible;
}

const FWeakWidgetPath& UDreamUMGWidgetInteraction::GetHoveredWidgetPath() const
{
	return LastWidgetPath;
}

FVector2D UDreamUMGWidgetInteraction::Get2DHitLocation() const
{
	return LocalHitLocation;
}
#undef LOCTEXT_NAMESPACE