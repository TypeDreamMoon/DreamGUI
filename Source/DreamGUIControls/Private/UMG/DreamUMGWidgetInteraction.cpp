// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "UMG/DreamUMGWidgetInteraction.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWorldContext.h"
#include "UMG/DreamUMGWidget.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Application/SlateApplication.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamUIInputTypes.h"

#define LOCTEXT_NAMESPACE "UIWidgetInteraction"

namespace DreamUMGWidgetInteractionLocal
{
	/** Whether a pointer is a finger Slate can carry as a touch of its own: one of the ten it numbers. */
	bool IsForwardableTouch(int32 InPointerId)
	{
		return DreamUIPointerIds::IsTouch(InPointerId)
			&& DreamUIPointerIds::GetFingerIndex(InPointerId) < static_cast<int32>(ETouchIndex::CursorPointerIndex);
	}

	/**
	 * The index a DreamGUI pointer has on the virtual Slate user. A finger is its own finger, 0 to 9, as Slate's
	 * own touch input numbers them. The mouse is Slate's cursor, which sits past the fingers: it used to be index 0,
	 * the first finger's, so a hover left that index's position behind and the first finger's touch then started
	 * over a pointer Slate thought was still down. Any other pointer -- one a project or a test made up -- keeps its
	 * id, which starts past both.
	 */
	uint32 SlatePointerIndexOf(int32 InPointerId)
	{
		if (IsForwardableTouch(InPointerId))
		{
			return static_cast<uint32>(DreamUIPointerIds::GetFingerIndex(InPointerId));
		}
		if (InPointerId == DreamUIPointerIds::Mouse)
		{
			return static_cast<uint32>(ETouchIndex::CursorPointerIndex);
		}
		return static_cast<uint32>(FMath::Max(0, InPointerId));
	}

	/** The mouse button a pointer pressed or let go of, as the key Slate knows it by. */
	FKey MouseKeyOf(const UDreamPointerEventData* InEventData)
	{
		switch (InEventData->MouseButtonType)
		{
		case EDreamUIMouseButtonType::Left:
			return EKeys::LeftMouseButton;
		case EDreamUIMouseButtonType::Middle:
			return EKeys::MiddleMouseButton;
		case EDreamUIMouseButtonType::Right:
			return EKeys::RightMouseButton;
		default:
			// A project's own buttons have no key on the Slate side, so they press nothing there.
			break;
		}
		return FKey();
	}
}

UDreamUMGWidgetInteractionManager* UDreamUMGWidgetInteractionManager::Get(const UObject* InWorldContext)
{
	const UWorld* World = DreamUI::GetWorldSafe(InWorldContext);
	return World != nullptr ? World->GetSubsystem<UDreamUMGWidgetInteractionManager>() : nullptr;
}

UDreamUMGWidgetInteraction::UDreamUMGWidgetInteraction()
{
	
}

UDreamUMGWidgetInteractionManager::FInteractionContainer* UDreamUMGWidgetInteraction::FindEnrolledInteractions()
{
	UDreamUMGWidgetInteractionManager* Manager = Helper.Get();
	return Manager != nullptr ? Manager->MapVirtualUserIndexToInteraction.Find(VirtualUserIndex) : nullptr;
}

UDreamUMGWidgetInteraction::FForwardedPointer& UDreamUMGWidgetInteraction::TrackPointer(UDreamPointerEventData* EventData)
{
	using namespace DreamUMGWidgetInteractionLocal;
	FForwardedPointer& Pointer = ForwardedPointers.FindOrAdd(EventData->PointerID);
	if (Pointer.Pointer.Get() != EventData)
	{
		// A pointer this id did not stand for before -- the first sight of it, or a finger put down again after
		// its last one was lifted and retired. Whatever the old one left is not this one's.
		Pointer = FForwardedPointer();
		Pointer.Pointer = EventData;
		Pointer.bTouch = IsForwardableTouch(EventData->PointerID);
		Pointer.SlatePointerIndex = SlatePointerIndexOf(EventData->PointerID);
	}
	if (!CurrentPointerEventData.IsValid())
	{
		CurrentPointerEventData = EventData;
	}
	return Pointer;
}

UDreamUMGWidgetInteraction::FForwardedPointer* UDreamUMGWidgetInteraction::FindPrimaryPointer()
{
	const UDreamPointerEventData* Primary = CurrentPointerEventData.Get();
	FForwardedPointer* Pointer = Primary != nullptr ? ForwardedPointers.Find(Primary->PointerID) : nullptr;
	return Pointer != nullptr && Pointer->Pointer.Get() == Primary ? Pointer : nullptr;
}

