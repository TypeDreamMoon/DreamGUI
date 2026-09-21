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
	// One step per nesting level, so a modal raised from a modal draws over the one that raised it.
	// The gap to the drag visual leaves room for forty levels, which is thirty-nine more than any
	// real screen has and still a bounded number rather than an unbounded climb into the tooltip.
	constexpr int32 ModalSortStep = 100;
	constexpr int32 MaxModalSortOrder = 28900;
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
		// This scope's own player, not player zero: Back arrives from whichever controller pressed it,
		// and closing the wrong player's dialog is worse than not closing one at all.
		Subsystem->CloseTopModal(TEXT("Back"), GetUserIndex());
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
	// No results are delivered on teardown: the world these callbacks would run in is going away,
	// and a dialog that never got an answer is the honest outcome of a world ending under it.
	for (TPair<int32, TArray<FActiveModal>>& Pair : ModalStacks)
	{
		for (int32 Index = Pair.Value.Num() - 1; Index >= 0; --Index)
		{
			DestroyModal(Pair.Value[Index]);
		}
	}
	ModalStacks.Reset();
	Super::Deinitialize();
}

TArray<UDreamUIModalSubsystem::FActiveModal>& UDreamUIModalSubsystem::FindOrAddStack(int32 InUserIndex)
{
	return ModalStacks.FindOrAdd(InUserIndex);
}

const TArray<UDreamUIModalSubsystem::FActiveModal>* UDreamUIModalSubsystem::FindStack(int32 InUserIndex) const
{
	return ModalStacks.Find(InUserIndex);
}

int32 UDreamUIModalSubsystem::GetModalDepth(int32 InUserIndex) const
{
	const TArray<FActiveModal>* Stack = FindStack(InUserIndex);
	return Stack != nullptr ? Stack->Num() : 0;
}

bool UDreamUIModalSubsystem::IsAnyModalActive() const
{
	for (const TPair<int32, TArray<FActiveModal>>& Pair : ModalStacks)
	{
		if (Pair.Value.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

UDreamUserWidget* UDreamUIModalSubsystem::GetActiveModalWidget(int32 InUserIndex) const
{
	const TArray<FActiveModal>* Stack = FindStack(InUserIndex);
	return Stack != nullptr && Stack->Num() > 0 ? Stack->Last().Dialog.Get() : nullptr;
}

void UDreamUIModalSubsystem::ShowModal(TSubclassOf<UDreamUserWidget> InDialogClass, FDreamUIModalResultDynamicDelegate OnResult, int32 InUserIndex)
{
	FPendingModal Modal;
	Modal.DialogClass = InDialogClass;
	Modal.DynamicResult = OnResult;
	Modal.UserIndex = InUserIndex;
	ShowNow(MoveTemp(Modal));
}

void UDreamUIModalSubsystem::ShowModalNative(TSubclassOf<UDreamUserWidget> InDialogClass, TFunction<void(FName)> OnResult, int32 InUserIndex)
{
	FPendingModal Modal;
	Modal.DialogClass = InDialogClass;
	Modal.NativeResult = MoveTemp(OnResult);
	Modal.UserIndex = InUserIndex;
	ShowNow(MoveTemp(Modal));
}

void UDreamUIModalSubsystem::FailPendingModal(FPendingModal& InModal, FName InResult)
{
	// The result is taken out of the pending modal before anything runs, so a callback that shows
	// another modal cannot see this one still holding a result to deliver.
	FDreamUIModalResultDynamicDelegate DynamicResult = InModal.DynamicResult;
	TFunction<void(FName)> NativeResult = MoveTemp(InModal.NativeResult);
	InModal.DynamicResult.Clear();
	InModal.NativeResult = nullptr;

	DynamicResult.ExecuteIfBound(InResult);
	if (NativeResult)
	{
		NativeResult(InResult);
	}
}

void UDreamUIModalSubsystem::ShowNow(FPendingModal&& InModal)
{
	if (InModal.DialogClass == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIModalSubsystem] ShowModal with no dialog class; delivering 'Invalid' immediately."));
		FailPendingModal(InModal, TEXT("Invalid"));
		return;
	}
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	// That player's own screen. A scrim on the shared root would darken both halves of a split screen
	// for a dialog only one of them raised.
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForUserIndex(InModal.UserIndex) : nullptr;
	if (!IsValid(ScreenRoot))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIModalSubsystem] No screen root; cannot show a modal."));
		// Same 'Invalid' as a missing class, and for the same reason: from the caller's side both are
		// "it never opened", and returning silently left an awaited OnResult unfired forever.
		FailPendingModal(InModal, TEXT("Invalid"));
		return;
	}

	// The layer IS the scrim: full-rect tinted rect block that wins the raycast, with the event
	// blocker terminating every pointer event's bubble at it. Keys are the navigation scope's job.
	// One per modal, so a nested modal gets its own scrim over the dialog that raised it.
	UDreamWidget* ModalLayer = NewObject<UDreamWidget>(GetWorld(), NAME_None, RF_Transient);
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
		const int32 SortOrder = FMath::Min(ModalSortOrder + FindOrAddStack(InModal.UserIndex).Num() * ModalSortStep, MaxModalSortOrder);
		Canvas->SetSortOrder(SortOrder, /*PropagateToChildrenCanvas*/true);
	}

	FActiveModal Active;
	Active.Layer = ModalLayer;
	Active.Dialog = CreateDreamWidget(GetWorld(), InModal.DialogClass, ModalLayer);
	if (!Active.Dialog.IsValid())
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIModalSubsystem] Dialog class '%s' failed to instantiate."), *GetNameSafe(InModal.DialogClass));
		DestroyModal(Active);
		FailPendingModal(InModal, TEXT("Invalid"));
		return;
	}

	UDreamUIModalScope* Scope = Cast<UDreamUIModalScope>(ModalLayer->AddComponent(UDreamUIModalScope::StaticClass()));
	Active.Scope = Scope;
	if (IsValid(Scope))
	{
		Scope->OwnerSubsystem = this;
		// The scope carries the user index for the navigation stack AND for Back: pushing without it
		// confined player two's focus with player one's dialog.
		Scope->SetUserIndex(InModal.UserIndex);
		if (UDreamUINavigationStack* NavStack = UDreamUINavigationStack::Get(this))
		{
			// The navigation stack is a stack too, and this is where the two agree: pushing confines
			// focus to the new dialog, popping hands it back to the dialog underneath rather than to
			// the page, which is the whole reason nesting can work at all.
			NavStack->PushScope(Scope);
		}
	}

	Active.DynamicResult = InModal.DynamicResult;
	Active.NativeResult = MoveTemp(InModal.NativeResult);
	FindOrAddStack(InModal.UserIndex).Add(MoveTemp(Active));
}

