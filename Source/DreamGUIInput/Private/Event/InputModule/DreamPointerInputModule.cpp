// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Event/DreamPointerPolicy.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"
#include "Event/DreamGestureEventData.h"

namespace DreamPointerInputModuleLocal
{
	/**
	 * Whether the pointer is over what its press would click: the nearest widget at or above the pressed one that answers
	 * a click -- the pressed widget itself when none does -- or anything inside it. A click is a press and a release both
	 * on the thing clicked: SButton::OnMouseButtonUp clicks only while the button IsHovered, the pointer on the button or
	 * on anything in it. A press let go of over something else -- or over something opened on top of it, a context menu
	 * a long press opened -- ends with its up and no click.
	 */
	bool IsPointerOverPressTarget(const UDreamPointerEventData* InEventData)
	{
		UDreamWidget* PressWidget = InEventData->PressWidget;
		const UDreamWidget* Over = InEventData->EnterWidget;
		if (!IsValid(PressWidget) || !IsValid(Over))
		{
			return false;
		}
		const UDreamWidget* ClickTarget = UDreamPointerInputModule::GetEventHandle(PressWidget, UDreamPointerClickInterface::StaticClass());
		if (ClickTarget == nullptr)
		{
			ClickTarget = PressWidget;
		}
		return Over == ClickTarget || Over->IsChildOf(ClickTarget);
	}

	/**
	 * Tell the popup layer about a press before anything else hears of it: one outside a player's open popups closes them
	 * (UDreamUIPopupLayer::NotifyPointerDown). True when a popup it closed eats its outside clicks, and the press is to go
	 * no further -- the full-screen click catchers the dropdown and the menu anchor used to build, without the catcher.
	 */
	bool IsPressTakenByPopups(const UDreamUIInputUser* InUser, UDreamWidget* InPressed)
	{
		UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(InUser);
		return Popups != nullptr && Popups->NotifyPointerDown(InUser->GetUserIndex(), InPressed);
	}

	/**
	 * Whether entering a widget takes the navigation highlight onto it: navigation's own landing does, and a pointer's
	 * hover only when the pointer has really moved since its last trace (UDreamUIInputUser::HasPointerMovedSinceTrace). A
	 * screen opening under a mouse at rest, or a list scrolling under it, used to take the highlight off what the keys had
	 * left it on.
	 */
	bool DoesEnterTakeHighlight(const UDreamUIInputUser* InUser, const UDreamPointerEventData* InEventData)
	{
		return InEventData->InputType == EDreamUIPointerInputType::Navigation || InUser->HasPointerMovedSinceTrace(InEventData->PointerID);
	}
}

void UDreamPointerInputModule::ApplyHoverCursor(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData)
{
	// In a pointer-driven UI the cursor is not decoration; it is how a player learns what is grabbable and what
	// will accept a drop.
	if (InUser == nullptr || EventData == nullptr)return;
	// The hardware cursor is the mouse's, and only the mouse's pointer moves it. A finger, a second laser or a script's
	// pointer leaving what it was over says nothing about where the mouse is, and gave away the cursor the widget under
	// the mouse had claimed.
	if (EventData->PointerID != DreamUIPointerIds::Mouse)return;
	EMouseCursor::Type Resolved = EMouseCursor::Default;
	// EnterWidgetStack runs outermost-first, and the innermost claim should win.
	TArray<UDreamWidget*, TInlineAllocator<8>> InnermostFirst;
	for (int32 Index = EventData->EnterWidgetStack.Num() - 1; Index >= 0; --Index)
	{
		InnermostFirst.Add(EventData->EnterWidgetStack[Index].Get());
	}
	// Whether anything claimed one is carried through: the player restores the cursor the project had rather than
	// writing EMouseCursor::Default over it.
	const bool bWidgetClaimedCursor = DreamPointerPolicy::ResolveCursor(InnermostFirst, Resolved);
	// The cursor belongs to the player this pointer belongs to: on a split screen player 2's hover must not rewrite
	// player 1's cursor.
	InUser->ApplyHoverCursorToPlayer(bWidgetClaimedCursor, Resolved);
}