bool UDreamUMGWidgetInteraction::HoldsVirtualCursor()
{
	const UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions();
	return Interactions != nullptr && Interactions->CurrentInteraction.Get() == this;
}

void UDreamUMGWidgetInteraction::UpdateTicking()
{
	// The cursor is the mouse's, and a component holding it forwards that pointer every frame. A finger is
	// nobody else's -- it has its own index on the virtual user -- so one that is down here is forwarded whoever
	// holds the cursor.
	bool bWantsTick = HoldsVirtualCursor();
	for (const TPair<int32, FForwardedPointer>& Entry : ForwardedPointers)
	{
		if (bWantsTick)
		{
			break;
		}
		bWantsTick = Entry.Value.bTouchDown;
	}
	this->SetCanExecuteTick(bWantsTick);
}

bool UDreamUMGWidgetInteraction::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData == nullptr)
	{
		return bAllowEventBubbleUp;
	}
	FForwardedPointer& Pointer = TrackPointer(EventData);
	Pointer.bHovering = true;
	// Back over this surface before letting go of a press that left it, or a hover starting afresh: either
	// way no exit is owed any more.
	Pointer.bExitPendingRelease = false;

	// Claiming the shared virtual user is only meaningful for a component that has one to claim.
	// Un-enrolled, there is no cursor to contend for and nothing downstream would do anything
	// with the tick either -- SimulatePointerMovement refuses on its first line without a
	// virtual user -- so the hover is recorded and the arbitration is skipped entirely. Both
	// halves of this used to be unconditional, which is why a hover on a build with no Slate
	// application was fatal twice over: a null Instance, and then a key the map never got.
	if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions())
	{
		if (!Interactions->CurrentInteraction.IsValid())
		{
			Interactions->CurrentInteraction = this;
		}
	}
	UpdateTicking();
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	FForwardedPointer* Pointer = EventData != nullptr ? ForwardedPointers.Find(EventData->PointerID) : nullptr;
	if (Pointer != nullptr && Pointer->Pointer.Get() == EventData && Pointer->bPressing)
	{
		// Not while a press made here is held. A press that travels past the drag threshold is a drag,
		// and the event system takes a dragged widget out of its own hit test -- so this surface hears
		// an exit a few pixels into every drag that starts on it. Acting on that gave the cursor back and
		// stopped forwarding moves, and a UMG slider's thumb froze where the drag began, though the
		// thumb had captured the pointer on its press and expects every move until the release. The
		// exit is acted on at the release instead.
		Pointer->bExitPendingRelease = true;
		return bAllowEventBubbleUp;
	}
	EndHover(EventData);
	return bAllowEventBubbleUp;
}
void UDreamUMGWidgetInteraction::EndHover(UDreamPointerEventData* EventData)
{
	FForwardedPointer* Pointer = EventData != nullptr ? ForwardedPointers.Find(EventData->PointerID) : nullptr;
	if (Pointer == nullptr || Pointer->Pointer.Get() != EventData)
	{
		return;
	}
	const int32 PointerId = EventData->PointerID;
	if (CanSendInput())
	{
		if (Pointer->bTouchDown)
		{
			// A finger gone without its release reaching this surface -- its player retired it, say. Lifted on
			// the Slate side as well, or the virtual user keeps an active touch the next finger of that number
			// starts over.
			ForwardPointerKey(*Pointer, EKeys::TouchKeys[Pointer->SlatePointerIndex], /*bInPressed*/false, /*bInEndsPress*/true);
		}
		else if (!Pointer->bTouch && HoldsVirtualCursor())
		{
			// The cursor leaving, which Slate learns from a move over nothing of the widget's: the widgets it was
			// over hear their leave.
			const FPointerEvent PointerEvent(VirtualUser->GetUserIndex(), Pointer->SlatePointerIndex,
				Pointer->LocalHitLocation, Pointer->LastLocalHitLocation, Pointer->PressedKeys, FKey(), 0.0f, ModifierKeys);
			LastWidgetPath = FWeakWidgetPath();
			Pointer->LastWidgetPath = FWeakWidgetPath();
			SendPointerMove(FWidgetPath(), PointerEvent);
		}
	}
	// Looked up again: handing Slate an event can run anything, this surface's other pointers included.
	ForwardedPointers.Remove(PointerId);
	if (!CurrentPointerEventData.IsValid() || CurrentPointerEventData.Get() == EventData)
	{
		// The calls that act for "the" pointer move on to whichever one is still here.
		CurrentPointerEventData.Reset();
		for (const TPair<int32, FForwardedPointer>& Entry : ForwardedPointers)
		{
			if (Entry.Value.Pointer.IsValid())
			{
				CurrentPointerEventData = Entry.Value.Pointer;
				break;
			}
		}
	}
	if (ForwardedPointers.Num() == 0)
	{
		if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions())
		{
			if (Interactions->CurrentInteraction.Get() == this)
			{
				// Nothing is over this surface any more, so the cursor it shares goes back to whoever hovers next.
				Interactions->CurrentInteraction.Reset();
			}
		}
	}
	UpdateTicking();
}
bool UDreamUMGWidgetInteraction::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData == nullptr)
	{
		return bAllowEventBubbleUp;
	}
	FForwardedPointer& Pointer = TrackPointer(EventData);
	// A finger presses as itself; the mouse presses the button it was pressed with.
	const FKey PressKey = Pointer.bTouch
		? EKeys::TouchKeys[Pointer.SlatePointerIndex]
		: DreamUMGWidgetInteractionLocal::MouseKeyOf(EventData);
	if (PressKey.IsValid())
	{
		// Whether or not Slate takes the key: what this records is the press on this surface, which is what
		// keeps the hover through a drag -- see OnPointerExit. Before the send, which is the last thing done.
		Pointer.bPressing = true;
		// Traced here, at the press, not left to the next tick: the pointer's index used to be learned only in
		// Tick, so the first tap on a host nothing had ticked for yet was sent nowhere.
		ForwardPointerKey(Pointer, PressKey, /*bInPressed*/true);
	}
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	// Forwarded as the press it is -- see the declaration.
	return IDreamPointerDownUpInterface::Execute_OnPointerDown(this, EventData);
}
bool UDreamUMGWidgetInteraction::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	FForwardedPointer* Pointer = EventData != nullptr ? ForwardedPointers.Find(EventData->PointerID) : nullptr;
	if (Pointer == nullptr || Pointer->Pointer.Get() != EventData)
	{
		// A release of a press this surface never saw: there is nothing of it here to let go of.
		return bAllowEventBubbleUp;
	}
	// The exit this press held off, owed now that nothing is held: the pointer left during the drag.
	const bool bExitWaited = Pointer->bPressing && Pointer->bExitPendingRelease;
	Pointer->bExitPendingRelease = false;
	const FKey ReleaseKey = Pointer->bTouch
		? EKeys::TouchKeys[Pointer->SlatePointerIndex]
		: DreamUMGWidgetInteractionLocal::MouseKeyOf(EventData);
	if (ReleaseKey.IsValid())
	{
		ForwardPointerKey(*Pointer, ReleaseKey, /*bInPressed*/false, /*bInEndsPress*/true);
	}
	else
	{
		Pointer->bPressing = false;
	}
	if (bExitWaited)
	{
		EndHover(EventData);
	}
	return bAllowEventBubbleUp;
}
bool UDreamUMGWidgetInteraction::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData == nullptr)
	{
		return bAllowEventBubbleUp;
	}
	// The notch goes where the pointer that turned it is, which is the first pointer over the surface only when
	// nothing else is tracked for it.
	FForwardedPointer* Pointer = ForwardedPointers.Find(EventData->PointerID);
	if (Pointer != nullptr && Pointer->Pointer.Get() == EventData)
	{
		if (CanSendInput())
		{
			ForwardPointerWheel(*Pointer, EventData->ScrollAxisValue.Y);
		}
	}
	else
	{
		ScrollWheel(EventData->ScrollAxisValue.Y);
	}
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
			if (UDreamUMGWidgetInteractionManager* Manager = UDreamUMGWidgetInteractionManager::Get(this))
			{
				Helper = Manager;
				auto& Interactions = Manager->MapVirtualUserIndexToInteraction.FindOrAdd(VirtualUserIndex);
				Interactions.AllInteractions.AddUnique(this);
			}
		}
	}
	// A behaviour whose outer chain no longer yields a widget has no visual to interact with either;
	// CanSendInput() already treats a null WidgetComponent as "not usable", so leaving it null is the
	// state the rest of the class is written for.
	WidgetComponent = GetWidget() != nullptr ? Cast<UDreamUMGWidget>(GetWidget()->GetVisual()) : nullptr;
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

	// A component that never enrolled has no manager to leave. The pointers go with the virtual user: unregistering
	// it above is what lets go of every finger still down on the Slate side, so nothing is sent for them here --
	// sending into a widget that is coming down is the one thing this teardown must not do.
	UDreamUMGWidgetInteractionManager* Manager = Helper.Get();
	Helper.Reset();
	CurrentPointerEventData.Reset();
	ForwardedPointers.Reset();
	if (Manager == nullptr)
	{
		return;
	}

	auto& MapVirtualUserIndexToInteraction = Manager->MapVirtualUserIndexToInteraction;
	if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = MapVirtualUserIndexToInteraction.Find(VirtualUserIndex))
	{
		// Giving the shared cursor back matters more than leaving the list tidy. CurrentInteraction
		// is exactly what the next component's hover tests for null, so a destroyed component still
		// named there does not leak an entry -- it makes the whole virtual user index permanently
		// deaf, because no later hover can ever claim a cursor that is already spoken for.
		if (Interactions->CurrentInteraction.Get() == this || !Interactions->CurrentInteraction.IsValid())
		{
			Interactions->CurrentInteraction.Reset();
		}
		Interactions->AllInteractions.Remove(this);
		Interactions->AllInteractions.RemoveAll([](const TWeakObjectPtr<UDreamUMGWidgetInteraction>& Entry) { return !Entry.IsValid(); });
		if (Interactions->AllInteractions.Num() == 0)
		{
			MapVirtualUserIndexToInteraction.Remove(VirtualUserIndex);
		}
	}

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
	// FocusWidget is a BlueprintCallable parameter, so "somebody passed None" is a script-authoring
	// mistake rather than an engine invariant, and it must not be a crash. Slate itself also needs to
	// exist before a user focus can be set -- SetFocus is reachable from Blueprint on a dedicated
	// server, where FSlateApplication::Get() would assert on the null instance.
	if (VirtualUser.IsValid() && IsValid(FocusWidget) && FSlateApplication::IsInitialized())
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

