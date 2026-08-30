// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIModal.h"

#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/UIEventBlocker.h"
#include "DreamGUI.h"
#include "Engine/World.h"

namespace
{
	// Above the page band (base 1000, step 10), below the drag visual's 29000 and the tooltip's
	// 30000: a drag or a tooltip born INSIDE the modal must still draw over it.
	constexpr int32 ModalSortOrder = 25000;
}

UDreamUIModalScope::UDreamUIModalScope()
{
	// The subsystem pushes explicitly, once, after configuring; the base class's push-on-enable
	// would race it and double-push. Back is handled here, not by the generic close-on-back.
	SetActivateWhenEnabled(false);
	SetCloseOnBack(false);
	SetRestoreLastFocus(true);
}

bool UDreamUIModalScope::HandleBackAction_Implementation()
{
	if (UDreamUIModalSubsystem* Subsystem = OwnerSubsystem.Get())
	{
		Subsystem->CloseTopModal(TEXT("Back"));
		return true;
	}
	return false;
}

UDreamUIModalSubsystem* UDreamUIModalSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	return IsValid(World) ? World->GetSubsystem<UDreamUIModalSubsystem>() : nullptr;
}

bool UDreamUIModalSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UDreamUIModalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamUIModalSubsystem::Deinitialize()
{
	Queue.Reset();
	ActiveDynamicResult.Clear();
	ActiveNativeResult = nullptr;
	DestroyActiveModal();
	Super::Deinitialize();
}

void UDreamUIModalSubsystem::ShowModal(TSubclassOf<UDreamUserWidget> InDialogClass, FDreamUIModalResultDynamicDelegate OnResult)
{
	FPendingModal Modal;
	Modal.DialogClass = InDialogClass;
	Modal.DynamicResult = OnResult;
	if (IsModalActive() || bClosingModal)
	{
		Queue.Add(MoveTemp(Modal));
		return;
	}
	ShowNow(MoveTemp(Modal));
}

void UDreamUIModalSubsystem::ShowModalNative(TSubclassOf<UDreamUserWidget> InDialogClass, TFunction<void(FName)> OnResult)
{
	FPendingModal Modal;
	Modal.DialogClass = InDialogClass;
	Modal.NativeResult = MoveTemp(OnResult);
	if (IsModalActive() || bClosingModal)
	{
		Queue.Add(MoveTemp(Modal));
		return;
	}
	ShowNow(MoveTemp(Modal));
}

void UDreamUIModalSubsystem::ShowNow(FPendingModal&& InModal)
{
	if (InModal.DialogClass == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIModalSubsystem] ShowModal with no dialog class; delivering 'Invalid' immediately."));
		InModal.DynamicResult.ExecuteIfBound(TEXT("Invalid"));
		if (InModal.NativeResult)
		{
			InModal.NativeResult(TEXT("Invalid"));
		}
		return;
	}
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(ScreenRoot))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIModalSubsystem] No screen root; cannot show a modal."));
		return;
	}

	// The layer IS the scrim: full-rect tinted rect block that wins the raycast, with the event
	// blocker terminating every pointer event's bubble at it. Keys are the navigation scope's job.
	ModalLayer = NewObject<UDreamWidget>(GetWorld(), NAME_None, RF_Transient);
	ModalLayer->SetDisplayName(TEXT("DreamUIModalScrim"));
	UDreamRectBlock* Scrim = ModalLayer->CreateNewVisual<UDreamRectBlock>();
	Scrim->SetColor(UDreamGUISettings::Get()->ModalScrimColor);
	ModalLayer->SetParentBeforeRegister(ScreenRoot);
	RegisterDreamWidgetHierarchy(ModalLayer);
	ModalLayer->SetAnchorMin(FVector2D::ZeroVector);
	ModalLayer->SetAnchorMax(FVector2D(1.0f, 1.0f));
	ModalLayer->SetSizeDelta(FVector2D::ZeroVector);
	ModalLayer->SetAnchoredPosition(FVector2D::ZeroVector);
	ModalLayer->AddComponent(UUIEventBlocker::StaticClass());

	UDreamCanvas* Canvas = ModalLayer->GetComponent<UDreamCanvas>();
	if (!IsValid(Canvas))
	{
		Canvas = Cast<UDreamCanvas>(ModalLayer->AddComponent(UDreamCanvas::StaticClass()));
	}
	if (IsValid(Canvas))
	{
		Canvas->SetOverrideSorting(true);
		Canvas->SetSortOrder(ModalSortOrder, /*PropagateToChildrenCanvas*/true);
	}

	ActiveDialog = CreateDreamWidget(GetWorld(), InModal.DialogClass, ModalLayer);
	if (!IsValid(ActiveDialog))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIModalSubsystem] Dialog class '%s' failed to instantiate."), *GetNameSafe(InModal.DialogClass));
		DestroyActiveModal();
		return;
	}

	ActiveScope = Cast<UDreamUIModalScope>(ModalLayer->AddComponent(UDreamUIModalScope::StaticClass()));
	if (IsValid(ActiveScope))
	{
		ActiveScope->OwnerSubsystem = this;
		if (UDreamUINavigationStack* NavStack = UDreamUINavigationStack::Get(this))
		{
			NavStack->PushScope(ActiveScope);
		}
	}

	ActiveDynamicResult = InModal.DynamicResult;
	ActiveNativeResult = MoveTemp(InModal.NativeResult);
}

void UDreamUIModalSubsystem::CloseTopModal(FName InResult)
{
	if (!IsModalActive() || bClosingModal)
	{
		return;
	}
	bClosingModal = true;

	if (IsValid(ActiveScope))
	{
		if (UDreamUINavigationStack* NavStack = UDreamUINavigationStack::Get(this))
		{
			NavStack->PopScope(ActiveScope);
		}
	}

	// Callbacks run AFTER teardown, so one that opens the next modal finds a clean stage -- but
	// while bClosingModal still holds, so it lands in the queue rather than re-entering ShowNow.
	FDreamUIModalResultDynamicDelegate DynamicResult = ActiveDynamicResult;
	TFunction<void(FName)> NativeResult = MoveTemp(ActiveNativeResult);
	ActiveDynamicResult.Clear();
	ActiveNativeResult = nullptr;
	DestroyActiveModal();

	DynamicResult.ExecuteIfBound(InResult);
	if (NativeResult)
	{
		NativeResult(InResult);
	}
	bClosingModal = false;

	if (Queue.Num() > 0)
	{
		FPendingModal Next = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		ShowNow(MoveTemp(Next));
	}
}

void UDreamUIModalSubsystem::DestroyActiveModal()
{
	if (IsValid(ModalLayer))
	{
		ModalLayer->DestroyWidget();
	}
	ModalLayer = nullptr;
	ActiveDialog = nullptr;
	ActiveScope = nullptr;
}
