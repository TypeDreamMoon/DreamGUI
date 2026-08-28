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
class DREAMGUI_API UDreamUIActionBarEntry : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUIActionBarEntry();

	/** Fill this entry in from a binding. Called by the bar right after the prefab is loaded. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetBinding(const FDreamUIActionBinding& InBinding);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	const FDreamUIActionBinding& GetBinding()const{ return Binding; }

	/** Wiring, for an entry assembled in code rather than authored as a prefab. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetLabelText(UDreamText* Value){ LabelText = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetIconImage(UDreamImage* Value){ IconImage = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetKeyText(UDreamText* Value){ KeyText = Value; }

protected:
	/** Called after the defaults have been written, so it can override any of them. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnBindingChanged"), Category = "DreamGUI-Navigation")
	void ReceiveOnBindingChanged(const FDreamUIActionBinding& InBinding);

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
};

/**
 * The row of "A: Confirm  B: Back" prompts along the bottom of a screen.
 *
 * It reads the router rather than being told what to show, so it cannot drift out of step with what
 * the keys actually do -- the failure mode of every hand-authored prompt bar, where a screen changes
 * a binding and the hint underneath keeps advertising the old one.
 *
 * Rebuilt only when the answer changes: when bindings come or go, and when the player switches
 * device. Polling would mean reloading a prefab per entry per frame.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamUIActionBar : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUIActionBar();

	/** Throw the entries away and build them again from the router. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	virtual void Rebuild();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	int32 GetUserIndex()const{ return UserIndex; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetUserIndex(int32 Value);
	/** The entry widgets currently on the bar, in the order they are shown. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	const TArray<UDreamWidget*>& GetEntryWidgets()const{ return EntryWidgets; }

protected:
	virtual void OnEnable()override;
	virtual void OnDisable()override;
	virtual void OnUnregister()override;

	/** Loaded once per prompt, as a child of this widget. Nothing is shown without one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	TSubclassOf<UDreamUserWidget> EntryClass = nullptr;
	/** Whose prompts these are. Matches the event system's user index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	int32 UserIndex = 0;
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
	void ClearEntries();

	FDelegateHandle BindingsChangedHandle;
	FDelegateHandle InputDeviceChangedHandle;
	TWeakObjectPtr<UDreamUIActionRouter> SubscribedRouter;
	TWeakObjectPtr<UDreamEventSystem> SubscribedEventSystem;
};
