// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIDragDrop.h"
#include "Core/DreamUIManager.h"

#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Interaction/DreamDragDropOperation.h"
#include "DreamGUI.h"
#include "Engine/World.h"

namespace
{
	// Above the screen stack's band, below the tooltip's 30000 -- moot in practice, since a press
	// hides the tooltip before a drag can begin, but a deterministic answer beats a tie.
	constexpr int32 DragVisualSortOrder = 29000;
	// One step per pointer id, so simultaneous touch drags stack in a defined order. The ceiling is
	// what keeps a large touch id from climbing into the tooltip's band.
	constexpr int32 MaxDragVisualSortOrder = 29900;
}

// ---------------------------------------------------------------- UDreamUIDragSource

UDreamDragDropOperation* UDreamUIDragSource::CreateDragOperation_Implementation(UDreamPointerEventData* EventData)
{
	UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(this);
	Operation->Tag = Tag;
	Operation->Payload = Payload;
	Operation->DragVisualClass = DragVisualClass;
	Operation->DragVisualOffset = DragVisualOffset;
	Operation->SourceWidget = GetWidget();
	return Operation;
}

bool UDreamUIDragSource::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData == nullptr)
	{
		return true;
	}
	UDreamDragDropOperation* Operation = CreateDragOperation(EventData);
	EventData->DragOperation = Operation;
	// With no operation this drag means nothing here; let it keep bubbling so a scroll view above
	// can still take it as geometry.
	return IsValid(Operation) ? bAllowEventBubbleUp : true;
}

bool UDreamUIDragSource::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	return (EventData != nullptr && IsValid(EventData->DragOperation)) ? bAllowEventBubbleUp : true;
}

bool UDreamUIDragSource::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamDragDropOperation* Operation = EventData != nullptr ? EventData->DragOperation.Get() : nullptr;
	if (!IsValid(Operation))
	{
		return true;
	}
	// The drop, when there was one, has already run: the pipeline fires DragDrop on the target
	// before EndDrag on the source. What is not handled by now was cancelled -- and the operation
	// latches that, so the pipeline's own cancel for a dead source cannot double-fire it.
	Operation->NotifyDragCancelled();
	return bAllowEventBubbleUp;
}

// ---------------------------------------------------------------- UDreamUIDropTarget

bool UDreamUIDropTarget::CanAcceptDrop_Implementation(UDreamDragDropOperation* Operation)
{
	if (!IsValid(Operation))
	{
		return false;
	}
	if (!RequiredTag.IsNone() && Operation->Tag != RequiredTag)
	{
		return false;
	}
	if (RequiredPayloadClass != nullptr
		&& (!IsValid(Operation->Payload) || !Operation->Payload->IsA(RequiredPayloadClass)))
	{
		return false;
	}
	return true;
}

bool UDreamUIDropTarget::IsDragHovered() const
{
	if (!IsValid(this))return false;
	for (const TWeakObjectPtr<UDreamDragDropOperation>& Operation : HoveredOperations)
	{
		if (Operation.IsValid())return true;
	}
	return false;
}

void UDreamUIDropTarget::RemoveExpiredHoverOperations()
{
	for (auto It = HoveredOperations.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())It.RemoveCurrent();
	}
}

void UDreamUIDropTarget::OnDestroy()
{
	HoveredOperations.Reset();
	Super::OnDestroy();
}

void UDreamUIDropTarget::NotifyDragEnter(UDreamDragDropOperation* InOperation)
{
	if (!IsValid(this) || !IsValid(InOperation))return;
	RemoveExpiredHoverOperations();
	const TWeakObjectPtr<UDreamDragDropOperation> Operation(InOperation);
	if (HoveredOperations.Contains(Operation))return;
	// Membership is visible to callbacks that cancel this drag or begin another over the same slot.
	HoveredOperations.Add(Operation);
	OnDragEnter.Broadcast(InOperation);
}

void UDreamUIDropTarget::NotifyDragOver(UDreamDragDropOperation* InOperation)
{
	if (!IsValid(this) || !IsValid(InOperation)
		|| !HoveredOperations.Contains(TWeakObjectPtr<UDreamDragDropOperation>(InOperation)))
	{
		return;//Over belongs to this operation's hover, never to another drag over the same slot.
	}
	OnDragOver.Broadcast(InOperation);
}

void UDreamUIDropTarget::NotifyDragLeave(UDreamDragDropOperation* InOperation)
{
	if (!IsValid(this))return;
	// Forget this operation before a handler can cancel it again; another finger stays a member.
	const bool bWasHovered = HoveredOperations.Remove(TWeakObjectPtr<UDreamDragDropOperation>(InOperation)) != 0;
	RemoveExpiredHoverOperations();
	if (bWasHovered && IsValid(InOperation))OnDragLeave.Broadcast(InOperation);
}

