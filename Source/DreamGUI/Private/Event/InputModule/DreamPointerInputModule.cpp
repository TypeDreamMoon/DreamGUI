// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Event/DreamPointerPolicy.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "Interaction/UISelectable.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "GameFramework/PlayerController.h"
#include "Event/DreamGestureEventData.h"

bool UDreamPointerInputModule::LineTrace(UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& OutDreamHitResult)
{
	MultiHitResult.Reset();
	auto World = this->GetWorld();
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World))
	{
		auto bIsGamePaused = World->IsPaused();
		auto& AllRaycasterArray = DreamUIManager->GetAllRaycasterArray();
		InPointerEventData->HoverComponentArray.Reset();

		FVector RayOrigin(0, 0, 0), RayDir(1, 0, 0), RayEnd(1, 0, 0);
		for (int i = 0; i < AllRaycasterArray.Num(); i++)
		{
			auto& RaycasterItem = AllRaycasterArray[i];
			if (!RaycasterItem.IsValid())continue;
			if (RaycasterItem->GetUserIndex() != EventSystem->GetUserIndex())continue;
			if (RaycasterItem->GetPointerID() != INDEX_NONE && RaycasterItem->GetPointerID() != InPointerEventData->PointerID)continue;
			if (bIsGamePaused && RaycasterItem->GetAffectByGamePause())continue;
			
			// Reused rather than rebuilt: this is per raycaster per pointer per frame, and every one
			// of those allocated and freed a fresh array whose capacity is the same every time.
			HitResultArray.Reset();
			RaycasterItem->Raycast(InPointerEventData, RayOrigin, RayDir, RayEnd, HitResultArray);
			// A dragged widget is still raycastable and sits directly under the cursor, so without
			// this it wins its own hit test every frame and the pointer never reaches what is
			// underneath. That is what made OnPointerDragDrop unreachable in the ordinary case --
			// the drop is dispatched to EnterWidget only when EnterWidget is not the drag source,
			// and the drag source was always what the ray found. Removing it here rather than
			// clearing its raycastable flag keeps authored state out of it, so a drag that ends
			// abnormally cannot leave a widget permanently unclickable.
			HitResultArray.RemoveAll([InPointerEventData](const FDreamUIHitResult& InHit) {
				return DreamPointerPolicy::ShouldIgnoreHitWhileDragging(
					InHit.Widget.Get(), InPointerEventData->DragWidget, InPointerEventData->bIsDragging);
				});
			if (HitResultArray.Num() > 0)
			{
				// An uninteractable widget in front still occludes -- but only along the ray that struck
				// it, and every raycaster generates its own ray from its own origin. Returning out of
				// LineTrace here promoted that to a statement about the whole frame: hits already
				// collected from raycasters earlier in the list were thrown away and every raycaster
				// after this one never ran, so one disabled panel under the mouse also blanked out a
				// motion controller aimed at a different UI entirely. Skip this raycaster instead; its
				// own results stay hidden, which is all the occlusion ever meant.
				// A hit with no widget is a world occluder from UDreamBaseRaycaster::RaycastWorld -- a
				// wall, a trigger volume -- and it is not "uninteractable UI": it is meant to win by
				// distance and take the pointer off whatever is behind it, so it passes through here.
				UDreamWidget* TopHitWidget = HitResultArray[0].Widget.Get();
				if (TopHitWidget != nullptr && !TopHitWidget->GetInteractableInHierarchy())
				{
					continue;
				}
				FDreamUIHitResultContainer DreamHitResult;
				DreamHitResult.HitResult = HitResultArray[0];
				DreamHitResult.Raycaster = RaycasterItem.Get();
				DreamHitResult.RayOrigin = RayOrigin;
				DreamHitResult.RayDirection = RayDir;
				DreamHitResult.RayEnd = RayEnd;
				for (auto& HitItem : HitResultArray)
				{
					//a world occluder has no widget to hover; only real widgets belong in this list
					if (UDreamWidget* HoveredWidget = HitItem.Widget.Get())
					{
						DreamHitResult.HoverArray.Add(HoveredWidget);
					}
				}
				MultiHitResult.Add(DreamHitResult);
			}
		}
		if (MultiHitResult.Num() == 0)
		{
			return false;
		}
		else if (MultiHitResult.Num() > 1)
		{
			//sort only on distance (not depth), because multiHitResult only store hit result of same depth
			MultiHitResult.Sort([](const FDreamUIHitResultContainer& A, const FDreamUIHitResultContainer& B)
			{
				auto AIsScreenSpace = A.Raycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass());
				auto BIsScreenSpace = B.Raycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass());
				if (AIsScreenSpace && !BIsScreenSpace)
				{
					return true;
				}
				if (BIsScreenSpace && !AIsScreenSpace)
				{
					return false;
				}
				return A.HitResult.Distance < B.HitResult.Distance;
			});
			for (auto& hitResultItem : MultiHitResult)
			{
				for (auto& hoverItem : hitResultItem.HoverArray)
				{
					InPointerEventData->HoverComponentArray.Add(hoverItem);
				}
			}
		}
		else
		{
			for (auto hoverItem : MultiHitResult[0].HoverArray)
			{
				InPointerEventData->HoverComponentArray.Add(hoverItem);
			}
		}
		OutDreamHitResult = MultiHitResult[0];
		return true;
	}
	return false;
}

