// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "DreamUIModal.generated.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamUIModalSubsystem;

DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIModalResultDynamicDelegate, FName, Result);

/**
 * The navigation scope a modal wears. Exists to give Back one meaning here -- close the modal with
 * the "Back" result -- and to keep a runtime-added scope from pushing itself twice (the base class
 * auto-pushes on enable; the subsystem pushes explicitly, once, after configuring it).
 */
UCLASS(NotBlueprintable, HideDropdown)
class DREAMGUI_API UDreamUIModalScope : public UDreamUINavigationScope
{
	GENERATED_BODY()

public:
	UDreamUIModalScope();

	virtual bool HandleBackAction_Implementation() override;

	TWeakObjectPtr<UDreamUIModalSubsystem> OwnerSubsystem;
};

/**
 * Show a dialog, await its result, keep the world honest underneath: the scrim eats every pointer
 * event, the navigation scope confines gamepad focus and restores it afterwards, Back means
 * "close with the Back result", and a second ShowModal while one is up QUEUES rather than stacking
 * two dialogs into a fight over focus.
 *
 * The pieces all predate this: UUIEventBlocker blocks, UDreamUINavigationStack confines and
 * restores, the screen subsystem layers. What no piece did was compose them, carry a result to a
 * caller, or hold the line at one-at-a-time -- which is the entire job of a modal service.
 *
 * The dialog itself is any UDreamUserWidget class. Its buttons end the modal by calling
 * CloseTopModal with whatever result name they mean ("Confirm", "Cancel", ...).
 */
UCLASS()
class DREAMGUI_API UDreamUIModalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI Modal Subsystem"), Category = "DreamGUI|Modal")
	static UDreamUIModalSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	/** Show InDialogClass modally. Queued when a modal is already up. OnResult fires exactly once. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Modal")
	void ShowModal(TSubclassOf<UDreamUserWidget> InDialogClass, FDreamUIModalResultDynamicDelegate OnResult);

	/** The C++ spelling of ShowModal. */
	void ShowModalNative(TSubclassOf<UDreamUserWidget> InDialogClass, TFunction<void(FName)> OnResult);

	/** Close the visible modal with InResult, deliver the result, and show the next queued one. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Modal")
	void CloseTopModal(FName InResult);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Modal")
	bool IsModalActive() const { return IsValid(ActiveDialog); }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Modal")
	UDreamUserWidget* GetActiveModalWidget() const { return ActiveDialog; }

private:
	struct FPendingModal
	{
		TSubclassOf<UDreamUserWidget> DialogClass;
		FDreamUIModalResultDynamicDelegate DynamicResult;
		TFunction<void(FName)> NativeResult;
	};

	void ShowNow(FPendingModal&& InModal);
	/**
	 * Deliver InResult for a modal that never got on screen, and let the queue carry on.
	 *
	 * ShowNow can fail three ways -- no dialog class, no screen root, a class that will not
	 * instantiate -- and "OnResult fires exactly once" has to hold for all of them, as does the
	 * queue: a modal that fails to open is not a modal that is up, and nothing else pumps the queue.
	 */
	void FailPendingModal(FPendingModal& InModal, FName InResult);
	/** Show queued modals until one actually opens, or the queue runs dry. Re-entrancy-safe. */
	void ShowNextQueuedModal();
	void DestroyActiveModal();

	TArray<FPendingModal> Queue;

	FDreamUIModalResultDynamicDelegate ActiveDynamicResult;
	TFunction<void(FName)> ActiveNativeResult;

	/** Scrim + dialog holder: full-rect, event-blocking, on its own canvas above the page band. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> ModalLayer;
	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> ActiveDialog;
	UPROPERTY(Transient)
	TObjectPtr<UDreamUIModalScope> ActiveScope;
	/** Re-entrancy latch: a result callback that opens another modal must land in the queue. */
	bool bClosingModal = false;
	/** Latch for ShowNextQueuedModal: a ShowNow that fails asks for the next one from inside the pump. */
	bool bPumpingQueue = false;
};