void UDreamPointerInputModule::ProcessPointerEnterExit(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData, UDreamWidget* oldObj, UDreamWidget* newObj)
{
	ON_SCOPE_EXIT{ ApplyHoverCursor(InUser, EventData); };
	if (oldObj == newObj)return;
	// The flag reads "an Exit has already gone out this frame", and both exit loops below claim it before
	// dispatching rather than after, so a handler that comes back in here through ClearEvent finds it already set.
	// That means the loops cannot ask the flag whether THEY may dispatch -- they would refuse their own first
	// event -- so the answer from before this call is kept here.
	const bool bExitAlreadyFiredBeforeThisCall = EventData->bIsExitFiredAtCurrentFrame;
	if (IsValid(oldObj) && IsValid(newObj))
	{
		auto commonRoot = FindCommonRoot(oldObj, newObj);
		//exit old
		// Claimed BEFORE the loop, and the index re-checked against the live length every iteration: an Exit
		// handler can come back through ClearEvent and empty EnterWidgetStack.
		EventData->bIsExitFiredAtCurrentFrame = true;
		for (int i = EventData->EnterWidgetStack.Num() - 1; i >= 0; i--)
		{
			if (!EventData->EnterWidgetStack.IsValidIndex(i))continue;
			if (commonRoot == EventData->EnterWidgetStack[i])
			{
				break;
			}
			if (!bExitAlreadyFiredBeforeThisCall)
			{
				InUser->CallOnPointerExit(EventData->EnterWidgetStack[i], EventData);
			}
			if (EventData->EnterWidgetStack.IsValidIndex(i))
			{
				EventData->EnterWidgetStack.RemoveAt(i);
			}
		}
		EventData->EnterWidget = nullptr;
		//enter new
		EventData->EnterWidget = newObj;
		auto enterObjectActor = newObj;
		if (commonRoot != enterObjectActor)
		{
			int insertIndex = EventData->EnterWidgetStack.Num();
			InUser->CallOnPointerEnter(newObj, EventData);
			if (DreamPointerInputModuleLocal::DoesEnterTakeHighlight(InUser, EventData))
			{
				EventData->HighlightWidgetForNavigation = newObj;
			}
			EventData->EnterWidgetStack.Add(newObj);
			enterObjectActor = IsValid(enterObjectActor) ? enterObjectActor->GetParent() : nullptr;
			while (enterObjectActor != nullptr)
			{
				if (commonRoot == enterObjectActor)
				{
					break;
				}
				InUser->CallOnPointerEnter(enterObjectActor, EventData);
				EventData->EnterWidgetStack.Insert(enterObjectActor, FMath::Min(insertIndex, EventData->EnterWidgetStack.Num()));
				enterObjectActor = IsValid(enterObjectActor) ? enterObjectActor->GetParent() : nullptr;
			}
		}
	}
	else
	{
		if (IsValid(oldObj) || EventData->EnterWidgetStack.Num() > 0)
		{
			//exit old
			// Claimed before dispatching, for the same reason as the loop above.
			EventData->bIsExitFiredAtCurrentFrame = true;
			for (int i = EventData->EnterWidgetStack.Num() - 1; i >= 0; i--)
			{
				if (!EventData->EnterWidgetStack.IsValidIndex(i))continue;
				if (IsValid(EventData->EnterWidgetStack[i]))
				{
					if (!bExitAlreadyFiredBeforeThisCall)
					{
						InUser->CallOnPointerExit(EventData->EnterWidgetStack[i], EventData);
					}
				}
				if (EventData->EnterWidgetStack.IsValidIndex(i))
				{
					EventData->EnterWidgetStack.RemoveAt(i);
				}
			}
			EventData->EnterWidget = nullptr;
			EventData->EnterWidgetStack.Reset();
		}
		if (IsValid(newObj))
		{
			//enter new
			if (!EventData->EnterWidgetStack.Contains(newObj))
			{
				auto enterObjectActor = newObj;
				int insertIndex = EventData->EnterWidgetStack.Num();
				EventData->EnterWidget = newObj;
				InUser->CallOnPointerEnter(newObj, EventData);
				if (DreamPointerInputModuleLocal::DoesEnterTakeHighlight(InUser, EventData))
				{
					EventData->HighlightWidgetForNavigation = newObj;
				}
				EventData->EnterWidgetStack.Add(newObj);
				enterObjectActor = IsValid(enterObjectActor) ? enterObjectActor->GetParent() : nullptr;
				while (enterObjectActor != nullptr)
				{
					InUser->CallOnPointerEnter(enterObjectActor, EventData);
					EventData->EnterWidgetStack.Insert(enterObjectActor, FMath::Min(insertIndex, EventData->EnterWidgetStack.Num()));
					enterObjectActor = IsValid(enterObjectActor) ? enterObjectActor->GetParent() : nullptr;
				}
			}
		}
	}
}