FWidgetPath UDreamUMGWidgetInteraction::DetermineWidgetUnderPointer(FForwardedPointer& InPointer)
{
	FWidgetPath WidgetPathUnderPointer;

	bIsHoveredWidgetInteractable = false;
	bIsHoveredWidgetFocusable = false;
	bIsHoveredWidgetHitTestVisible = false;

	InPointer.LastLocalHitLocation = InPointer.LocalHitLocation;
	FWidgetTraceResult TraceResult;
	bool bTraced = false;
	const UDreamPointerEventData* TracedPointer = InPointer.Pointer.Get();
	if (TracedPointer != nullptr && InPointer.bPressing && IsValid(TracedPointer->PressRaycaster))
	{
		// While a press made here is held, the pointer is followed on the plane it pressed, through the
		// raycaster that took the press. The point the hit test found is no use then: a drag takes this
		// surface out of the hit test, so that point lies on whatever is behind -- or there is none at
		// all. The plane is the one this surface was pressed on, so the point is where the pointer is ON
		// the surface, inside its rect or past its edge, which is what a captured UMG widget is owed.
		const FVector RayOrigin = TracedPointer->GetDragRayOrigin();
		const FVector RayEnd = RayOrigin + TracedPointer->GetDragRayDirection() * TracedPointer->PressRaycaster->GetRayLength();

		WidgetComponent->GetLocalHitLocation(TracedPointer->FaceIndex, TracedPointer->GetWorldPointInPlane(), RayOrigin, RayEnd, TraceResult.LocalHitLocation);
		bTraced = true;
	}
	else if (TracedPointer != nullptr && TracedPointer->Raycaster != nullptr)
	{
		auto RayOrigin = TracedPointer->Raycaster->GetRayOrigin();
		auto RayDirection = TracedPointer->Raycaster->GetRayDirection();
		auto RayEnd = RayOrigin + RayDirection * TracedPointer->Raycaster->GetRayLength();

		WidgetComponent->GetLocalHitLocation(TracedPointer->FaceIndex, TracedPointer->WorldPoint, RayOrigin, RayEnd, TraceResult.LocalHitLocation);
		bTraced = true;
	}
	if (bTraced)
	{
		// This pointer's own previous position, not the component's: with two fingers on the surface the last
		// position anyone hit is the other finger's, and the movement Slate reads off the pair would be a jump
		// between them.
		TraceResult.HitWidgetPath = FWidgetPath(WidgetComponent->GetHitWidgetPathForPointer(
			TraceResult.LocalHitLocation, InPointer.LastLocalHitLocation, /*bIgnoreEnabledStatus*/ false));
		InPointer.LocalHitLocation = TraceResult.LocalHitLocation;
	}
	// What the getters report: the pointer traced last.
	LastLocalHitLocation = InPointer.LastLocalHitLocation;
	LocalHitLocation = InPointer.LocalHitLocation;
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
	// Pointers retired without their exit reaching this surface are forgotten here, a finger among them lifted on
	// the Slate side first, so a pointer nobody holds any more cannot keep the cursor or the tick for good.
	TArray<int32> StalePointerIds;
	for (const TPair<int32, FForwardedPointer>& Entry : ForwardedPointers)
	{
		if (!Entry.Value.Pointer.IsValid())
		{
			StalePointerIds.Add(Entry.Key);
		}
	}
	for (const int32 StaleId : StalePointerIds)
	{
		FForwardedPointer* Stale = ForwardedPointers.Find(StaleId);
		if (Stale == nullptr)
		{
			continue;
		}
		if (Stale->bTouchDown && CanSendInput())
		{
			// No pointer to trace any more: the finger ends where it was last sent from.
			const FPointerEvent TouchEnd(VirtualUser->GetUserIndex(), Stale->SlatePointerIndex,
				Stale->LocalHitLocation, Stale->LocalHitLocation, 0.0f, /*bPressLeftMouseButton*/true);
			const FWidgetPath EndPath = Stale->LastWidgetPath.ToWidgetPath();
			ForwardedPointers.Remove(StaleId);
			SendPointerUp(EndPath, TouchEnd);
			continue;
		}
		ForwardedPointers.Remove(StaleId);
	}
	if (StalePointerIds.Num() > 0)
	{
		if (!CurrentPointerEventData.IsValid())
		{
			for (const TPair<int32, FForwardedPointer>& Entry : ForwardedPointers)
			{
				if (Entry.Value.Pointer.IsValid())
				{
					CurrentPointerEventData = Entry.Value.Pointer;
					break;
				}
			}
		}
		if (ForwardedPointers.Num() == 0)
		{
			if (UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions())
			{
				if (Interactions->CurrentInteraction.Get() == this)
				{
					Interactions->CurrentInteraction.Reset();
				}
			}
		}
		UpdateTicking();
	}

	if (!CanSendInput())
	{
		return;
	}
	// The mouse while this component holds the cursor it shares, every frame, as Slate hears a cursor that rests:
	// that is what keeps hover and tooltips alive. A finger whenever it is down, whoever holds the cursor.
	const bool bHoldsCursor = HoldsVirtualCursor();
	TArray<int32> PointerIds;
	ForwardedPointers.GenerateKeyArray(PointerIds);
	for (const int32 PointerId : PointerIds)
	{
		// Asked again for each: sending one pointer's move can run anything, the others' exits included.
		FForwardedPointer* Pointer = ForwardedPointers.Find(PointerId);
		if (Pointer != nullptr && (Pointer->bTouch || bHoldsCursor))
		{
			ForwardPointerMove(*Pointer);
		}
	}
}