//@todo: these logs is just for editor testing, remove them when ready
#define LOG_ENTER_EXIT 0
void UDreamPointerInputModule::ApplyHoverCursor(UDreamEventSystem* InEventSystem, UDreamPointerEventData* EventData)
{
	// UDreamWidget::Cursor has been editable in the details panel and inert since it was added -- its
	// getter had no caller anywhere in the plugin. In a pointer-driven UI the cursor is not
	// decoration; it is how a player learns what is grabbable and what will accept a drop.
	if (EventData == nullptr)return;
	EMouseCursor::Type Resolved = EMouseCursor::Default;
	// EnterWidgetStack runs outermost-first, and the innermost claim should win.
	TArray<UDreamWidget*, TInlineAllocator<8>> InnermostFirst;
	for (int32 Index = EventData->EnterWidgetStack.Num() - 1; Index >= 0; --Index)
	{
		InnermostFirst.Add(EventData->EnterWidgetStack[Index].Get());
	}
	// Whether anything claimed one is carried through, not thrown away: the event system restores the
	// cursor the project had rather than writing EMouseCursor::Default over it, which is what made this
	// fight with a game that sets its own cursor outside the UI.
	const bool bWidgetClaimedCursor = DreamPointerPolicy::ResolveCursor(InnermostFirst, Resolved);

	// The cursor belongs to the player this pointer belongs to. GetFirstPlayerController unconditionally
	// -- which is what this did -- means player 2's hover rewrites player 1's cursor on a split screen.
	if (InEventSystem != nullptr)
	{
		InEventSystem->ApplyHoverCursorToPlayer(bWidgetClaimedCursor, Resolved);
		return;
	}
	// No event system to ask: this is the static dispatch path, where the first controller is the only
	// answer available and was the answer before. No restore state to keep either, so a claim is
	// written and a release falls back to the platform default.
	UWorld* World = nullptr;
	for (const auto& Entry : EventData->EnterWidgetStack)
	{
		if (IsValid(Entry.Get())) { World = Entry->GetWorld(); break; }
	}
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		PC->CurrentMouseCursor = Resolved;
	}
}

void UDreamPointerInputModule::ProcessPointerEnterExit(UDreamEventSystem* eventSystem, UDreamPointerEventData* EventData, UDreamWidget* oldObj, UDreamWidget* newObj)
{
	ON_SCOPE_EXIT{ ApplyHoverCursor(eventSystem, EventData); };
	if (oldObj == newObj)return;
	// The flag reads "an Exit has already gone out this frame", and both exit loops below claim it
	// before dispatching rather than after, so a handler that comes back in here through ClearEvent
	// finds it already set. That means the loops cannot ask the flag whether THEY may dispatch --
	// they would refuse their own first event -- so the answer from before this call is kept here.
	const bool bExitAlreadyFiredBeforeThisCall = EventData->bIsExitFiredAtCurrentFrame;
	if (IsValid(oldObj) && IsValid(newObj))
	{
		auto commonRoot = FindCommonRoot(oldObj, newObj);
#if LOG_ENTER_EXIT
		UE_LOG(DreamGUI, Error, TEXT("-----begin exit 000, commonRoot:%s"), commonRoot != nullptr ? *(commonRoot->GetActorLabel()) : TEXT("null"));
#endif
		//exit old
		// Claimed BEFORE the loop, not after it. An Exit handler is game code and can reach
		// UDreamEventSystem::ClearEvent, which comes back through ClearEventByID into this same
		// function and empties EnterWidgetStack -- and the outer loop then kept indexing it with a
		// counter it captured from the old length. Setting the flag first makes the re-entrant pass
		// dispatch nothing, and the index is re-checked against the live length every iteration.
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
				if (eventSystem == nullptr)
				{
					UDreamEventSystem::ExecuteEvent_OnPointerExit(EventData->EnterWidgetStack[i], EventData, false);
				}
				else
				{
					eventSystem->CallOnPointerExit(EventData->EnterWidgetStack[i], EventData);
				}
			}
#if LOG_ENTER_EXIT
			UE_LOG(DreamGUI, Error, TEXT("	%s"), *(EventData->EnterWidgetStack[i]->GetOwner()->GetActorLabel()));
#endif
			EventData->EnterWidgetStack.RemoveAt(i);
		}
		EventData->EnterWidget = nullptr;
#if LOG_ENTER_EXIT
		UE_LOG(DreamGUI, Error, TEXT("*****end exit, stack count:%d\n"), EventData->EnterWidgetStack.Num());
