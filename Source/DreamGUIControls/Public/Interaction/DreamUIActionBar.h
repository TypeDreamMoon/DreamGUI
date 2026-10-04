// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Interaction/DreamUIActionRouter.h"
#include "DreamUIActionBar.generated.h"

class UDreamUserWidget;
class UDreamImage;
class UDreamText;
class UDreamWidget;

/**
 * One prompt on the bar: the key glyph or name, and the words beside it.
 *
 * Put this on the root of the entry prefab and point it at the two or three widgets that draw the
 * prompt. Filling those in is all most projects need; anything more elaborate overrides the Blueprint
 * event, which is called after the defaults have been applied and can undo any of them.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUICONTROLS_API UDreamUIActionBarEntry : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUIActionBarEntry();

	/** Fill this entry in from a binding. Called by the bar right after the prefab is loaded. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetBinding(const FDreamUIActionBinding& InBinding);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	const FDreamUIActionBinding& GetBinding()const{ return Binding; }

	/** 0..1 through this entry's hold, kept live by the router. Always 0 for an action that fires on press. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	float GetHoldProgress()const{ return Binding.HoldProgress; }

	/** Wiring, for an entry assembled in code rather than authored as a prefab. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetLabelText(UDreamText* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetIconImage(UDreamImage* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetKeyText(UDreamText* Value);

protected:
	virtual void OnEnable()override;
	virtual void OnDisable()override;
	virtual void OnUnregister()override;

	/** Called after the defaults have been written, so it can override any of them. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnBindingChanged"), Category = "DreamGUI-Navigation")
	void ReceiveOnBindingChanged(const FDreamUIActionBinding& InBinding);
	/**
	 * Called every time this entry's hold progress moves, and once with 0 when the hold ends.
	 *
	 * This is where a filling ring is drawn. Pushed from the router rather than ticked: an entry for an
	 * action that fires on press never hears from it at all.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnHoldProgressChanged"), Category = "DreamGUI-Navigation")
	void ReceiveOnHoldProgressChanged(float InHoldProgress);

	/** What the action does, in words. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	TWeakObjectPtr<UDreamText> LabelText = nullptr;
	/** The key glyph. Hidden when the action has no icon for the device in use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	TWeakObjectPtr<UDreamImage> IconImage = nullptr;
	/**
	 * The key's name, for when there is no glyph. Shown exactly when the icon is not, so a prompt is
	 * never blank and never says the same thing twice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	TWeakObjectPtr<UDreamText> KeyText = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI-Navigation", AdvancedDisplay)
	FDreamUIActionBinding Binding;

private:
	void SubscribeToRouter();
	void UnsubscribeFromRouter();
	void HandleHoldProgressChanged(FDreamUIActionHandle InHandle, float InProgress);

	FDelegateHandle HoldProgressHandle;
	TWeakObjectPtr<UDreamUIActionRouter> SubscribedHoldRouter;
};

/**
 * The row of "A: Confirm  B: Back" prompts along the bottom of a screen.
 *
 * It reads the router rather than being told what to show, so it cannot drift out of step with what
 * the keys actually do -- the failure mode of every hand-authored prompt bar, where a screen changes
 * a binding and the hint underneath keeps advertising the old one.
 *
 * Rebuilt only when the answer changes: when bindings come or go, when the player switches device, and
 * when they pick up another model of pad (the glyphs are per model). Polling would mean reloading a
 * prefab per entry per frame.
 *
 * After the screen's own actions come the shoulder buttons' two prompts, "previous tab" and "next tab",
 * while the player has a tab view those keys would switch (DreamUIKeyRouting::FindTabSwitchTarget,
 * asked at the rebuild): the project's PreviousTabKeys and NextTabKeys, the first of each for the device
 * in use -- none on a device the tables have no key for.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUICONTROLS_API UDreamUIActionBar : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUIActionBar();

	/** Throw the entries away and build them again from the router. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	virtual void Rebuild();

	/** Whose prompts: UserIndex when it names a player, else the player who owns this widget (UDreamWidget::GetOwningPlayerIndex). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	int32 GetUserIndex()const;
	/** -1: the owning player. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetUserIndex(int32 Value);
	/** The entry widgets currently on the bar, in the order they are shown. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	const TArray<UDreamWidget*>& GetEntryWidgets()const{ return EntryWidgets; }
	/**
	 * What the last rebuild put on the bar, in order, MaxEntries at most: one prompt per entry -- the same list
	 * with no entry prefab to draw it, which is how a test or a bar drawn some other way reads it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	const TArray<FDreamUIActionBinding>& GetPrompts()const{ return Prompts; }

protected:
	virtual void OnEnable()override;
	virtual void OnDisable()override;
	virtual void OnUnregister()override;

	/** Loaded once per prompt, as a child of this widget. Nothing is shown without one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	TSubclassOf<UDreamUserWidget> EntryClass = nullptr;
	/** Whose prompts these are. Matches the event system's user index. -1, the default: the player who owns this widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation", meta = (ClampMin = "-1"))
	int32 UserIndex = -1;
	/** Guard against a runaway table filling the screen with prompts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation", meta = (ClampMin = "1"))
	int32 MaxEntries = 8;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidget>> SpawnedEntries;
	/** Non-owning view of the same widgets, so Blueprint can read the row without a copy per call. */
	UPROPERTY(Transient)
	TArray<UDreamWidget*> EntryWidgets;

private:
	void SubscribeToSources();
	void UnsubscribeFromSources();
	void HandleBindingsChanged(int32 InUserIndex);
	void HandleInputDeviceChanged(EDreamUIInputDevice InDevice);
	void HandleGamepadModelChanged(EDreamUIGamepadModel InModel);
	void ClearEntries();
	/** The two tab-switch prompts, after InOutPrompts, while player InUserIndex has a tab view to switch. */
	void AppendTabSwitchPrompts(int32 InUserIndex, TArray<FDreamUIActionBinding>& InOutPrompts) const;

	/** See GetPrompts. */
	UPROPERTY(Transient)
	TArray<FDreamUIActionBinding> Prompts;

	FDelegateHandle BindingsChangedHandle;
	FDelegateHandle InputDeviceChangedHandle;
	FDelegateHandle GamepadModelChangedHandle;
	TWeakObjectPtr<UDreamUIActionRouter> SubscribedRouter;
	TWeakObjectPtr<UDreamEventSystem> SubscribedEventSystem;
};
