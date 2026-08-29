// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUINavigationScope.generated.h"

class UUISelectable;
class UDreamUINavigationScope;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUINavigationScopeActivationEvent, UDreamUINavigationScope*, Scope);

/**
 * A screen, panel or dialog as far as navigation is concerned: the unit that can be pushed in front
 * of what came before, take focus, and give it back on the way out.
 *
 * DreamGUI could restrict navigation to a subtree, but there was nothing that remembered where focus
 * had been. Open a submenu and come back and focus fell to whichever selectable happened to have
 * registered first -- registration order, invisible to the designer and different in a packaged build
 * than in the editor. A scope keeps that answer: where focus should start, and where it was when the
 * scope last handed control away.
 *
 * Push order is the stack, not the widget hierarchy, so a dialog opened from a page correctly sits in
 * front of it whether or not it is parented under it.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamUINavigationScope : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUINavigationScope();

	/** Push this scope: it takes focus, and the scope under it remembers where focus was. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void ActivateScope();
	/** Pop this scope. Focus returns to whatever the scope below it remembered. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void DeactivateScope();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool IsScopeActive()const{ return bIsScopeActive; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	int32 GetUserIndex()const{ return UserIndex; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetUserIndex(int32 Value){ UserIndex = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool GetConfineNavigation()const{ return bConfineNavigation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetConfineNavigation(bool Value){ bConfineNavigation = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	UUISelectable* GetDesiredFocusTarget()const{ return DesiredFocusTarget.Get(); }
	/**
	 * Defined in the .cpp, unlike its neighbours, because assigning to a TWeakObjectPtr needs the
	 * pointee to be COMPLETE -- the operator has to prove the conversion -- and UUISelectable is only
	 * forward declared here. The getter can stay inline: Get() returns T* and never asks.
	 *
	 * It compiled inline for a long time on borrowed completeness: some neighbour in the same unity
	 * blob included UISelectable.h, so the definition happened to be in scope. Deleting an unrelated
	 * set of files reshuffled the blobs and it stopped being. Including the header here would fix it
	 * too, and would hand that include to everyone who includes this one.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetDesiredFocusTarget(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool GetRestoreLastFocus()const{ return bRestoreLastFocus; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetRestoreLastFocus(bool Value){ bRestoreLastFocus = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool GetActivateWhenEnabled()const{ return bActivateWhenEnabled; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetActivateWhenEnabled(bool Value){ bActivateWhenEnabled = Value; }

	/**
	 * Where focus belongs when this scope takes over: the remembered spot if it is still usable and
	 * restoring is on, then the authored target, then the first navigable control inside the scope.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	UUISelectable* ResolveFocusTarget()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	UUISelectable* GetRememberedFocus()const{ return RememberedFocus.Get(); }
	/** Record where focus is, so a later re-activation can put it back. Called by the stack. */
	void RememberFocus(UUISelectable* InSelectable);

	/** Fired after the stack has pushed/popped this scope, not when the call is made. */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Navigation")
	FDreamUINavigationScopeActivationEvent OnScopeActivated;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Navigation")
	FDreamUINavigationScopeActivationEvent OnScopeDeactivated;

	/** Called by the stack once the push/pop has actually happened. */
	void NotifyScopeActivated();
	void NotifyScopeDeactivated();

	/**
	 * Deal with Back yourself -- discard an edit, step back a page, ask for confirmation.
	 * @return true to stop Back going any further down the stack. Returning false lets it reach this
	 *         scope's close behaviour, and then the screen underneath.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DreamGUI-Navigation")
	bool HandleBackAction();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool GetCloseOnBack()const{ return bCloseOnBack; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetCloseOnBack(bool Value){ bCloseOnBack = Value; }

protected:
	virtual void OnEnable()override;
	virtual void OnDisable()override;
	virtual void OnUnregister()override;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnScopeActivated"), Category = "DreamGUI-Navigation")
	void ReceiveOnScopeActivated();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnScopeDeactivated"), Category = "DreamGUI-Navigation")
	void ReceiveOnScopeDeactivated();

	/** Push as soon as the widget goes live. Off for a scope a Blueprint opens and closes itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	bool bActivateWhenEnabled = true;
	/**
	 * While this is the top scope, directional navigation cannot reach anything outside it. Navigation
	 * only -- a pointer can still click straight through to what is behind, which is what UIEventBlocker
	 * is for; conflating the two here would silently take the mouse away from a scope that only wanted
	 * to keep the gamepad in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	bool bConfineNavigation = true;
	/**
	 * Close when Back reaches this screen and nothing handled it. Off for a root screen, which Back
	 * should pass straight through rather than shutting down the UI.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	bool bCloseOnBack = true;
	/** Come back to where focus was last time rather than to the authored target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	bool bRestoreLastFocus = true;
	/** Where focus starts. Empty means the first navigable control inside this scope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	TWeakObjectPtr<UUISelectable> DesiredFocusTarget = nullptr;
	/** Which player's focus this scope governs; matches the event system's user index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	int32 UserIndex = 0;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI-Navigation", AdvancedDisplay)
	bool bIsScopeActive = false;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI-Navigation", AdvancedDisplay)
	TWeakObjectPtr<UUISelectable> RememberedFocus = nullptr;
};