#endif
		//enter new
		EventData->EnterWidget = newObj;
		auto enterObjectActor = newObj;
		if (commonRoot != enterObjectActor)
		{
#if LOG_ENTER_EXIT
			UE_LOG(DreamGUI, Error, TEXT("-----begin enter 111"));
#endif
			int insertIndex = EventData->EnterWidgetStack.Num();
			if (eventSystem == nullptr)
			{
				UDreamEventSystem::ExecuteEvent_OnPointerEnter(newObj, EventData, false);
			}
			else
			{
				eventSystem->CallOnPointerEnter(newObj, EventData);
			}
			EventData->HighlightWidgetForNavigation = newObj;
			EventData->EnterWidgetStack.Add(newObj);
#if LOG_ENTER_EXIT
			UE_LOG(DreamGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
			enterObjectActor = enterObjectActor->GetParent();
			while (enterObjectActor != nullptr)
			{
				if (commonRoot == enterObjectActor)
				{
					break;
				}
				if (eventSystem == nullptr)
				{
					UDreamEventSystem::ExecuteEvent_OnPointerEnter(enterObjectActor, EventData, false);
				}
				else
				{
					eventSystem->CallOnPointerEnter(enterObjectActor, EventData);
				}
				EventData->EnterWidgetStack.Insert(enterObjectActor, insertIndex);
#if LOG_ENTER_EXIT
				UE_LOG(DreamGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
				enterObjectActor = enterObjectActor->GetParent();
			}
#if LOG_ENTER_EXIT
			UE_LOG(DreamGUI, Error, TEXT("*****end enter, stack count:%d\n"), EventData->EnterWidgetStack.Num());
#endif
		}
	}
	else
	{
		if (IsValid(oldObj) || EventData->EnterWidgetStack.Num() > 0)
		{
#if LOG_ENTER_EXIT
			UE_LOG(DreamGUI, Error, TEXT("-----begin exit 222"));
#endif
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
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerExit(EventData->EnterWidgetStack[i], EventData, false);
						}
						else
						{
							eventSystem->CallOnPointerExit(EventData->EnterWidgetStack[i], EventData);
						}
					}
#if LOG_ENTER_EXIT
					UE_LOG(DreamGUI, Error, TEXT("	%s, fireType:%d"), *(EventData->EnterWidgetStack[i]->GetOwner()->GetActorLabel()), (int)(EventData->enterComponentEventFireType));
#endif
				}
				EventData->EnterWidgetStack.RemoveAt(i);
			}
			EventData->EnterWidget = nullptr;
#if LOG_ENTER_EXIT
			UE_LOG(DreamGUI, Error, TEXT("*****end exit, stack count:%d\n"), EventData->EnterWidgetStack.Num());
#endif
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
#if LOG_ENTER_EXIT
				UE_LOG(DreamGUI, Error, TEXT("-----begin enter 333"));
				UE_LOG(DreamGUI, Error, TEXT("	%s"), *(enterObjectActor->GetActorLabel()));
