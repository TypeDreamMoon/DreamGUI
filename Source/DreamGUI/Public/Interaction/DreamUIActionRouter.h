// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Interaction/DreamUIInputAction.h"
#include "DreamUIActionRouter.generated.h"

class UDreamUINavigationScope;

/** Identifies one live binding. Handed back by RegisterAction and used to take it away again. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIActionHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	int32 Id = INDEX_NONE;

	bool IsValidHandle()const{ return Id != INDEX_NONE; }
	bool operator==(const FDreamUIActionHandle& Other)const{ return Id == Other.Id; }
};

DECLARE_DYNAMIC_DELEGATE(FDreamUIActionExecutedDelegate);

/** One live binding as a prompt bar needs to see it: already resolved for the device in use. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIActionBinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	FDreamUIActionHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	FKey Key;
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	float HoldTime = 0.0f;
	/** 0..1 through the hold; always 0 for an action that fires on press. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	float HoldProgress = 0.0f;
};

/**
 * Which screen gets told about which key.
 *
 * Before this, the answer was a static array of FKeys in the input actor's cpp: a project could not
 * add an action, rebind one, or draw a prompt for one without editing the plugin, and there was no
 * notion of a key belonging to the screen currently in front. Bindings here live and die with the
 * screen that registered them, and only the screen on top is offered the key -- which is what makes
 * "Delete" mean the dialog's delete and not the list's while that dialog is open.
 *
 * Ticks, because a hold has to fire when the time is up rather than when the player lets go.
 */
UCLASS()
class DREAMGUI_API UDreamUIActionRouter : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer)const override;
	virtual void Deinitialize()override;
	virtual void Tick(float DeltaTime)override;
	virtual TStatId GetStatId()const override;

	static UDreamUIActionRouter* Get(const UObject* WorldContextObject);

	/**
	 * Bind InAction for as long as InScope is the screen on top. A null scope binds globally, which is
	 * for something that must work whatever is open and is deliberately the lower priority of the two.
	 * @param bDisplayInActionBar	And-ed with the action's own flag; a caller can hide but not reveal.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	FDreamUIActionHandle RegisterAction(UDreamUINavigationScope* InScope, const FDataTableRowHandle& InAction, FDreamUIActionExecutedDelegate InCallback, int32 InUserIndex = 0, bool bDisplayInActionBar = true);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void UnregisterAction(const FDreamUIActionHandle& InHandle);
	/** Drop every binding InScope registered. Called when a screen goes away. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void UnregisterScope(UDreamUINavigationScope* InScope);

	/**
	 * Offer a key to the bindings for InUserIndex.
	 * @return true when a binding took it, which the caller must read as "do not also treat this as
	 *         navigation" -- otherwise a Confirm bound to Enter would both fire and press whatever
	 *         navigation happens to be sitting on.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool HandleKey(int32 InUserIndex, const FKey& InKey, bool bPressed);

	/** Live bindings for a prompt bar, most recently registered first, resolved for the device in use. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void GetDisplayBindings(int32 InUserIndex, TArray<FDreamUIActionBinding>& OutBindings)const;
	/** 0..1 through the hold on one binding. Zero when it is not being held or fires on press. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	float GetHoldProgress(const FDreamUIActionHandle& InHandle)const;

	/** Fired when the display set for a user changes, so a bar rebuilds then and not every frame. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIActionBindingsChangedDelegate, int32);
	FDreamUIActionBindingsChangedDelegate& GetBindingsChangedEvent(){ return BindingsChangedEvent; }

private:
	/**
	 * A registration plus whatever the key is doing right now. Deliberately not a UPROPERTY: the scope
	 * is a weak pointer and a dynamic delegate holds its object weakly too, so nothing here keeps a
	 * dead screen alive.
	 */
	struct FBindingEntry
	{
		int32 Id = INDEX_NONE;
		int32 UserIndex = 0;
		TWeakObjectPtr<UDreamUINavigationScope> Scope;
		FDreamUIInputActionData Action;
		FDreamUIActionExecutedDelegate Callback;
		bool bDisplayInActionBar = true;

		/** Hold state. bHoldFired stops one long press firing again on every tick after the threshold. */
		bool bHeld = false;
		float HeldSeconds = 0.0f;
		bool bHoldFired = false;
	};

	TArray<FBindingEntry> Bindings;
	int32 NextId = 0;
	FDreamUIActionBindingsChangedDelegate BindingsChangedEvent;

	/** True when this binding's screen is the one in front, or it is global. */
	bool IsEligible(const FBindingEntry& InEntry)const;
	/** Drop bindings whose screen has been destroyed without unregistering. */
	void RemoveStaleBindings();
	FBindingEntry* FindBinding(const FDreamUIActionHandle& InHandle);
	const FBindingEntry* FindBinding(const FDreamUIActionHandle& InHandle)const;
	static void Execute(FBindingEntry& InEntry);
};