void UDreamUIModalSubsystem::CloseTopModal(FName InResult, int32 InUserIndex)
{
	TArray<FActiveModal>* Stack = ModalStacks.Find(InUserIndex);
	if (Stack == nullptr || Stack->Num() == 0)
	{
		return;
	}
	// Popped BEFORE anything runs, and that ordering is what makes re-entrancy safe without a latch:
	// a result callback that closes the modal underneath, or opens a new one on top, is acting on a
	// stack this call has already finished editing. The pointer into the map is not held across the
	// callbacks either, for the same reason -- ShowNow can rehash it.
	FActiveModal Closing = MoveTemp(Stack->Last());
	Stack->RemoveAt(Stack->Num() - 1);

	if (UDreamUIModalScope* Scope = Closing.Scope.Get())
	{
		if (UDreamUINavigationStack* NavStack = UDreamUINavigationStack::Get(this))
		{
			NavStack->PopScope(Scope);
		}
	}

	// The result is taken out of the entry before the teardown, so a callback cannot see this modal
	// still holding a result to deliver.
	FDreamUIModalResultDynamicDelegate DynamicResult = Closing.DynamicResult;
	TFunction<void(FName)> NativeResult = MoveTemp(Closing.NativeResult);
	Closing.DynamicResult.Clear();
	Closing.NativeResult = nullptr;
	DestroyModal(Closing);

	DynamicResult.ExecuteIfBound(InResult);
	if (NativeResult)
	{
		NativeResult(InResult);
	}
}

void UDreamUIModalSubsystem::CloseAllModals(FName InResult, int32 InUserIndex)
{
	// Bounded by the depth taken before the first callback runs: a result handler that opens another
	// modal must not turn "close everything" into a loop that never reaches the bottom.
	for (int32 Remaining = GetModalDepth(InUserIndex); Remaining > 0 && GetModalDepth(InUserIndex) > 0; --Remaining)
	{
		CloseTopModal(InResult, InUserIndex);
	}
}

void UDreamUIModalSubsystem::DestroyModal(FActiveModal& InModal)
{
	if (UDreamWidget* Layer = InModal.Layer.Get())
	{
		// The dialog is the layer's child and the scope its component; both go with it.
		Layer->DestroyWidget();
	}
	InModal.Layer.Reset();
	InModal.Dialog.Reset();
	InModal.Scope.Reset();
}