void UDreamUMGWidgetInteraction::ForwardPointerMove(FForwardedPointer& InPointer)
{
	if (!CanSendInput())
	{
		return;
	}
	const UDreamPointerEventData* EventData = InPointer.Pointer.Get();
	const uint32 UserIndex = VirtualUser->GetUserIndex();
	if (InPointer.bTouch)
	{
		// A finger only while it is down, and only when it moved: Slate hears of a touch when it changes, and a
		// finger resting on the glass sends nothing -- not even the first move, which is the first REAL one.
		if (!InPointer.bTouchDown || EventData == nullptr
			|| EventData->PointerPosition.Equals(InPointer.LastSentPointerPosition, UE_KINDA_SMALL_NUMBER))
		{
			return;
		}
		const FWidgetPath WidgetPathUnderFinger = DetermineWidgetUnderPointer(InPointer);
		InPointer.LastSentPointerPosition = EventData->PointerPosition;
		if (WidgetPathUnderFinger.IsValid())
		{
			LastWidgetPath = WidgetPathUnderFinger;
			InPointer.LastWidgetPath = WidgetPathUnderFinger;
		}
		const bool bFirstMove = InPointer.bAwaitingFirstMove;
		InPointer.bAwaitingFirstMove = false;
		// Copies, because the sends below are the last use of anything here: InPointer is not read after them.
		const uint32 FingerIndex = InPointer.SlatePointerIndex;
		const FVector2D Location = InPointer.LocalHitLocation;
		const FVector2D LastLocation = InPointer.LastLocalHitLocation;
		if (bFirstMove)
		{
			// Slate's first move: the flagged event a platform sends for a finger's first travel, ahead of the
			// ordinary move for the same travel (FWindowsApplication's touch input sends the pair). The flagged
			// one goes to OnTouchFirstMove with no mouse fallback, so the ordinary one after it is still what a
			// ScrollBox pans by.
			SendPointerMove(WidgetPathUnderFinger, FPointerEvent(UserIndex, FingerIndex, Location, LastLocation,
				1.0f, /*bPressLeftMouseButton*/true, /*bIsForceChanged*/false, /*bIsFirstMove*/true));
		}
		SendPointerMove(WidgetPathUnderFinger, FPointerEvent(UserIndex, FingerIndex, Location, LastLocation,
			1.0f, /*bPressLeftMouseButton*/true));
		return;
	}

	const FWidgetPath WidgetPathUnderPointer = DetermineWidgetUnderPointer(InPointer);
	if (EventData != nullptr)
	{
		InPointer.LastSentPointerPosition = EventData->PointerPosition;
	}
	const FPointerEvent PointerEvent(
		UserIndex,
		InPointer.SlatePointerIndex,
		InPointer.LocalHitLocation,
		InPointer.LastLocalHitLocation,
		InPointer.PressedKeys,
		FKey(),
		0.0f,
		ModifierKeys);
	if (WidgetPathUnderPointer.IsValid())
	{
		check(WidgetComponent);
		LastWidgetPath = WidgetPathUnderPointer;
		InPointer.LastWidgetPath = WidgetPathUnderPointer;
	}
	else
	{
		LastWidgetPath = FWeakWidgetPath();
		InPointer.LastWidgetPath = FWeakWidgetPath();
	}
	SendPointerMove(WidgetPathUnderPointer, PointerEvent);
}