bool UDreamUIDropTarget::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	UDreamDragDropOperation* Operation = EventData != nullptr ? EventData->DragOperation.Get() : nullptr;
	if (!IsValid(Operation) || !CanAcceptDrop(Operation))
	{
		// Refused drops keep bubbling: a nested target that cannot take this payload is transparent
		// to the one around it, the way every other refused event is.
		return true;
	}
	// The hover ends when the drop lands, whoever tells us about it first: the subsystem's leave and
	// this one race, and a target left believing it is still hovered stays lit for good.
	NotifyDragLeave(Operation);
	if (!IsValid(this) || !IsValid(Operation) || !IsValid(EventData) || EventData->DragOperation.Get() != Operation)return true;
	Operation->bDropWasHandled = true;
	HandleAcceptedDrop(Operation);
	OnDropAccepted.Broadcast(Operation);
	Operation->OnDropHandled.Broadcast(Operation);
	return false;
}

// ---------------------------------------------------------------- DreamUIDragDropPolicy

UDreamUIDropTarget* DreamUIDragDropPolicy::ResolveDropTarget(UDreamWidget* InEnterWidget, UDreamDragDropOperation* InOperation)
{
	if (!IsValid(InOperation))
	{
		return nullptr;
	}
	for (UDreamWidget* Widget = InEnterWidget; IsValid(Widget); Widget = Widget->GetParent())
	{
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			UDreamUIDropTarget* Target = Cast<UDreamUIDropTarget>(Component);
			if (IsValid(Target) && Target->CanAcceptDrop(InOperation))
			{
				return Target;
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------- UDreamUIDragDropSubsystem

UDreamUIDragDropSubsystem* UDreamUIDragDropSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	return IsValid(World) ? World->GetSubsystem<UDreamUIDragDropSubsystem>() : nullptr;
}

bool UDreamUIDragDropSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UDreamUIDragDropSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamUIDragDropSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DreamUI::EnrolWorldService(Collection, *this, *this);
	// Every player's events, from the moment the world has input.
	if (UDreamUIInputSubsystem* Input = Collection.InitializeDependency<UDreamUIInputSubsystem>())
	{
		Input->GetOnInputEvent().AddUObject(this, &UDreamUIDragDropSubsystem::HandleInputEvent);
		InputSubsystem = Input;
	}
}

void UDreamUIDragDropSubsystem::Deinitialize()
{
	// Passive: the world's teardown has taken this service down already (TeardownForWorld), unless the
	// world had no manager to take it.
	if (!bTornDownForWorld && GetWorld() != nullptr)
	{
		TeardownForWorld(*GetWorld());
	}
	Super::Deinitialize();
}

void UDreamUIDragDropSubsystem::TeardownForWorld(UWorld& InWorld)
{
	if (bTornDownForWorld)
	{
		return;
	}
	bTornDownForWorld = true;
	if (UDreamUIInputSubsystem* Input = InputSubsystem.Get())
	{
		Input->GetOnInputEvent().RemoveAll(this);
	}
	InputSubsystem.Reset();
	TArray<FIntPoint> Keys;
	FollowedDrags.GetKeys(Keys);
	for (const FIntPoint& Key : Keys)
	{
		StopFollowingDrag(Key);
	}
	FollowedDrags.Reset();
}

TStatId UDreamUIDragDropSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUIDragDropSubsystem, STATGROUP_Tickables);
}

bool UDreamUIDragDropSubsystem::IsStillLive(const FFollowedDrag& InDrag)
{
	const UDreamPointerEventData* PointerEvent = InDrag.PointerEvent.Get();
	return PointerEvent != nullptr && PointerEvent->bIsDragging && InDrag.Operation.IsValid();
}

bool UDreamUIDragDropSubsystem::IsDragInProgress() const
{
	for (const TPair<FIntPoint, FFollowedDrag>& Pair : FollowedDrags)
	{
		if (IsStillLive(Pair.Value))
		{
			return true;
		}
	}
	return false;
}

bool UDreamUIDragDropSubsystem::IsDragInProgressForUser(int32 InUserIndex) const
{
	for (const TPair<FIntPoint, FFollowedDrag>& Pair : FollowedDrags)
	{
		if (Pair.Key.X == InUserIndex && IsStillLive(Pair.Value))
		{
			return true;
		}
	}
	return false;
}