#endif
				if (eventSystem == nullptr)
				{
					UDreamEventSystem::ExecuteEvent_OnPointerEnter(newObj, EventData, false);
				}
				else
				{
					eventSystem->CallOnPointerEnter(newObj, EventData);
				}
				EventData->HighlightWidgetForNavigation = newObj;
				EventData->EnterWidgetStack.Add(newObj);
				enterObjectActor = enterObjectActor->GetParent();
				while (enterObjectActor != nullptr)
				{
#if LOG_ENTER_EXIT
					UE_LOG(DreamGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
					if (eventSystem == nullptr)
					{
						UDreamEventSystem::ExecuteEvent_OnPointerEnter(enterObjectActor, EventData, false);
					}
					else
					{
						eventSystem->CallOnPointerEnter(enterObjectActor, EventData);
					}
					EventData->EnterWidgetStack.Insert(enterObjectActor, insertIndex);
					enterObjectActor = enterObjectActor->GetParent();
				}
#if LOG_ENTER_EXIT
				UE_LOG(DreamGUI, Error, TEXT("*****end enter, stack count:%d\n"), EventData->EnterWidgetStack.Num());
#endif
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
void UDreamPointerInputModule::ProcessPointerEvent(UDreamEventSystem* eventSystem, UDreamPointerEventData* EventData, bool bLineTraceHitSomething, const FDreamUIHitResultContainer& DreamHitResult, bool& OutIsHitSomething, FDreamUIHitResult& OutHitResult)
{
	EventData->bIsUpFiredAtCurrentFrame = false;
	EventData->bIsExitFiredAtCurrentFrame = false;
	EventData->bIsEndDragFiredAtCurrentFrame = false;

	EventData->FaceIndex = DreamHitResult.HitResult.FaceIndex;
	EventData->Raycaster = DreamHitResult.Raycaster;
	OutHitResult = DreamHitResult.HitResult;
	OutIsHitSomething = bLineTraceHitSomething;

	if (bLineTraceHitSomething)
	{
		auto nowHitComponent = OutHitResult.Widget.Get();
		//fire event
		EventData->WorldPoint = OutHitResult.Location;
		EventData->WorldNormal = OutHitResult.Normal;
		if (EventData->EnterWidget != nowHitComponent)//hit different object
		{
			ProcessPointerEnterExit(eventSystem, EventData, EventData->EnterWidget, nowHitComponent);
		}
	}
	else
	{
		if (IsValid(EventData->EnterWidget) || EventData->EnterWidgetStack.Num() > 0)//prev object
		{
			ProcessPointerEnterExit(eventSystem, EventData, EventData->EnterWidget, nullptr);
		}
	}

	if (EventData->bNowIsTriggerPressed && EventData->bPrevIsTriggerPressed)//if trigger keep pressing
	{
		if (EventData->bIsDragging)//if dragging
		{
			//trigger drag event
			if (IsValid(EventData->DragWidget))
			{
				if (eventSystem == nullptr)
				{
					UDreamEventSystem::ExecuteEvent_OnPointerDrag(EventData->DragWidget, EventData, true);
				}
				else
				{
					eventSystem->CallOnPointerDrag(EventData->DragWidget, EventData);
				}

				OutHitResult.Distance = EventData->PressDistance;
				OutIsHitSomething = true;//always hit a plane when drag
			}
			else
			{
				EventData->bIsDragging = false;
			}
		}
		else//trigger press but not dragging, only concern if trigger drag event
		{
			if (IsValid(EventData->PressWidget))//if hit something when press
			{
				if (IsValid(EventData->PressRaycaster))
				{
					if (EventData->PressRaycaster->ShouldStartDrag(EventData))
					{
						EventData->bIsDragging = true;
						EventData->DragWidget = EventData->PressWidget;
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerBeginDrag(EventData->DragWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerBeginDrag(EventData->DragWidget, EventData);
						}
					}
				}
				// Long press, asked AFTER the drag question and only while the answer was no.
				//
				// Hold-to-drag and long press are two readings of one gesture -- the finger has not
				// moved and time has passed -- so something has to decide which one the player meant.
				// The drag wins, because a drag is visible while it happens and a long press is not:
				// firing both would open a context menu on top of the thing the player is dragging.
				if (!EventData->bIsDragging && !EventData->bIsLongPressFiredForThisPress)
				{
					const float LongPressTime = eventSystem != nullptr ? eventSystem->GetLongPressTime() : 0.0f;
					const UWorld* PressWorld = EventData->GetWorld();
					if (LongPressTime > 0.0f && PressWorld != nullptr
						&& (PressWorld->GetTimeSeconds() - EventData->PressTime) >= (double)LongPressTime)
					{
						EventData->bIsLongPressFiredForThisPress = true;
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerLongPress(EventData->PressWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerLongPress(EventData->PressWidget, EventData);
						}
					}
				}
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
			if (bLineTraceHitSomething)
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
					EventData->PressWorldToLocalTransform = EventData->EnterWidget->GetWorldTransform().Inverse();
					EventData->PressWidget = EventData->EnterWidget;
					//a new press is a new chance at a long press, whatever the previous one did
					EventData->bIsLongPressFiredForThisPress = false;
					DeselectIfSelectionChanged(eventSystem, EventData->PressWidget, EventData);
					if (eventSystem == nullptr)
					{
						UDreamEventSystem::ExecuteEvent_OnPointerDown(EventData->PressWidget, EventData, true);
					}
					else
					{
						eventSystem->CallOnPointerDown(EventData->PressWidget, EventData);
					}
				}
			}
		}
		else//now is release, prev is press
		{
			// Swipe is decided here, before PressWidget is cleared further down. A swipe is a statement
			// about a whole press -- where it started, where it ended, how long it took -- and this is
			// the last moment both of its ends are still known. Reported whether or not the movement
			// was also a drag: a flick across a list is both, and the handler decides what it means.
			DetectSwipeGesture(eventSystem, EventData);
			if (EventData->bIsDragging)//is dragging
			{
				EventData->bIsDragging = false;
				if (IsValid(EventData->PressWidget))
				{
					if (!EventData->bIsUpFiredAtCurrentFrame)
					{
						EventData->bIsUpFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerUp(EventData->PressWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerUp(EventData->PressWidget, EventData);
						}
					}
					EventData->PressWidget = nullptr;
				}
				if (bLineTraceHitSomething)//hit something when stop drag
				{
					//if enter an object when drag, and after one frame trigger release and hit new object, then old object need to call DragExit
					if (IsValid(EventData->EnterWidget) && EventData->EnterWidget != EventData->DragWidget)
					{
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerDragDrop(EventData->EnterWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerDragDrop(EventData->EnterWidget, EventData);
						}
					}
				}
				//drag end
				if (IsValid(EventData->DragWidget))
				{
					if (!EventData->bIsEndDragFiredAtCurrentFrame)
					{
						EventData->bIsEndDragFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerEndDrag(EventData->DragWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerEndDrag(EventData->DragWidget, EventData);
						}
					}
					EventData->DragWidget = nullptr;
				}
				else if (UDreamDragDropOperation* EndedOperation = EventData->DragOperation.Get())
				{
					// The source went away mid-drag, so UDreamUIDragSource::OnPointerEndDrag can
					// never run and nothing would tell the operation its drag is over. The cancel
					// belongs to the operation, which outlives the widget that made it.
					EndedOperation->NotifyDragCancelled();
				}
				// The operation lives exactly as long as the drag; EndDrag has already run, so the
				// source has read bDropWasHandled by now. Cleared on BOTH paths: leaving it on the
				// event data when the source had died handed a stale operation to the next drag
				// that pointer began.
				EventData->DragOperation = nullptr;
			}
			else//not dragging
			{
				if (IsValid(EventData->PressWidget))
				{
					if (!EventData->bIsUpFiredAtCurrentFrame)
					{
						EventData->bIsUpFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerUp(EventData->PressWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerUp(EventData->PressWidget, EventData);
						}
					}
					// Double click, decided from the two things that define one: the same widget, and
					// little enough time since the last click. ClickTime has been written here since the
					// beginning and its comment has said "can be used to tell double click" for just as
					// long -- nothing ever read it, and there was no event to raise if anything had.
					const double ClickWorldTime = EventData->GetWorld()->GetTimeSeconds();
					const float DoubleClickTime = eventSystem != nullptr ? eventSystem->GetDoubleClickTime() : 0.0f;
					const bool bContinuesClickRun = EventData->ClickCount > 0
						&& EventData->LastClickWidget == EventData->PressWidget
						&& DoubleClickTime > 0.0f
						&& (ClickWorldTime - EventData->ClickTime) <= (double)DoubleClickTime;
					EventData->ClickCount = bContinuesClickRun ? EventData->ClickCount + 1 : 1;
					EventData->LastClickWidget = EventData->PressWidget;
					EventData->ClickTime = ClickWorldTime;
					// Every even click in the run, so a triple reads click/double/click like the desktop.
					const bool bIsDoubleClick = (EventData->ClickCount % 2) == 0;
					UDreamWidget* ClickedWidget = EventData->PressWidget;
					if (eventSystem == nullptr)
					{
						UDreamEventSystem::ExecuteEvent_OnPointerClick(ClickedWidget, EventData, true);
					}
					else
					{
						eventSystem->CallOnPointerClick(ClickedWidget, EventData);
					}
					// After the single click, never instead of it: a row that opens on double click
					// usually also selects on single, and a handler that wants only the second one has
					// ClickCount to read. Re-checked for validity because the click handler that just ran
					// is game code and may have destroyed the widget it was dispatched to.
					if (bIsDoubleClick && IsValid(ClickedWidget))
					{
						if (eventSystem == nullptr)
						{
							UDreamEventSystem::ExecuteEvent_OnPointerDoubleClick(ClickedWidget, EventData, true);
						}
						else
						{
							eventSystem->CallOnPointerDoubleClick(ClickedWidget, EventData);
						}
					}
					EventData->PressWidget = nullptr;
				}
			}
		}
	}

	EventData->bPrevIsTriggerPressed = EventData->bNowIsTriggerPressed;
}
bool UDreamPointerInputModule::Navigate(EDreamUINavigationDirection InDirection, UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& OutDreamUIHitResult)
{
	auto CurrentHover = InPointerEventData->HighlightWidgetForNavigation.Get();
	UDreamUIBehaviour* CurrentNavigateObject = nullptr;
	if (IsValid(CurrentHover))
	{
		auto SearchWidget = CurrentHover;
		auto FindNavigationInterface = [](UDreamWidget* InWidget) {
			auto& Components = InWidget->GetAllComponents();
			for (auto& Comp : Components)
			{
				if (IsValid(Comp) && Comp->GetClass()->ImplementsInterface(UDreamNavigationInterface::StaticClass()))
				{
					if (IDreamNavigationInterface::Execute_CanNavigateHere(Comp))
					{
						return Comp;
					}
				}
			}
			return (UDreamUIBehaviour*)nullptr;
		};
		while (IsValid(SearchWidget))
		{
			CurrentNavigateObject = FindNavigationInterface(SearchWidget);
			if (CurrentNavigateObject != nullptr)
			{
				break;
			}
			SearchWidget = SearchWidget->GetParent();
		}
	}
	
	if (CurrentNavigateObject == nullptr)//not find valid selectable object, use default one
	{
		//@todo: don't reference UISelectableComponent directly
		CurrentNavigateObject = UUISelectable::FindDefaultSelectable(this, EventSystem.IsValid() ? EventSystem->GetUserIndex() : 0);
	}
	else//find valid selectable, do navigation
	{
		TScriptInterface<IDreamNavigationInterface> NextNavigateInterface = nullptr;
		if (IDreamNavigationInterface::Execute_OnNavigate(CurrentNavigateObject, InDirection, NextNavigateInterface))
		{
			if (auto NextNavigateObject = Cast<UDreamUIBehaviour>(NextNavigateInterface.GetObject()))
			{
				CurrentNavigateObject = NextNavigateObject;
			}
		}
	}
	if (CurrentNavigateObject != nullptr)
	{
		OutDreamUIHitResult.HitResult.Widget = CurrentNavigateObject->GetWidget();//this convert is incorrect, but I need this pointer
		OutDreamUIHitResult.HitResult.Location = OutDreamUIHitResult.HitResult.Widget->GetWorldLocation();
		OutDreamUIHitResult.HitResult.Normal = OutDreamUIHitResult.HitResult.Widget->GetWorldTransform().TransformVector(FVector(0, 0, 1));
		OutDreamUIHitResult.HitResult.Normal.Normalize();
		OutDreamUIHitResult.Raycaster = nullptr;
		OutDreamUIHitResult.HoverArray.Reset();

		InPointerEventData->HighlightWidgetForNavigation = CurrentNavigateObject->GetWidget();
		// Whatever the navigation policy picked, put it on screen. Done here rather than inside
		// UISelectable so a custom IDreamNavigationInterface gets the same treatment for free.
		//
		// Only when a direction was actually given: a None call is the confirm button resolving what it
		// is pressing, and scrolling the view under the player at that moment is not navigation.
		if (InDirection != EDreamUINavigationDirection::None
			&& EventSystem.IsValid() && EventSystem->GetScrollNavigationTargetIntoView())
		{
			FDreamUINavigationScroll::RevealWidget(CurrentNavigateObject->GetWidget(), EventSystem->GetAnimateNavigationScroll());
		}
		return true;
	}
	return false;
}