void UDreamUMGWidgetInteraction::ForwardPointerKey(FForwardedPointer& InPointer, const FKey& InKey, bool bInPressed, bool bInEndsPress)
{
	if (!CanSendInput())
	{
		// Nothing to send it to, but a press this surface was holding is still let go of: an exit that waited
		// for it must not wait for good.
		if (bInEndsPress)
		{
			InPointer.bPressing = false;
		}
		return;
	}
	const bool bTouchKey = InKey.IsTouch();
	if (bTouchKey ? InPointer.bTouchDown == bInPressed : InPointer.PressedKeys.Contains(InKey) == bInPressed)
	{
		// Already down, or not down to let go of: Slate is told of each edge once.
		if (bInEndsPress)
		{
			InPointer.bPressing = false;
		}
		return;
	}

	// Where the pointer is at this moment -- a tap can go down and come up between two ticks -- traced while a
	// press being let go of still holds it to the plane it was made on.
	const FWidgetPath WidgetPathUnderPointer = DetermineWidgetUnderPointer(InPointer);
	if (bInEndsPress)
	{
		InPointer.bPressing = false;
	}
	if (const UDreamPointerEventData* EventData = InPointer.Pointer.Get())
	{
		InPointer.LastSentPointerPosition = EventData->PointerPosition;
	}
	if (WidgetPathUnderPointer.IsValid())
	{
		LastWidgetPath = WidgetPathUnderPointer;
		InPointer.LastWidgetPath = WidgetPathUnderPointer;
	}
	const uint32 UserIndex = VirtualUser->GetUserIndex();

	if (bTouchKey)
	{
		InPointer.bTouchDown = bInPressed;
		InPointer.bAwaitingFirstMove = bInPressed;
		if (bInPressed)
		{
			// A touch starts where it is: no movement comes with it.
			InPointer.LastLocalHitLocation = InPointer.LocalHitLocation;
		}
		else
		{
			// A lifted finger stops existing on the Slate side, and so does the path it was last over.
			InPointer.LastWidgetPath = FWeakWidgetPath();
		}
		// The tick follows the finger: a finger down here is forwarded whoever holds the shared cursor.
		UpdateTicking();
		// Slate's own touch: full force going down, none coming up (FSlateApplication::OnTouchStarted and
		// OnTouchEnded), and the left button standing in for the finger, which is what a widget that answers
		// only the mouse falls back to.
		const FPointerEvent TouchEvent(UserIndex, InPointer.SlatePointerIndex, InPointer.LocalHitLocation,
			InPointer.LastLocalHitLocation, bInPressed ? 1.0f : 0.0f, /*bPressLeftMouseButton*/true);
		if (bInPressed)
		{
			SendPointerDown(WidgetPathUnderPointer, TouchEvent);
		}
		else
		{
			SendPointerUp(WidgetPathUnderPointer, TouchEvent);
		}
		return;
	}

	if (bInPressed)
	{
		InPointer.PressedKeys.Add(InKey);
	}
	else
	{
		InPointer.PressedKeys.Remove(InKey);
	}
	const FPointerEvent PointerEvent(
		UserIndex,
		InPointer.SlatePointerIndex,
		InPointer.LocalHitLocation,
		InPointer.LastLocalHitLocation,
		InPointer.PressedKeys,
		InKey,
		0.0f,
		ModifierKeys);
	if (bInPressed)
	{
		// @TODO Something about double click, expose directly, or automatically do it if key press happens within
		// the double click timeframe?
		SendPointerDown(WidgetPathUnderPointer, PointerEvent);
	}
	else
	{
		SendPointerUp(WidgetPathUnderPointer, PointerEvent);
	}
}