UDreamWidget* UDreamPointerInputModule::FindCommonRoot(UDreamWidget* A, UDreamWidget* B)
{
	if (A == nullptr || B == nullptr)return nullptr;

	while (A != nullptr)
	{
		UDreamWidget* TempB = B;
		while (TempB != nullptr)
		{
			if (A == TempB)
				return A;
			TempB = TempB->GetParent();
		}
		A = A->GetParent();
	}
	return nullptr;
}

AActor* UDreamPointerInputModule::ResolveWorldTarget(const FDreamUIHitResultContainer& InHit, bool bInHitSomething)
{
	if (!bInHitSomething || !IsValid(InHit.Raycaster))
	{
		return nullptr;
	}
	// The actor, not the primitive, is what is hovered and pressed: it is the actor's components that are
	// dispatched to, so moving between two primitives of one actor is not leaving it.
	const UPrimitiveComponent* HitComponent = InHit.Raycaster->GetWorldHitComponent(InHit.HitResult);
	return HitComponent != nullptr ? HitComponent->GetOwner() : nullptr;
}

void UDreamPointerInputModule::ExitWorldTargetUnless(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, const AActor* InStillOver)
{
	FDreamUIPointerWorldTarget* State = InUser->GetPointerWorldTarget(InEventData->PointerID, false);
	if (State == nullptr || State->Hovered.IsExplicitlyNull())
	{
		return;//over nothing outside the widgets
	}
	AActor* WasOver = State->Hovered.Get();
	if (WasOver != nullptr && WasOver == InStillOver)
	{
		return;
	}
	// Forgotten before the Exit goes out, as the widget exits claim their flag first: a handler can come back in
	// through ClearEvent and has to find nothing left to exit. An actor destroyed while hovered is simply forgotten.
	State->Hovered.Reset();
	if (IsValid(WasOver))
	{
		InUser->CallOnWorldTargetExit(WasOver, InEventData);
	}
}

void UDreamPointerInputModule::EnterWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, AActor* InNowOver)
{
	if (!IsValid(InNowOver))
	{
		return;
	}
	FDreamUIPointerWorldTarget* State = InUser->GetPointerWorldTarget(InEventData->PointerID, true);
	if (State->Hovered.Get() == InNowOver)
	{
		return;//an Enter is for arriving, not for staying
	}
	// Recorded before the Enter goes out, so a handler that clears the event system finds it to exit.
	State->Hovered = InNowOver;
	InUser->CallOnWorldTargetEnter(InNowOver, InEventData);
}