void UDreamPointerInputModule::ProcessInputForNavigation(UDreamPointerEventData* EventData)
{
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (World == nullptr)return;

	// Something has to be happening. The gate used to be the repeat timer alone, which made "no key is
	// held" indistinguishable from "the key has been held long enough to repeat": a pointer parked in
	// navigation mode re-ran Navigate() every NavigateInputInterval forever, reveal-scrolling its
	// highlighted widget back into view and re-dispatching hit/select on the way. That is what yanked a
	// list back to the selected row every 0.2s while the player scrolled it with the wheel.
	const bool bHasNavigateDirection = EventData->NavigateDirection != EDreamUINavigationDirection::None;
	// A trigger edge -- confirm pressed or released in navigation mode -- still has to be dispatched on
	// the frame it happens, direction or not, or navigation-mode clicks stop working entirely.
	const bool bTriggerStateChanged = EventData->bNowIsTriggerPressed != EventData->bPrevIsTriggerPressed;
	if (!bHasNavigateDirection && !bTriggerStateChanged)
	{
		return;
	}

	const auto TimeSeconds = World->GetTimeSeconds();
	const bool bRepeatIsDue = TimeSeconds > EventData->NavigateTickTime;
	const bool bTakeNavigateStep = bHasNavigateDirection && bRepeatIsDue;
	if (!bTakeNavigateStep && !bTriggerStateChanged)
	{
		return;//direction held, but the repeat is not due yet
	}

	// One navigation step per frame, and the next deadline measured from NOW.
	//
	// This was a while-loop that accumulated (NavigateTickTime += TimeInterval), which made it a
	// catch-up loop for wall-clock time the frame never had: a single 3s hitch -- a level load, a
	// shader compile -- came back and fired fifteen navigation steps inside the recovering frame,
	// each one moving focus and firing select events. And with an interval of 0 the deadline can
	// never be pushed past the current time at all, so the loop simply never ends; nothing clamped
	// the interval, and it is an ordinary editable property.
	if (bTakeNavigateStep)
	{
		const bool bIsFirstPressInSequence = EventData->NavigateTickTime == 0.0f;
		const auto TimeInterval = bIsFirstPressInSequence
			? EventSystem->GetNavigateInputIntervalForFirstTime()
			: EventSystem->GetNavigateInputInterval();
		EventData->NavigateTickTime = TimeSeconds + FMath::Max(TimeInterval, UDreamEventSystem::MinNavigateInputInterval);
	}

	// None on a trigger-only frame: the confirm button must not also move focus. Navigate still resolves
	// the currently highlighted widget into the hit result, which is what Down/Up/Click are dispatched to.
	const EDreamUINavigationDirection StepDirection = bTakeNavigateStep
		? EventData->NavigateDirection
		: EDreamUINavigationDirection::None;
	FDreamUIHitResultContainer DreamUIHitResult;
	bool bSelectValid = Navigate(StepDirection, EventData, DreamUIHitResult);
	bool bResultHitSomething = false;
	FDreamUIHitResult HitResult;
	ProcessPointerEvent(EventSystem.Get(), EventData, bSelectValid, DreamUIHitResult, bResultHitSomething, HitResult);
	if (bResultHitSomething)
	{
		EventSystem->SetSelectWidget(HitResult.Widget.Get(), EventData);
	}

	auto TempHitComp = HitResult.Widget.Get();
	EventSystem->RaiseHitEvent(bResultHitSomething, HitResult, TempHitComp);
}
void UDreamPointerInputModule::ProcessPinchGesture()
{
	if (!EventSystem.IsValid())
	{
		bPinchActive = false;
		return;
	}

	// Exactly two pressed pointers, and the same two as last frame. A third finger ends the pinch
	// rather than being ignored: three fingers moving is not a pinch, and picking two of them would be
	// a guess the player cannot see or correct.
	UDreamPointerEventData* First = nullptr;
	UDreamPointerEventData* Second = nullptr;
	int32 PressedCount = 0;
	for (const auto& KeyValue : EventSystem->GetPointerEventDataMap())
	{
		UDreamPointerEventData* Data = KeyValue.Value.Get();
		if (!IsValid(Data) || !Data->bNowIsTriggerPressed)continue;
		if (Data->InputType != EDreamUIPointerInputType::Pointer)continue;
		++PressedCount;
		if (First == nullptr) { First = Data; }
		else if (Second == nullptr) { Second = Data; }
	}
	if (PressedCount != 2 || First == nullptr || Second == nullptr)
	{
		bPinchActive = false;
		return;
	}
	//ordered by id so the pair does not swap between frames as the map rehashes
	if (First->PointerID > Second->PointerID)
	{
		Swap(First, Second);
	}

	const FVector2D PositionA(First->PointerPosition.X, First->PointerPosition.Y);
	const FVector2D PositionB(Second->PointerPosition.X, Second->PointerPosition.Y);
	const double Distance = FVector2D::Distance(PositionA, PositionB);
	if (!bPinchActive)
	{
		// The frame the second finger lands only establishes the baseline. Reporting a scale on it
		// would report the whole distance between the fingers as if they had just moved that far.
		bPinchActive = true;
		PinchStartDistance = Distance;
		PinchLastReportedDistance = Distance;
		return;
	}

	const double DistanceDelta = Distance - PinchLastReportedDistance;
	if (FMath::Abs(DistanceDelta) < (double)EventSystem->GetPinchMinDistanceChange())
	{
		return;//two fingers resting on the glass are not pinching
	}
	PinchLastReportedDistance = Distance;

	if (!IsValid(PinchEventData))
	{
		PinchEventData = NewObject<UDreamGestureEventData>(EventSystem.Get());
	}
	PinchEventData->GestureType = EDreamUIGestureType::Pinch;
	PinchEventData->UserIndex = First->UserIndex;
	PinchEventData->PointerIDs = { First->PointerID, Second->PointerID };
	PinchEventData->ScreenPosition = (PositionA + PositionB) * 0.5;
	PinchEventData->PinchDistance = (float)Distance;
	PinchEventData->PinchDistanceDelta = (float)DistanceDelta;
	PinchEventData->PinchScale = PinchStartDistance > UE_KINDA_SMALL_NUMBER ? (float)(Distance / PinchStartDistance) : 1.0f;
	// Dispatched where the first finger went down, which is the only widget the whole gesture can be
	// said to belong to. Bubbling then lets a scroll view take a pinch its own row did not want.
	UDreamWidget* GestureWidget = First->PressWidget != nullptr ? First->PressWidget.Get() : First->EnterWidget.Get();
	PinchEventData->Widget = GestureWidget;
	if (IsValid(GestureWidget))
	{
		EventSystem->CallOnPointerPinch(GestureWidget, PinchEventData);
	}
}

