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
				if (!HitResultArray[0].Widget->GetInteractableInHierarchy())
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
					DreamHitResult.HoverArray.Add(HitItem.Widget.Get());
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
void UDreamPointerInputModule::ApplyHoverCursor(UDreamPointerEventData* EventData)
{
	// UDreamWidget::Cursor has been editable in the details panel and inert since it was added -- its
	// getter had no caller anywhere in the plugin. In a pointer-driven UI the cursor is not
	// decoration; it is how a player learns what is grabbable and what will accept a drop.
	if (EventData == nullptr)return;
	UWorld* World = nullptr;
	for (const auto& Entry : EventData->EnterWidgetStack)
	{
		if (IsValid(Entry.Get())) { World = Entry->GetWorld(); break; }
	}
	auto PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)return;
	EMouseCursor::Type Resolved = EMouseCursor::Default;
	// EnterWidgetStack runs outermost-first, and the innermost claim should win.
	TArray<UDreamWidget*, TInlineAllocator<8>> InnermostFirst;
	for (int32 Index = EventData->EnterWidgetStack.Num() - 1; Index >= 0; --Index)
	{
		InnermostFirst.Add(EventData->EnterWidgetStack[Index].Get());
	}
	// Written unconditionally so that leaving a widget restores the default rather than stranding
	// whatever the last hovered thing asked for.
	DreamPointerPolicy::ResolveCursor(InnermostFirst, Resolved);
	PC->CurrentMouseCursor = Resolved;
}

void UDreamPointerInputModule::ProcessPointerEnterExit(UDreamEventSystem* eventSystem, UDreamPointerEventData* EventData, UDreamWidget* oldObj, UDreamWidget* newObj)
{
	ON_SCOPE_EXIT{ ApplyHoverCursor(EventData); };
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
					// The operation lives exactly as long as the drag; EndDrag has already run, so
					// the source has read bDropWasHandled by now.
					EventData->DragOperation = nullptr;
				}
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
					EventData->ClickTime = EventData->GetWorld()->GetTimeSeconds();
					if (eventSystem == nullptr)
					{
						UDreamEventSystem::ExecuteEvent_OnPointerClick(EventData->PressWidget, EventData, true);
					}
					else
					{
						eventSystem->CallOnPointerClick(EventData->PressWidget, EventData);
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
		if (EventSystem.IsValid() && EventSystem->GetScrollNavigationTargetIntoView())
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
	const auto TimeSeconds = World->GetTimeSeconds();
	if (TimeSeconds <= EventData->NavigateTickTime)
	{
		return;
	}

	// One navigation step per frame, and the next deadline measured from NOW.
	//
	// This was a while-loop that accumulated (NavigateTickTime += TimeInterval), which made it a
	// catch-up loop for wall-clock time the frame never had: a single 3s hitch -- a level load, a
	// shader compile -- came back and fired fifteen navigation steps inside the recovering frame,
	// each one moving focus and firing select events. And with an interval of 0 the deadline can
	// never be pushed past the current time at all, so the loop simply never ends; nothing clamped
	// the interval, and it is an ordinary editable property.
	const bool bIsFirstPressInSequence = EventData->NavigateTickTime == 0.0f;
	const auto TimeInterval = bIsFirstPressInSequence
		? EventSystem->GetNavigateInputIntervalForFirstTime()
		: EventSystem->GetNavigateInputInterval();
	EventData->NavigateTickTime = TimeSeconds + FMath::Max(TimeInterval, UDreamEventSystem::MinNavigateInputInterval);

	FDreamUIHitResultContainer DreamUIHitResult;
	bool bSelectValid = Navigate(EventData->NavigateDirection, EventData, DreamUIHitResult);
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
void UDreamPointerInputModule::ClearEventByID(int pointerID)
{
	auto EventData = EventSystem->GetPointerEventData(pointerID, false);
	if (EventData == nullptr)return;
	if (!EventSystem.IsValid())return;

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