void UDreamPointerInputModule::PressWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData, const FDreamUIHitResultContainer& InHit)
{
	FDreamUIPointerWorldTarget* State = InUser->GetPointerWorldTarget(EventData->PointerID, false);
	AActor* Target = State != nullptr ? State->Hovered.Get() : nullptr;
	if (!IsValid(Target))
	{
		return;//nothing outside the widgets is under the pointer either: a press on nothing
	}
	const FDreamUIHitResult& Hit = InHit.HitResult;
	// What a widget's press records, for the readers a press has: hold-to-drag and the double-click distance on
	// the press raycaster, and a handler asking where it was pressed. PressWidget stays empty.
	EventData->WorldPoint = Hit.Location;
	EventData->WorldNormal = Hit.Normal;
	EventData->PressDistance = Hit.Distance;
	EventData->PressRayOrigin = InHit.RayOrigin;
	EventData->PressRayDirection = InHit.RayDirection;
	EventData->PressWorldPoint = Hit.Location;
	EventData->PressWorldNormal = Hit.Normal;
	EventData->PressRaycaster = InHit.Raycaster;
	EventData->SetPressRaycasterRay(InHit.RayOrigin, InHit.RayDirection);
	// The primitive's own frame stands in for the widget's: it is the surface the press landed on.
	const UPrimitiveComponent* HitComponent = IsValid(InHit.Raycaster) ? InHit.Raycaster->GetWorldHitComponent(Hit) : nullptr;
	EventData->PressWorldToLocalTransform = HitComponent != nullptr
		? HitComponent->GetComponentTransform().Inverse()
		: Target->GetActorTransform().Inverse();
	//a new press is a new chance at a long press, whatever the previous one did
	EventData->bIsLongPressFiredForThisPress = false;

	// The click run, by the widget's rule with the actor in the widget's place. The pointer's latest click has to be
	// THIS actor's click: LastClickWidget empty says it was not a widget's, LastClickedTime says it was not an older
	// one of this actor's with a widget click in between.
	const double PressClockSeconds = UDreamEventSystem::GetPointerClockSeconds(EventData);
	const float DoubleClickTime = InUser->GetConfig().DoubleClickTime;
	const bool bContinuesClickRun = EventData->ClickCount > 0
		&& EventData->LastClickWidget == nullptr
		&& State->LastClicked.Get() == Target
		&& State->LastClickedTime == EventData->ClickTime
		&& EventData->LastClickMouseButtonType == EventData->MouseButtonType
		&& DoubleClickTime > 0.0f
		&& (PressClockSeconds - EventData->ClickTime) <= (double)DoubleClickTime
		&& (!IsValid(EventData->PressRaycaster) || EventData->PressRaycaster->IsWithinDoubleClickDistance(EventData));
	EventData->ClickCount = bContinuesClickRun ? EventData->ClickCount + 1 : 1;
	const bool bIsDoubleClickPress = (EventData->ClickCount % 2) == 0
		&& EventData->InputType == EDreamUIPointerInputType::Pointer;

	// Recorded before anything is dispatched: the release goes to this actor whatever the handlers do, and State is
	// map storage that a handler can move.
	State->Pressed = Target;
	// The selection is left alone: a press outside every widget has never touched it.
	if (bIsDoubleClickPress)
	{
		InUser->CallOnWorldTargetDoubleClick(Target, EventData);
	}
	else
	{
		InUser->CallOnWorldTargetDown(Target, EventData);
	}
}

bool UDreamPointerInputModule::HoldWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData)
{
	AActor* Target = InUser->GetPressedWorldTarget(EventData->PointerID);
	if (!IsValid(Target))
	{
		return false;
	}
	// The widget's long press, once per press when the hold reaches the time. An actor's press never becomes a drag.
	if (!EventData->bIsLongPressFiredForThisPress)
	{
		const float LongPressTime = InUser->GetConfig().LongPressTime;
		const UWorld* PressWorld = InUser->GetWorld();
		if (LongPressTime > 0.0f && PressWorld != nullptr
			&& (UDreamEventSystem::GetPointerClockSeconds(PressWorld) - EventData->PressTime) >= (double)LongPressTime)
		{
			EventData->bIsLongPressFiredForThisPress = true;
			InUser->CallOnWorldTargetLongPress(Target, EventData);
		}
	}
	return true;
}

void UDreamPointerInputModule::ReleaseWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData, bool bInClick)
{
	FDreamUIPointerWorldTarget* State = InUser->GetPointerWorldTarget(EventData->PointerID, false);
	if (State == nullptr || State->Pressed.IsExplicitlyNull())
	{
		return;//the press, if there was one, was a widget's
	}
	AActor* Target = State->Pressed.Get();
	// Let go of before anything is dispatched: a second release -- ClearEvent from inside a handler -- has to find
	// nothing left to release.
	State->Pressed.Reset();
	if (!IsValid(Target))
	{
		return;//pressed, then destroyed: nobody left to tell
	}
	if (bInClick)
	{
		// What the next press measures its click run against, written now because State is map storage that a
		// handler below may move. LastClickWidget is emptied so that no widget's run survives a click on an actor.
		EventData->LastClickWidget = nullptr;
		EventData->LastClickMouseButtonType = EventData->MouseButtonType;
		EventData->LastClickPressPointerPosition = EventData->PressPointerPosition;
		EventData->LastClickPressWorldPoint = EventData->PressWorldPoint;
		EventData->ClickTime = UDreamEventSystem::GetPointerClockSeconds(EventData);
		State->LastClicked = Target;
		State->LastClickedTime = EventData->ClickTime;
	}
	// The flag a widget's release claims, claimed the same way: an Up has gone out this frame.
	EventData->bIsUpFiredAtCurrentFrame = true;
	InUser->CallOnWorldTargetUp(Target, EventData);
	if (bInClick)
	{
		InUser->CallOnWorldTargetClick(Target, EventData);
	}
}