void UDreamPointerInputModule::DetectSwipeGesture(UDreamEventSystem* eventSystem, UDreamPointerEventData* EventData)
{
	if (eventSystem == nullptr || EventData == nullptr)return;
	const float MinDistance = eventSystem->GetSwipeMinDistance();
	const float MaxDuration = eventSystem->GetSwipeMaxDuration();
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

	UDreamGestureEventData* GestureData = NewObject<UDreamGestureEventData>(eventSystem);
	GestureData->GestureType = EDreamUIGestureType::Swipe;
	GestureData->UserIndex = EventData->UserIndex;
	GestureData->PointerIDs = { EventData->PointerID };
	GestureData->ScreenPosition = FVector2D(EventData->PointerPosition.X, EventData->PointerPosition.Y);
	GestureData->Widget = GestureWidget;
	GestureData->SwipeDelta = Delta;
	GestureData->SwipeDuration = (float)Duration;
	// Snapped to the dominant axis, in the same four directions navigation uses. Viewport Y grows
	// downward, so a positive Y delta is Down.
	if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
	{
		GestureData->SwipeDirection = Delta.X >= 0.0 ? EDreamUINavigationDirection::Right : EDreamUINavigationDirection::Left;
	}
	else
	{
		GestureData->SwipeDirection = Delta.Y >= 0.0 ? EDreamUINavigationDirection::Down : EDreamUINavigationDirection::Up;
	}
	eventSystem->CallOnPointerSwipe(GestureWidget, GestureData);
}