void UDreamUMGWidgetInteraction::ForwardPointerWheel(FForwardedPointer& InPointer, float InScrollDelta)
{
	if (!CanSendInput())
	{
		return;
	}
	const FWidgetPath WidgetPathUnderPointer = DetermineWidgetUnderPointer(InPointer);
	const FPointerEvent MouseWheelEvent(
		VirtualUser->GetUserIndex(),
		InPointer.SlatePointerIndex,
		InPointer.LocalHitLocation,
		InPointer.LastLocalHitLocation,
		InPointer.PressedKeys,
		EKeys::MouseWheelAxis,
		InScrollDelta,
		ModifierKeys);
	FSlateApplication::Get().RouteMouseWheelOrGestureEvent(WidgetPathUnderPointer, MouseWheelEvent, nullptr);
}

void UDreamUMGWidgetInteraction::SendPointerDown(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent)
{
	// A touch is routed like this too: RoutePointerDownEvent hands a touch event to OnTouchStarted before the mouse
	// handlers, and the move and release that follow reach OnTouchMoved and OnTouchEnded the same way. What
	// FSlateApplication::ProcessTouchStartedEvent adds ahead of the press -- FSlateUser::NotifyTouchStarted, which only
	// feeds Slate's gesture detector -- is out of reach: Slate keeps it for its own module (SLATE_SCOPE).
	FSlateApplication::Get().RoutePointerDownEvent(InWidgetPath, InEvent);
}

