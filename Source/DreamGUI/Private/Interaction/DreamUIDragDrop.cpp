// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIDragDrop.h"

#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamDragDropOperation.h"
#include "DreamGUI.h"
#include "Engine/World.h"

namespace
{
	// Above the screen stack's band, below the tooltip's 30000 -- moot in practice, since a press
	// hides the tooltip before a drag can begin, but a deterministic answer beats a tie.
	constexpr int32 DragVisualSortOrder = 29000;
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
	// before EndDrag on the source. What is not handled by now was cancelled.
	if (!Operation->bDropWasHandled)
	{
		Operation->OnDragCancelled.Broadcast(Operation);
	}
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

bool UDreamUIDropTarget::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	UDreamDragDropOperation* Operation = EventData != nullptr ? EventData->DragOperation.Get() : nullptr;
	if (!IsValid(Operation) || !CanAcceptDrop(Operation))
	{
		// Refused drops keep bubbling: a nested target that cannot take this payload is transparent
		// to the one around it, the way every other refused event is.
		return true;
	}
	Operation->bDropWasHandled = true;
	HandleAcceptedDrop(Operation);
	OnDropAccepted.Broadcast(Operation);
	Operation->OnDropHandled.Broadcast(Operation);
	return false;
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

void UDreamUIDragDropSubsystem::Deinitialize()
{
	if (UDreamEventSystem* EventSystem = SubscribedEventSystem.Get())
	{
		EventSystem->GetInputEvent().RemoveAll(this);
	}
	SubscribedEventSystem.Reset();
	DestroyDragVisual();
	Super::Deinitialize();
}

TStatId UDreamUIDragDropSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUIDragDropSubsystem, STATGROUP_Tickables);
}

void UDreamUIDragDropSubsystem::EnsureSubscribed()
{
	if (SubscribedEventSystem.IsValid())
	{
		return;
	}
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(GetWorld(), 0);
	if (!IsValid(EventSystem))
	{
		return;
	}
	EventSystem->GetInputEvent().AddUObject(this, &UDreamUIDragDropSubsystem::HandleInputEvent);
	SubscribedEventSystem = EventSystem;
}

void UDreamUIDragDropSubsystem::Tick(float DeltaTime)
{
	EnsureSubscribed();
	if (IsValid(DragVisualHolder))
	{
		// The drag may have died without its EndDrag reaching us (ClearEvent, raycast disabled).
		const UDreamPointerEventData* PointerEvent = LastPointerEvent.Get();
		if (PointerEvent == nullptr || !PointerEvent->bIsDragging || !VisualForOperation.IsValid())
		{
			DestroyDragVisual();
			return;
		}
		UpdateDragVisualPosition();
	}
}

void UDreamUIDragDropSubsystem::HandleInputEvent(UDreamBaseEventData* InEventData)
{
	UDreamPointerEventData* PointerEvent = Cast<UDreamPointerEventData>(InEventData);
	if (!IsValid(PointerEvent))
	{
		return;
	}
	LastPointerEvent = PointerEvent;

	switch (PointerEvent->EventType)
	{
	case EDreamUIPointerEventType::BeginDrag:
		ShowDragVisual(PointerEvent);
		break;
	case EDreamUIPointerEventType::Drag:
		UpdateDragVisualPosition();
		break;
	case EDreamUIPointerEventType::EndDrag:
		DestroyDragVisual();
		break;
	default:
		break;
	}
}

void UDreamUIDragDropSubsystem::ShowDragVisual(UDreamPointerEventData* InPointerEvent)
{
	DestroyDragVisual();

	UDreamDragDropOperation* Operation = InPointerEvent->DragOperation.Get();
	if (!IsValid(Operation) || Operation->DragVisualClass == nullptr)
	{
		return;
	}
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(ScreenRoot))
	{
		return;
	}

	// Raycast-disabled through the whole subtree: the visual rides UNDER the pointer, and one that
	// could be hit would become EnterWidget and stand between the drag and every drop target.
	DragVisualHolder = NewObject<UDreamWidget>(GetWorld(), NAME_None, RF_Transient);
	DragVisualHolder->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	DragVisualHolder->SetDisplayName(TEXT("DreamUIDragVisual"));
	DragVisualHolder->SetParentBeforeRegister(ScreenRoot);
	RegisterDreamWidgetHierarchy(DragVisualHolder);

	DragVisual = CreateDreamWidget(GetWorld(), Operation->DragVisualClass, DragVisualHolder);
	if (IsValid(DragVisual))
	{
		DragVisualHolder->SetSizeDelta(FVector2D(DragVisual->GetWidth(), DragVisual->GetHeight()));
		DragVisual->SetAnchoredPosition(FVector2D::ZeroVector);
	}

	UDreamCanvas* Canvas = DragVisualHolder->GetComponent<UDreamCanvas>();
	if (!IsValid(Canvas))
	{
		Canvas = Cast<UDreamCanvas>(DragVisualHolder->AddComponent(UDreamCanvas::StaticClass()));
	}
	if (IsValid(Canvas))
	{
		Canvas->SetOverrideSorting(true);
		Canvas->SetSortOrder(DragVisualSortOrder, /*PropagateToChildrenCanvas*/true);
	}

	VisualForOperation = Operation;
	UpdateDragVisualPosition();
}

void UDreamUIDragDropSubsystem::UpdateDragVisualPosition()
{
	const UDreamPointerEventData* PointerEvent = LastPointerEvent.Get();
	const UDreamDragDropOperation* Operation = VisualForOperation.Get();
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(DragVisualHolder) || !IsValid(ScreenRoot) || PointerEvent == nullptr || Operation == nullptr)
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
	DragVisualHolder->SetAnchoredPosition(PointerInCanvas + Operation->DragVisualOffset);
}

void UDreamUIDragDropSubsystem::DestroyDragVisual()
{
	if (IsValid(DragVisualHolder))
	{
		DragVisualHolder->DestroyWidget();
	}
	DragVisualHolder = nullptr;
	DragVisual = nullptr;
	VisualForOperation.Reset();
}
