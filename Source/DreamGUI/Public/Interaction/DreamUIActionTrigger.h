// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Engine/DataTable.h"
#include "Interaction/DreamUIActionRouter.h"
#include "DreamUIActionTrigger.generated.h"

class UDreamUINavigationScope;

/**
 * "This button is also what Confirm does."
 *
 * CommonUI spells this as UCommonButtonBase::TriggeringInputAction -- an action named on the button
 * itself, so that a screen's Confirm key and its Confirm button are one thing and cannot drift apart.
 * DreamGUI has the two halves already (the action router knows which screen owns a key; the pointer
 * pipeline knows how to click a widget) and nothing joined them, so every project wired the same key
 * to the same button by hand in a Blueprint and got to keep both copies in step itself.
 *
 * Put this beside the button. It is a behaviour rather than a field on UUIButton on purpose: that way
 * it works for any widget -- a card, a row, a custom control -- and no control class has to know the
 * action system exists.
 *
 * Lifetime, which is the part that has to be right: the binding is registered when the widget goes
 * live and dropped the moment it stops being -- hidden, disabled, destroyed, or its screen closed.
 * A key can only ever fire a button the player could have clicked.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamUIActionTrigger : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUIActionTrigger();

	/** The action row this widget answers to. Empty means nothing is bound. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetAction(const FDataTableRowHandle& InAction);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Navigation")
	const FDataTableRowHandle& GetAction()const{ return Action; }
	/** True while the action is registered with the router -- that is, while the key would reach here. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Navigation")
	bool IsActionBound()const{ return Handle.IsValidHandle(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	int32 GetUserIndex()const{ return UserIndex; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetUserIndex(int32 Value);

	/**
	 * Fire as though the widget had been clicked. Public so a screen can trigger it itself -- a
	 * tutorial pointing at the button, a test -- and so the router's callback has something to call.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void TriggerAction();

protected:
	virtual void OnEnable()override;
	virtual void OnDisable()override;
	virtual void OnUnregister()override;
	virtual void OnInteractableChanged(bool Interactable)override;

	/** Called after the click has been dispatched, for anything the widget's own handlers do not cover. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnActionTriggered"), Category = "DreamGUI-Navigation")
	void ReceiveOnActionTriggered();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Navigation")
	FDataTableRowHandle Action;
	/**
	 * Bind for every screen rather than only while this widget's screen is in front.
	 *
	 * Off by default, and that default is the whole point of scoping: a button on a page underneath a
	 * dialog must not answer the dialog's keypress. On for something that really is always available.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Navigation")
	bool bBindGlobally = false;
	/** Offer this action to prompt bars. And-ed with the action row's own flag, which can still hide it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Navigation")
	bool bDisplayInActionBar = true;
	/** Whose key. Matches the event system's user index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Navigation")
	int32 UserIndex = 0;

private:
	void RegisterAction();
	void UnregisterAction();
	/** The nearest navigation scope at or above this widget: the screen this button belongs to. */
	UDreamUINavigationScope* FindOwningScope()const;

	UFUNCTION()
	void HandleActionExecuted();

	FDreamUIActionHandle Handle;
	/** A pointer event to dispatch the synthetic click with. Never enters the event system's pointer map. */
	UPROPERTY(Transient)
	TObjectPtr<class UDreamPointerEventData> SyntheticPointerEvent = nullptr;
};