void UDreamPointerInputModule::ProcessPointerEvent(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData, bool bLineTraceHitSomething, const FDreamUIHitResultContainer& DreamHitResult, bool& OutIsHitSomething, FDreamUIHitResult& OutHitResult)
{
	OutHitResult = DreamHitResult.HitResult;
	OutIsHitSomething = bLineTraceHitSomething;
	if (InUser == nullptr || EventData == nullptr)
	{
		return;
	}
	EventData->bIsUpFiredAtCurrentFrame = false;
	EventData->bIsExitFiredAtCurrentFrame = false;
	EventData->bIsEndDragFiredAtCurrentFrame = false;

	EventData->FaceIndex = DreamHitResult.HitResult.FaceIndex;
	EventData->Raycaster = DreamHitResult.Raycaster;

	// The actor behind a hit that is not a widget's is hovered, pressed and released alongside the widgets, by their
	// rules (see FDreamUIPointerWorldTarget). Exits go out before enters across both kinds of target, as they do
	// among widgets.
	AActor* const NowWorldTarget = ResolveWorldTarget(DreamHitResult, bLineTraceHitSomething);
	ExitWorldTargetUnless(InUser, EventData, NowWorldTarget);

	if (bLineTraceHitSomething)
	{
		auto nowHitComponent = OutHitResult.Widget.Get();
		//fire event
		EventData->WorldPoint = OutHitResult.Location;
		EventData->WorldNormal = OutHitResult.Normal;
		if (EventData->EnterWidget != nowHitComponent)//hit different object
		{
			ProcessPointerEnterExit(InUser, EventData, EventData->EnterWidget, nowHitComponent);
		}
	}
	else
	{
		if (IsValid(EventData->EnterWidget) || EventData->EnterWidgetStack.Num() > 0)//prev object
		{
			ProcessPointerEnterExit(InUser, EventData, EventData->EnterWidget, nullptr);
		}
	}

	EnterWorldTarget(InUser, EventData, NowWorldTarget);

	if (EventData->bNowIsTriggerPressed && EventData->bPrevIsTriggerPressed)//if trigger keep pressing
	{
		if (EventData->bIsDragging)//if dragging
		{
			//trigger drag event
			if (IsValid(EventData->DragWidget))
			{
				InUser->CallOnPointerDrag(EventData->DragWidget, EventData);
				OutHitResult.Distance = EventData->PressDistance;
				OutIsHitSomething = true;//always hit a plane when drag
			}
			else
			{
				// The source went away with the trigger still held. The drag is over here, so the release takes the
				// not-dragging road and would never tell the operation: told now, as the release tells it when the
				// source goes on the release's own frame.
				EventData->bIsDragging = false;
				if (UDreamDragDropOperation* EndedOperation = EventData->DragOperation.Get())
				{
					EndedOperation->NotifyDragCancelled();
				}
				EventData->DragOperation = nullptr;
			}
		}
		else//trigger press but not dragging, only concern if trigger drag event
		{
			// A navigation press belongs to what the highlight was on when the confirm went down. A step that takes the
			// highlight off it with the confirm still held ends the press there, with its up and no click -- SButton
			// lets go of a press when it loses the focus (SButton::OnFocusLost) -- rather than leaving it for the
			// release to click, over whatever the highlight is on by then.
			if (EventData->InputType == EDreamUIPointerInputType::Navigation && IsValid(EventData->PressWidget)
				&& !DreamPointerInputModuleLocal::IsPointerOverPressTarget(EventData))
			{
				UDreamWidget* LeftPressWidget = EventData->PressWidget;
				EventData->PressWidget = nullptr;
				if (!EventData->bIsUpFiredAtCurrentFrame)
				{
					EventData->bIsUpFiredAtCurrentFrame = true;
					InUser->CallOnPointerUp(LeftPressWidget, EventData);
				}
			}
			if (IsValid(EventData->PressWidget))//if hit something when press
			{
				if (IsValid(EventData->PressRaycaster))
				{
					if (EventData->PressRaycaster->ShouldStartDrag(EventData))
					{
						EventData->bIsDragging = true;
						EventData->DragWidget = EventData->PressWidget;
						InUser->CallOnPointerBeginDrag(EventData->DragWidget, EventData);
					}
				}
				// Long press, asked AFTER the drag question and only while the answer was no: a drag is visible while
				// it happens and a long press is not, so firing both would open a context menu on top of the thing
				// the player is dragging.
				if (!EventData->bIsDragging && !EventData->bIsLongPressFiredForThisPress && IsValid(EventData->PressWidget))
				{
					const float LongPressTime = InUser->GetConfig().LongPressTime;
					const UWorld* PressWorld = InUser->GetWorld();
					// On the clock the press was stamped with -- real time, so a press held in a paused game's menu
					// still matures (see UDreamEventSystem::GetPointerClockSeconds).
					if (LongPressTime > 0.0f && PressWorld != nullptr
						&& (UDreamEventSystem::GetPointerClockSeconds(PressWorld) - EventData->PressTime) >= (double)LongPressTime)
					{
						EventData->bIsLongPressFiredForThisPress = true;
						InUser->CallOnPointerLongPress(EventData->PressWidget, EventData);
					}
				}
				OutHitResult.Distance = EventData->PressDistance;
				OutIsHitSomething = true;
			}
			// A press on an actor is held the way a press on a widget is -- its long press, and the pointer still
			// counts as on what it pressed -- but it never becomes a drag.
			else if (HoldWorldTarget(InUser, EventData))
			{
				OutHitResult.Distance = EventData->PressDistance;
				OutIsHitSomething = true;
			}
		}
	}
	else if (!EventData->bNowIsTriggerPressed && !EventData->bPrevIsTriggerPressed)//is trigger keep release, only concern Enter/Exit event
	{

	}
	else//trigger state change
	{
		if (EventData->bNowIsTriggerPressed)//now is press, prev is release
		{
			// The popups first, whatever the press landed on -- a widget, the world, nothing -- and before the selection
			// can change under them. Swallowed, the press is nobody's: no down, no selection change, and with no press
			// widget its release clicks nothing, drags nothing and presses no actor either.
			if (DreamPointerInputModuleLocal::IsPressTakenByPopups(InUser, bLineTraceHitSomething ? EventData->EnterWidget.Get() : nullptr))
			{
				EventData->PressWidget = nullptr;
			}
			else if (bLineTraceHitSomething)
			{
				if (IsValid(EventData->EnterWidget))//now object
				{
					EventData->WorldPoint = OutHitResult.Location;
					EventData->WorldNormal = OutHitResult.Normal;
					EventData->PressDistance = OutHitResult.Distance;
					EventData->PressRayOrigin = DreamHitResult.RayOrigin;
					EventData->PressRayDirection = DreamHitResult.RayDirection;
					EventData->PressWorldPoint = OutHitResult.Location;
					EventData->PressWorldNormal = OutHitResult.Normal;
					EventData->PressRaycaster = DreamHitResult.Raycaster;
					EventData->SetPressRaycasterRay(DreamHitResult.RayOrigin, DreamHitResult.RayDirection);
					EventData->PressWorldToLocalTransform = EventData->EnterWidget->GetWorldTransform().Inverse();
					EventData->PressWidget = EventData->EnterWidget;
					//a new press is a new chance at a long press, whatever the previous one did
					EventData->bIsLongPressFiredForThisPress = false;
					// Which click of a run this press is, decided HERE and not at the release, because the press is
					// where a double click happens: the second press of a pair is delivered as the double click, IN
					// PLACE of its down, which is Slate's routing. The run is the same widget as the last click, no more
					// than DoubleClickTime after it, the same button, and within the press raycaster's drag threshold of
					// where the last click was pressed (UDreamBaseRaycaster::IsWithinDoubleClickDistance). Every even
					// press of a run is a double click, so a triple reads down, double click, down, as on the desktop.
					// Nothing turns an unanswered double click back into a down; Slate does not either.
					const double PressClockSeconds = UDreamEventSystem::GetPointerClockSeconds(EventData);
					const float DoubleClickTime = InUser->GetConfig().DoubleClickTime;
					const bool bContinuesClickRun = EventData->ClickCount > 0
						&& EventData->LastClickWidget == EventData->PressWidget
						&& EventData->LastClickMouseButtonType == EventData->MouseButtonType
						&& DoubleClickTime > 0.0f
						&& (PressClockSeconds - EventData->ClickTime) <= (double)DoubleClickTime
						&& (!IsValid(EventData->PressRaycaster) || EventData->PressRaycaster->IsWithinDoubleClickDistance(EventData));
					EventData->ClickCount = bContinuesClickRun ? EventData->ClickCount + 1 : 1;
					// A pointer's only: a key or a pad's confirm is never a double click in Slate.
					const bool bIsDoubleClickPress = (EventData->ClickCount % 2) == 0
						&& EventData->InputType == EDreamUIPointerInputType::Pointer;
					if (bIsDoubleClickPress)
					{
						// And no selection change: Slate's double-click route moves no focus, its down route does.
						InUser->CallOnPointerDoubleClick(EventData->PressWidget, EventData);
					}
					else
					{
						DeselectIfSelectionChanged(InUser, EventData->PressWidget, EventData);
						InUser->CallOnPointerDown(EventData->PressWidget, EventData);
					}
				}
				// No widget under the pointer, but perhaps an actor: the same press, by the same rules.
				else
				{
					PressWorldTarget(InUser, EventData, DreamHitResult);
				}
			}
		}
		else//now is release, prev is press
		{
			// Swipe is decided here, before PressWidget is cleared further down: the last moment both ends of the
			// press are known. Reported whether or not the movement was also a drag.
			DetectSwipeGesture(InUser, EventData);
			if (EventData->bIsDragging)//is dragging
			{
				EventData->bIsDragging = false;
				if (IsValid(EventData->PressWidget))
				{
					if (!EventData->bIsUpFiredAtCurrentFrame)
					{
						EventData->bIsUpFiredAtCurrentFrame = true;
						InUser->CallOnPointerUp(EventData->PressWidget, EventData);
					}
					EventData->PressWidget = nullptr;
				}
				if (bLineTraceHitSomething)//hit something when stop drag
				{
					//if enter an object when drag, and after one frame trigger release and hit new object, then old object need to call DragExit
					if (IsValid(EventData->EnterWidget) && EventData->EnterWidget != EventData->DragWidget)
					{
						InUser->CallOnPointerDragDrop(EventData->EnterWidget, EventData);
					}
				}
				//drag end
				if (IsValid(EventData->DragWidget))
				{
					if (!EventData->bIsEndDragFiredAtCurrentFrame)
					{
						EventData->bIsEndDragFiredAtCurrentFrame = true;
						InUser->CallOnPointerEndDrag(EventData->DragWidget, EventData);
					}
					EventData->DragWidget = nullptr;
				}
				else if (UDreamDragDropOperation* EndedOperation = EventData->DragOperation.Get())
				{
					// The source went away mid-drag, so nothing would tell the operation its drag is over. The cancel
					// belongs to the operation, which outlives the widget that made it.
					EndedOperation->NotifyDragCancelled();
				}
				// The operation lives exactly as long as the drag; cleared on BOTH paths.
				EventData->DragOperation = nullptr;
			}
			else//not dragging
			{
				if (IsValid(EventData->PressWidget))
				{
					if (!EventData->bIsUpFiredAtCurrentFrame)
					{
						EventData->bIsUpFiredAtCurrentFrame = true;
						InUser->CallOnPointerUp(EventData->PressWidget, EventData);
					}
					// The click this press ends in, when it is let go of over what it pressed (IsPointerOverPressTarget).
					// Its place in the run was settled at the press; the release records which widget the run's last
					// click landed on and when.
					if (IsValid(EventData->PressWidget) && DreamPointerInputModuleLocal::IsPointerOverPressTarget(EventData))
					{
						EventData->LastClickWidget = EventData->PressWidget;
						EventData->LastClickMouseButtonType = EventData->MouseButtonType;
						EventData->LastClickPressPointerPosition = EventData->PressPointerPosition;
						EventData->LastClickPressWorldPoint = EventData->PressWorldPoint;
						EventData->ClickTime = UDreamEventSystem::GetPointerClockSeconds(EventData);
						UDreamWidget* ClickedWidget = EventData->PressWidget;
						InUser->CallOnPointerClick(ClickedWidget, EventData);
					}
					EventData->PressWidget = nullptr;
				}
			}
			// A press that landed on an actor is let go as a widget's is: its up wherever the pointer is now, and its click
			// only when the pointer is over that actor still (IsPointerOverPressTarget's rule). Read before the release
			// forgets the press. A release made over nothing -- or while a pause has the raycasters tracing nothing --
			// clicked the actor pressed before it.
			AActor* const PressedActor = InUser->GetPressedWorldTarget(EventData->PointerID);
			const bool bOverPressedActor = PressedActor != nullptr && PressedActor == InUser->GetHoveredWorldTarget(EventData->PointerID);
			ReleaseWorldTarget(InUser, EventData, bOverPressedActor);
		}
	}

	EventData->bPrevIsTriggerPressed = EventData->bNowIsTriggerPressed;
}