void UDreamPointerInputModule::ClearEventByID(int pointerID)
{
	// Validity first: EventSystem is a weak pointer, and the caller that matters here is a world being
	// torn down. Dereferencing it to fetch the event data and only then asking whether it was alive was
	// the wrong way round.
	if (!EventSystem.IsValid())return;
	auto EventData = EventSystem->GetPointerEventData(pointerID, false);
	if (EventData == nullptr)return;

	if (EventData->bPrevIsTriggerPressed)//if trigger is pressed
	{
		if (EventData->bIsDragging)
		{
			EventData->bIsDragging = false;
			if (!EventData->bIsEndDragFiredAtCurrentFrame)
			{
				EventData->bIsEndDragFiredAtCurrentFrame = true;
				if (IsValid(EventData->DragWidget))
				{
					EventSystem->CallOnPointerEndDrag(EventData->DragWidget, EventData);
					EventData->DragWidget = nullptr;
				}
				else if (UDreamDragDropOperation* EndedOperation = EventData->DragOperation.Get())
				{
					// Same as the release path: with the source destroyed nobody else can deliver
					// the cancel, and a drag that ends in silence leaves every OnDragCancelled
					// handler -- the one that puts the item back in its slot -- waiting forever.
					EndedOperation->NotifyDragCancelled();
				}
				EventData->DragOperation = nullptr;
			}
		}

		if (!EventData->bIsUpFiredAtCurrentFrame)
		{
			EventData->bIsUpFiredAtCurrentFrame = true;
			if (IsValid(EventData->PressWidget))
			{
				auto oldPressComponent = EventData->PressWidget;
				EventData->PressWidget = nullptr;
				EventSystem->CallOnPointerUp(oldPressComponent, EventData);
			}
		}
		if (!EventData->bIsExitFiredAtCurrentFrame)
		{
			if (IsValid(EventData->EnterWidget) || EventData->EnterWidgetStack.Num() > 0)
			{
				ProcessPointerEnterExit(EventSystem.Get(), EventData, EventData->EnterWidget, nullptr);
			}
			EventData->bIsExitFiredAtCurrentFrame = true;
		}

		EventData->bPrevIsTriggerPressed = false;
	}
	else
	{
		if (!EventData->bIsExitFiredAtCurrentFrame)
		{
			if (IsValid(EventData->EnterWidget) || EventData->EnterWidgetStack.Num() > 0)
			{
				ProcessPointerEnterExit(EventSystem.Get(), EventData, EventData->EnterWidget, nullptr);
			}
			EventData->bIsExitFiredAtCurrentFrame = true;
		}
	}
}