UDreamDragDropOperation* UDreamUIDragDropSubsystem::GetDragOperationForPointer(int32 InPointerID) const
{
	return GetDragOperationForUserPointer(0, InPointerID);
}

UDreamDragDropOperation* UDreamUIDragDropSubsystem::GetDragOperationForUserPointer(int32 InUserIndex, int32 InPointerID) const
{
	const FFollowedDrag* Drag = FollowedDrags.Find(MakeKey(InUserIndex, InPointerID));
	return Drag != nullptr ? Drag->Operation.Get() : nullptr;
}

UDreamUIDropTarget* UDreamUIDragDropSubsystem::GetHoveredTargetForPointer(int32 InPointerID) const
{
	return GetHoveredTargetForUserPointer(0, InPointerID);
}

UDreamUIDropTarget* UDreamUIDragDropSubsystem::GetHoveredTargetForUserPointer(int32 InUserIndex, int32 InPointerID) const
{
	const FFollowedDrag* Drag = FollowedDrags.Find(MakeKey(InUserIndex, InPointerID));
	return Drag != nullptr ? Drag->HoveredTarget.Get() : nullptr;
}

bool UDreamUIDragDropSubsystem::CancelActiveDrag()
{
	TSet<int32> UsersDragging;
	for (const TPair<FIntPoint, FFollowedDrag>& Pair : FollowedDrags)
	{
		if (IsStillLive(Pair.Value))
		{
			UsersDragging.Add(Pair.Key.X);
		}
	}
	bool bCancelled = false;
	for (const int32 UserIndex : UsersDragging)
	{
		bCancelled |= CancelActiveDragForUser(UserIndex);
	}
	return bCancelled;
}

bool UDreamUIDragDropSubsystem::CancelActiveDragForUser(int32 InUserIndex)
{
	UDreamUIInputSubsystem* Input = InputSubsystem.Get();
	UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
	if (User == nullptr || !IsDragInProgressForUser(InUserIndex))
	{
		return false;
	}
	// The pipeline's own cancel verb. It fires EndDrag, then Up, then Exit -- and crucially NOT DragDrop, which is
	// the whole difference between letting go and giving up. Re-implementing that order here would be a second copy
	// of the teardown that has to agree with the first forever.
	//
	// It lets go of every pointer of that player, which is why this cancels all their drags rather than a chosen
	// one: Escape belongs to no finger in particular -- but it does belong to a player.
	User->ReleaseAllPointers();
	TArray<FIntPoint> Keys;
	FollowedDrags.GetKeys(Keys);
	for (const FIntPoint& Key : Keys)
	{
		if (Key.X == InUserIndex)
		{
			StopFollowingDrag(Key);
		}
	}
	return true;
}

void UDreamUIDragDropSubsystem::Tick(float DeltaTime)
{
	// A drag can die without its EndDrag reaching us (ClearEvent, raycast disabled, the screen torn down). Checked
	// whether or not a visual exists, because the hover bookkeeping outlives a drag with no visual class just as
	// easily, and a target left lit is as wrong as a visual left parked.
	if (FollowedDrags.Num() == 0)
	{
		return;
	}
	// Walked by key, each drag looked up as its turn comes: a target's enter and leave reach game code, which can end a
	// drag -- Escape's cancel -- or begin one, and a loop over the map itself went on through a map changed under it.
	TArray<FIntPoint, TInlineAllocator<4>> Keys;
	FollowedDrags.GetKeys(Keys);
	for (const FIntPoint& Key : Keys)
	{
		FFollowedDrag* Drag = FollowedDrags.Find(Key);
		if (Drag == nullptr)
		{
			continue;//ended by a handler earlier in this walk
		}
		if (!IsStillLive(*Drag))
		{
			StopFollowingDrag(Key);
			continue;
		}
		UpdateDragVisualPosition(*Drag);
		UpdateDropHover(Key);
	}
}

void UDreamUIDragDropSubsystem::HandleInputEvent(UDreamBaseEventData* InEventData)
{
	UDreamPointerEventData* PointerEvent = Cast<UDreamPointerEventData>(InEventData);
	if (!IsValid(PointerEvent))
	{
		return;
	}
	// Everything is keyed by the player and the pointer the event came from, so one finger's move can never move
	// another finger's visual or light up another finger's drop target -- nor another player's.
	const FIntPoint Key = MakeKey(PointerEvent->UserIndex, PointerEvent->PointerID);

	switch (PointerEvent->EventType)
	{
	case EDreamUIPointerEventType::BeginDrag:
		BeginFollowingDrag(PointerEvent);
		break;
	case EDreamUIPointerEventType::Drag:
		if (FFollowedDrag* Drag = FollowedDrags.Find(Key))
		{
			if (Drag->PointerEvent.Get() == PointerEvent)
			{
				UpdateDragVisualPosition(*Drag);
				UpdateDropHover(Key);
			}
		}
		break;
	case EDreamUIPointerEventType::EndDrag:
		StopFollowingDrag(Key);
		break;
	default:
		break;
	}
}