void UDreamPointerInputModule::DetectSwipeGesture(UDreamUIInputUser* InUser, UDreamPointerEventData* EventData)
{
	if (InUser == nullptr || EventData == nullptr)return;
	// A navigation confirm travels nowhere. The pointer it is pressed on is the mouse's too, and where the mouse rests
	// is no part of the press.
	if (EventData->InputType == EDreamUIPointerInputType::Navigation)return;
	const float MinDistance = InUser->GetConfig().SwipeMinDistance;
	const float MaxDuration = InUser->GetConfig().SwipeMaxDuration;
	if (MinDistance <= 0.0f || MaxDuration <= 0.0f)return;//either one at zero turns swipes off

	const double Duration = EventData->ReleaseTime - EventData->PressTime;
	if (Duration < 0.0 || Duration > (double)MaxDuration)
	{
		return;//a slow travel across the screen is a drag, which is why there is a time limit at all
	}
	const FVector2D Delta = FVector2D(EventData->PointerPosition.X, EventData->PointerPosition.Y)
		- FVector2D(EventData->PressPointerPosition.X, EventData->PressPointerPosition.Y);
	if (Delta.Size() < (double)MinDistance)return;

	UDreamWidget* GestureWidget = EventData->PressWidget.Get();
	if (!IsValid(GestureWidget))return;//nothing to dispatch to; the press did not land on the UI

	UDreamGestureEventData* GestureData = NewObject<UDreamGestureEventData>(InUser);
	GestureData->GestureType = EDreamUIGestureType::Swipe;
	GestureData->UserIndex = EventData->UserIndex;
	GestureData->PointerIDs = { EventData->PointerID };
	GestureData->ScreenPosition = FVector2D(EventData->PointerPosition.X, EventData->PointerPosition.Y);
	GestureData->Widget = GestureWidget;
	GestureData->SwipeDelta = Delta;
	GestureData->SwipeDuration = (float)Duration;
	// Snapped to the dominant axis, in the same four directions navigation uses. Viewport Y grows downward, so a
	// positive Y delta is Down.
	if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
	{
		GestureData->SwipeDirection = Delta.X >= 0.0 ? EDreamUINavigationDirection::Right : EDreamUINavigationDirection::Left;
	}
	else
	{
		GestureData->SwipeDirection = Delta.Y >= 0.0 ? EDreamUINavigationDirection::Down : EDreamUINavigationDirection::Up;
	}
	InUser->CallOnPointerSwipe(GestureWidget, GestureData);
}

