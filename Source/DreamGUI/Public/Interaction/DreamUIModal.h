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
 * event, the navigation scope confines gamepad focus and restores it afterwards, and Back means
 * "close with the Back result".
 *
 * Modals NEST. A second ShowModal while one is up puts its dialog on top of the first -- its own
 * scrim, its own scope, a higher sort order -- and closing it reveals the one underneath, still
 * live and still waiting for its own result. That is what the name CloseTopModal always promised;
 * what it used to do was close the only modal there could be, because a second ShowModal was queued
 * until the first finished. Queuing made the ordinary "are you sure?" raised from a settings dialog
 * arrive AFTER the dialog that asked it had already closed, with its answer delivered to nobody.
 *
 * The pieces all predate this: UUIEventBlocker blocks, UDreamUINavigationStack confines and
 * restores, the screen subsystem layers. What no piece did was compose them or carry a result back
 * to a caller -- which is the entire job of a modal service.
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

	/**
	 * Show InDialogClass modally for InUserIndex, on top of any modal that player already has up.
	 * OnResult fires exactly once.
	 *
	 * Every call takes a user index because a modal is a per-PLAYER thing: on a split screen, player
	 * one's "are you sure?" must not scrim player two's half of the display, must not take player
	 * two's gamepad focus, and must not be closed by player two's Back. There was one stack and one
	 * scrim for the whole world, so all three of those happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Modal")
	void ShowModal(TSubclassOf<UDreamUserWidget> InDialogClass, FDreamUIModalResultDynamicDelegate OnResult, int32 InUserIndex = 0);

	/** The C++ spelling of ShowModal. */
	void ShowModalNative(TSubclassOf<UDreamUserWidget> InDialogClass, TFunction<void(FName)> OnResult, int32 InUserIndex = 0);

	/** Close InUserIndex's topmost modal with InResult and deliver it; the one underneath becomes top. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Modal")
	void CloseTopModal(FName InResult, int32 InUserIndex = 0);

	/** Close every modal of InUserIndex, top down, each with InResult. For "back to the main menu". */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Modal")
	void CloseAllModals(FName InResult, int32 InUserIndex = 0);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Modal")
	bool IsModalActive(int32 InUserIndex = 0) const { return GetModalDepth(InUserIndex) > 0; }

	/** How many modals InUserIndex has stacked. Zero when none is up; the top one is what Back closes. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Modal")
	int32 GetModalDepth(int32 InUserIndex = 0) const;

	/** True when ANY player has a modal up. For code that pauses the world rather than one view. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Modal")
	bool IsAnyModalActive() const;

	/** InUserIndex's topmost dialog, or null when that player has no modal up. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Modal")
	UDreamUserWidget* GetActiveModalWidget(int32 InUserIndex = 0) const;

private:
	struct FPendingModal
	{
		TSubclassOf<UDreamUserWidget> DialogClass;
		FDreamUIModalResultDynamicDelegate DynamicResult;
		TFunction<void(FName)> NativeResult;
		int32 UserIndex = 0;
	};

	/**
	 * One modal on the stack: its scrim, its dialog, its scope, and the result its caller awaits.
	 *
	 * Weak handles rather than owning ones because nothing here owns those objects anyway -- the
	 * layer is kept alive by the widget manager while it is registered and the dialog by the layer's
	 * own children -- and because a screen torn down underneath a modal takes them with it, which is
	 * a state this has to survive reading rather than a state it can prevent.
	 */
	struct FActiveModal
	{
		TWeakObjectPtr<UDreamWidget> Layer;
		TWeakObjectPtr<UDreamUserWidget> Dialog;
		TWeakObjectPtr<UDreamUIModalScope> Scope;
		FDreamUIModalResultDynamicDelegate DynamicResult;
		TFunction<void(FName)> NativeResult;
	};

	void ShowNow(FPendingModal&& InModal);
	/** InUserIndex's stack, created empty on first use. */
	TArray<FActiveModal>& FindOrAddStack(int32 InUserIndex);
	const TArray<FActiveModal>* FindStack(int32 InUserIndex) const;
	/**
	 * Deliver InResult for a modal that never got on screen.
	 *
	 * ShowNow can fail three ways -- no dialog class, no screen root, a class that will not
	 * instantiate -- and "OnResult fires exactly once" has to hold for all of them; returning
	 * silently left an awaited OnResult unfired forever.
	 */
	void FailPendingModal(FPendingModal& InModal, FName InResult);
	/** Tear down one entry's widgets. Safe on an entry whose widgets are already gone. */
	void DestroyModal(FActiveModal& InModal);

	/**
	 * One stack per player, bottom to top. The last entry of a stack is that player's modal with
	 * focus, the scrim in front of their view, and the one their Back closes.
	 */
	TMap<int32, TArray<FActiveModal>> ModalStacks;
};