void UDreamUIDragDropSubsystem::BeginFollowingDrag(UDreamPointerEventData* InPointerEvent)
{
	const FIntPoint Key = MakeKey(InPointerEvent->UserIndex, InPointerEvent->PointerID);
	// A pointer that begins a second drag without ending the first has had something go wrong
	// upstream; tearing the old one down here keeps this map from being the place it shows up.
	StopFollowingDrag(Key);

	UDreamDragDropOperation* Operation = InPointerEvent->DragOperation.Get();
	if (!IsValid(Operation))
	{
		return;//a drag with no meaning is pure geometry: a scroll, not a drop
	}
	FFollowedDrag& Drag = FollowedDrags.Add(Key);
	Drag.PointerEvent = InPointerEvent;
	Drag.Operation = Operation;
	ShowDragVisual(Drag, InPointerEvent);
	UpdateDropHover(Key);
}

void UDreamUIDragDropSubsystem::UpdateDropHover(const FIntPoint& InKey)
{
	FFollowedDrag* Drag = FollowedDrags.Find(InKey);
	if (Drag == nullptr)
	{
		return;
	}
	UDreamPointerEventData* PointerEvent = Drag->PointerEvent.Get();
	UDreamDragDropOperation* Operation = Drag->Operation.Get();
	if (PointerEvent == nullptr || Operation == nullptr)
	{
		ClearDropHover(*Drag);
		return;
	}
	// Whether the drag is still followed once a target's handler has run: one that ended it -- or ended it and began
	// another on the same pointer -- has had the leave of whatever it lit delivered by StopFollowingDrag, and a target
	// entered now would never hear it was left.
	auto IsStillFollowed = [this, &InKey, Operation]()
	{
		const FFollowedDrag* Now = FollowedDrags.Find(InKey);
		return Now != nullptr && Now->Operation.Get() == Operation;
	};
	UDreamUIDropTarget* Target = DreamUIDragDropPolicy::ResolveDropTarget(PointerEvent->EnterWidget, Operation);
	UDreamUIDropTarget* Previous = Drag->HoveredTarget.Get();
	if (Target != Previous)
	{
		// Written before either target hears of it, through an entry no handler has had the chance to move yet.
		Drag->HoveredTarget = Target;
		if (IsValid(Previous))
		{
			Previous->NotifyDragLeave(Operation);
			if (!IsStillFollowed())
			{
				return;
			}
		}
		if (IsValid(Target))
		{
			Target->NotifyDragEnter(Operation);
			if (!IsStillFollowed())
			{
				return;
			}
		}
	}
	if (IsValid(Target))
	{
		Target->NotifyDragOver(Operation);
	}
}

void UDreamUIDragDropSubsystem::ClearDropHover(FFollowedDrag& InDrag)
{
	// Forgotten before the target hears of it: its leave reaches game code, which can change the map InDrag lives in.
	UDreamUIDropTarget* Previous = InDrag.HoveredTarget.Get();
	UDreamDragDropOperation* Operation = InDrag.Operation.Get();
	InDrag.HoveredTarget.Reset();
	if (Previous != nullptr)
	{
		Previous->NotifyDragLeave(Operation);
	}
}

void UDreamUIDragDropSubsystem::StopFollowingDrag(const FIntPoint& InKey)
{
	FFollowedDrag* Drag = FollowedDrags.Find(InKey);
	if (Drag == nullptr)
	{
		return;
	}
	// The entry is taken OUT of the map before the callbacks below run: NotifyDragLeave reaches game
	// code, and game code that starts a new drag on the same pointer would otherwise be editing an
	// entry this call is still holding a reference into.
	FFollowedDrag Closing = MoveTemp(*Drag);
	FollowedDrags.Remove(InKey);
	ClearDropHover(Closing);
	DestroyDragVisual(Closing);
}