bool UDreamPointerInputModule::CanHandleInterface(UDreamWidget* targetComp, UClass* targetInterfaceClass)
{
	for (UDreamUIBehaviour* Item : targetComp->GetAllComponents())
	{
		if (IsValid(Item) && Item->GetClass()->ImplementsInterface(targetInterfaceClass))
		{
			return true;
		}
	}
	return false;
}

UDreamWidget* UDreamPointerInputModule::GetEventHandle(UDreamWidget* targetComp, UClass* targetInterfaceClass)
{
	for (UDreamWidget* Walker = targetComp; IsValid(Walker); Walker = Walker->GetParent())
	{
		if (CanHandleInterface(Walker, targetInterfaceClass))
		{
			return Walker;
		}
	}
	return nullptr;
}

void UDreamPointerInputModule::DeselectIfSelectionChanged(UDreamUIInputUser* InUser, UDreamWidget* currentPressed, UDreamBaseEventData* EventData)
{
	UDreamWidget* SelectHandle = GetEventHandle(currentPressed, UDreamPointerSelectDeselectInterface::StaticClass());
	// Against the player's focus, not this pointer's record of it: a finger just put down has none, and the focus
	// another pointer gave is still the one a press somewhere else takes away.
	if (SelectHandle != InUser->GetFocusedWidget())
	{
		InUser->SetSelectWidget(nullptr, EventData);
	}
}
