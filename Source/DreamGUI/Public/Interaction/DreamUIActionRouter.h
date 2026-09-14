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
	/**
	 * Ticks while the game is paused, because this tick is what advances a hold -- and hold-to-confirm
	 * lives on pause menus ("hold to quit", "hold to restart"). Without it the key press registered,
	 * the prompt filled to zero and the action never fired.
	 */
	virtual bool IsTickableWhenPaused()const override { return true; }

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
	/**
	 * The same, with the modifier keys stated rather than read from the player.
	 *
	 * HandleKey asks the player controller what is held, which is right for a key arriving from real
	 * input and useless without one -- a test, a replay, or a virtual keyboard knows its own chord and
	 * has no controller to ask.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool HandleKeyWithModifiers(int32 InUserIndex, const FKey& InKey, bool bPressed, bool bShiftDown, bool bCtrlDown, bool bAltDown, bool bCmdDown);

	/** Live bindings for a prompt bar, most recently registered first, resolved for the device in use. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void GetDisplayBindings(int32 InUserIndex, TArray<FDreamUIActionBinding>& OutBindings)const;
	/** 0..1 through the hold on one binding. Zero when it is not being held or fires on press. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	float GetHoldProgress(const FDreamUIActionHandle& InHandle)const;

	/**
	 * Re-read the keys behind every binding's Enhanced Input action.
	 *
	 * They are resolved once, when a binding first has to answer for a key, because a mapping context
	 * is normally pushed before any screen opens and never touched again. A project that swaps contexts
	 * while screens are open -- a remapping menu, a vehicle mode with its own bindings -- calls this
	 * afterwards, and the prompt bars rebuild with the new keys.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void RefreshInputActionKeys();

	/** Fired when the display set for a user changes, so a bar rebuilds then and not every frame. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIActionBindingsChangedDelegate, int32);
	FDreamUIActionBindingsChangedDelegate& GetBindingsChangedEvent(){ return BindingsChangedEvent; }

	/**
	 * Fired while a hold is running, and once with 0 when it ends without firing.
	 *
	 * Pushed rather than polled: the router is the only thing that knows a key is down, and a prompt bar
	 * that ticked to ask would pay for it on every frame of every screen that has no hold at all. The
	 * progress was computed into FDreamUIActionBinding::HoldProgress from the start, but the only thing
	 * that read it was a full rebuild -- so the filling ring on a hold-to-confirm never moved off 0.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamUIActionHoldProgressDelegate, FDreamUIActionHandle, float);
	FDreamUIActionHoldProgressDelegate& GetHoldProgressEvent(){ return HoldProgressEvent; }

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

		/**
		 * The keys the row's Enhanced Input action is mapped to, resolved once from this user's mapping
		 * contexts. Empty for a row with no action, which is most of them.
		 */
		TArray<FKey> InputActionKeys;
		bool bInputActionKeysResolved = false;
	};

	TArray<FBindingEntry> Bindings;
	int32 NextId = 0;
	/** Keys already reported as claimed by both an action row and an Input Action. One warning each. */
	TSet<FKey> ReportedInputActionConflictKeys;
	FDreamUIActionBindingsChangedDelegate BindingsChangedEvent;
	FDreamUIActionHoldProgressDelegate HoldProgressEvent;

	/** True when this binding's screen is the one in front, or it is global. */
	bool IsEligible(const FBindingEntry& InEntry)const;
	/** Which modifier keys this player is holding right now. All false when there is no controller. */
	void GetModifierKeyState(int32 InUserIndex, bool& bOutShift, bool& bOutCtrl, bool& bOutAlt, bool& bOutCmd)const;
	/**
	 * Fill InEntry.InputActionKeys from the local player's mapping contexts, once.
	 *
	 * Lazily, and only for a row that names an Input Action: querying costs a walk of every mapping in
	 * every context the player has, and almost no row has one.
	 */
	void ResolveInputActionKeys(FBindingEntry& InEntry)const;
	/** Drop bindings whose screen has been destroyed without unregistering. */
	void RemoveStaleBindings();
	/**
	 * Offer a key to the FOCUSED widget of InUserIndex and everything above it, before the named
	 * bindings get their turn. True when a widget kept it, which spends the key.
	 */
	bool DispatchKeyToFocusedWidget(int32 InUserIndex, const FKey& InKey, bool bPressed,
		bool bShiftDown, bool bCtrlDown, bool bAltDown, bool bCmdDown);
	FBindingEntry* FindBinding(const FDreamUIActionHandle& InHandle);
	const FBindingEntry* FindBinding(const FDreamUIActionHandle& InHandle)const;
	static void Execute(FBindingEntry& InEntry);
};