void UDreamUIDragDropSubsystem::ShowDragVisual(FFollowedDrag& InDrag, UDreamPointerEventData* InPointerEvent)
{
	DestroyDragVisual(InDrag);

	UDreamDragDropOperation* Operation = InPointerEvent->DragOperation.Get();
	if (!IsValid(Operation) || Operation->DragVisualClass == nullptr)
	{
		return;
	}
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	// The screen of the player who is dragging, which the pointer already knows.
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(GetWorld());
	const int32 ScreenIndex = Input != nullptr ? Input->GetScreenIndexForUser(InPointerEvent->UserIndex) : InPointerEvent->UserIndex;
	UDreamWidget* ScreenRoot = IsValid(ScreenUI)
		? ScreenUI->GetOrCreateScreenRootForUserIndex(ScreenIndex) : nullptr;
	if (!IsValid(ScreenRoot))
	{
		return;
	}

	// Raycast-disabled through the whole subtree: the visual rides UNDER the pointer, and one that
	// could be hit would become EnterWidget and stand between the drag and every drop target.
	UDreamWidget* VisualHolder = NewObject<UDreamWidget>(this, NAME_None, RF_Transient);
	VisualHolder->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	VisualHolder->SetDisplayName(TEXT("DreamUIDragVisual"));
	VisualHolder->SetParentBeforeRegister(ScreenRoot);
	RegisterDreamWidgetHierarchy(VisualHolder);
	InDrag.VisualHolder = VisualHolder;

	UDreamUserWidget* Visual = CreateDreamWidget(GetWorld(), Operation->DragVisualClass, VisualHolder);
	InDrag.Visual = Visual;
	if (IsValid(Visual))
	{
		VisualHolder->SetSizeDelta(FVector2D(Visual->GetWidth(), Visual->GetHeight()));
		Visual->SetAnchoredPosition(FVector2D::ZeroVector);
	}

	UDreamCanvas* Canvas = VisualHolder->GetComponent<UDreamCanvas>();
	if (!IsValid(Canvas))
	{
		Canvas = Cast<UDreamCanvas>(VisualHolder->AddComponent(UDreamCanvas::StaticClass()));
	}
	if (IsValid(Canvas))
	{
		Canvas->SetOverrideSorting(true);
		// One step per pointer so two visuals under two fingers have a defined order instead of
		// z-fighting, and clamped so a large touch id cannot climb into the tooltip's band.
		const int32 SortOrder = FMath::Clamp(DragVisualSortOrder + InPointerEvent->PointerID,
			DragVisualSortOrder, MaxDragVisualSortOrder);
		Canvas->SetSortOrder(SortOrder, /*PropagateToChildrenCanvas*/true);
	}

	UpdateDragVisualPosition(InDrag);
}

void UDreamUIDragDropSubsystem::UpdateDragVisualPosition(FFollowedDrag& InDrag)
{
	const UDreamPointerEventData* PointerEvent = InDrag.PointerEvent.Get();
	const UDreamDragDropOperation* Operation = InDrag.Operation.Get();
	UDreamWidget* VisualHolder = InDrag.VisualHolder.Get();
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	// Same screen ShowDragVisual put the visual on: the dragging player's.
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(GetWorld());
	const int32 DraggingUser = PointerEvent != nullptr ? PointerEvent->UserIndex : 0;
	const int32 ScreenIndex = Input != nullptr ? Input->GetScreenIndexForUser(DraggingUser) : DraggingUser;
	UDreamWidget* ScreenRoot = IsValid(ScreenUI)
		? ScreenUI->GetOrCreateScreenRootForUserIndex(ScreenIndex) : nullptr;
	if (!IsValid(VisualHolder) || !IsValid(ScreenRoot) || PointerEvent == nullptr || Operation == nullptr)
	{
		return;
	}
	UDreamCanvas* RootCanvas = ScreenRoot->GetComponent<UDreamCanvas>();
	if (!IsValid(RootCanvas))
	{
		return;
	}
	FVector2D PointerInCanvas = FVector2D::ZeroVector;
	if (!RootCanvas->ConvertPositionFromViewportToCanvas(FVector2D(PointerEvent->PointerPosition.X, PointerEvent->PointerPosition.Y), PointerInCanvas))
	{
		return;
	}
	// Bottom-left-origin out of the conversion, center-origin into the anchored position -- the
	// same shift the tooltip needed; without it the visual rides in a corner instead of under the
	// pointer.
	PointerInCanvas -= FVector2D(ScreenRoot->GetWidth() * 0.5f, ScreenRoot->GetHeight() * 0.5f);
	VisualHolder->SetAnchoredPosition(PointerInCanvas + Operation->DragVisualOffset);
}

void UDreamUIDragDropSubsystem::DestroyDragVisual(FFollowedDrag& InDrag)
{
	if (UDreamWidget* VisualHolder = InDrag.VisualHolder.Get())
	{
		VisualHolder->DestroyWidget();
	}
	InDrag.VisualHolder.Reset();
	InDrag.Visual.Reset();
}