bool UDreamPointerInputModule::CanHandleInterface(UDreamWidget* targetComp, UClass* targetInterfaceClass)
{
	bool canSelectPressedComponent = false;
	auto components = targetComp->GetAllComponents();
	for (auto item : components)
	{
		if (item->GetClass()->ImplementsInterface(targetInterfaceClass))
		{
			canSelectPressedComponent = true;
			break;
		}
	}
	return canSelectPressedComponent;
}

UDreamWidget* UDreamPointerInputModule::GetEventHandle(UDreamWidget* targetComp, UClass* targetInterfaceClass)
{
	if (!IsValid(targetComp))
	{
		return nullptr;
	}

	UDreamWidget* rootComp = targetComp;
	while (rootComp != nullptr)
	{
		if (CanHandleInterface(rootComp, targetInterfaceClass))
		{
			return rootComp;
		}
		rootComp = rootComp->GetParent();
	}
	return nullptr;
}
void UDreamPointerInputModule::DeselectIfSelectionChanged(UDreamEventSystem* eventSystem, UDreamWidget* currentPressed, UDreamBaseEventData* EventData)
{
	auto selectHandleComp = GetEventHandle(currentPressed, UDreamPointerSelectDeselectInterface::StaticClass());
	if (selectHandleComp != EventData->SelectedComponent)
	{
		UDreamEventSystem::SetSelectWidget(eventSystem, nullptr, EventData);
	}
}

void UDreamPointerInputModule::ClearEvent()
{
	if (!EventSystem.IsValid())return;

	// ClearEventByID fires Exit/Up/EndDrag into game code, and game code reaches
	// UDreamEventSystem::GetPointerEventData, which adds to the map this loop walks -- an insertion
	// that grows it rehashes it and leaves the iterator on freed storage. Snapshot the ids first.
	TArray<int> PointerIDArray;
	EventSystem->GetPointerEventDataMap().GenerateKeyArray(PointerIDArray);
	for (auto PointerID : PointerIDArray)
	{
		ClearEventByID(PointerID);
	}
}