void UDreamUMGWidgetInteraction::SendPointerUp(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent)
{
	FSlateApplication::Get().RoutePointerUpEvent(InWidgetPath, InEvent);
}

void UDreamUMGWidgetInteraction::SendPointerMove(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent)
{
	FSlateApplication::Get().RoutePointerMoveEvent(InWidgetPath, InEvent, /*bIsSynthetic*/false);
}

void UDreamUMGWidgetInteraction::PressPointerKey(FKey Key)
{
	if (!CanSendInput())
	{
		return;
	}
	// For the first pointer still over the surface. With none there is nowhere for a press to be: the pointer's
	// index used to be whatever the last tick had left, and a press before any tick was sent nowhere at all.
	if (FForwardedPointer* Pointer = FindPrimaryPointer())
	{
		ForwardPointerKey(*Pointer, Key, /*bInPressed*/true);
	}
}

void UDreamUMGWidgetInteraction::ReleasePointerKey(FKey Key)
{
	if (!CanSendInput())
	{
		return;
	}
	if (FForwardedPointer* Pointer = FindPrimaryPointer())
	{
		ForwardPointerKey(*Pointer, Key, /*bInPressed*/false);
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
	// For the first pointer still over the surface, as the key calls are.
	if (FForwardedPointer* Pointer = FindPrimaryPointer())
	{
		ForwardPointerWheel(*Pointer, ScrollDelta);
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