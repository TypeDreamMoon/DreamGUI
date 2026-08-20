// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "DreamUMGWidgetInteraction.generated.h"

class UDreamUMGWidget;
class UDreamUMGWidgetInteraction;

UCLASS()
class DREAMGUI_API UDreamUMGWidgetInteractionManager : public UObject
{
	GENERATED_BODY()
public:

	static UDreamUMGWidgetInteractionManager* Instance;
	struct FInteractionContainer
	{
		TArray<UDreamUMGWidgetInteraction*> AllInteractions;
		UDreamUMGWidgetInteraction* CurrentInteraction = nullptr;
	};
	TMap<int, FInteractionContainer> MapVirtualUserIndexToInteraction;
};

/**
 * Perform a raycaster and interaction for DreamUMGWidget, which shows UMG widget.
 * This component should be placed on a actor which have a DreamUMGWidget component.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamUMGWidgetInteraction : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerScrollInterface
{
	GENERATED_BODY()
	
public:	
	UDreamUMGWidgetInteraction();
	
protected:
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		bool bAllowEventBubbleUp = false;
	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay)
		UDreamUMGWidgetInteractionManager* Helper = nullptr;

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;

	UDreamPointerEventData* CurrentPointerEventData = nullptr;

public:

	// Begin ActorComponent interface
	virtual void Awake() override;
	virtual void OnDestroy() override;
	virtual void Tick(float DeltaTime) override;
	// End UActorComponent

	/**
	 * Presses a key as if the mouse/pointer were the source of it.  Normally you would just use
	 * Left/Right mouse button for the Key.  However - advanced uses could also be imagined where you
	 * send other keys to signal widgets to take special actions if they're under the cursor.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual void PressPointerKey(FKey Key);

	/**
	 * Releases a key as if the mouse/pointer were the source of it.  Normally you would just use
	 * Left/Right mouse button for the Key.  However - advanced uses could also be imagined where you
	 * send other keys to signal widgets to take special actions if they're under the cursor.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual void ReleasePointerKey(FKey Key);

	/**
	 * Press a key as if it had come from the keyboard.  Avoid using this for 'a-z|A-Z', things like
	 * the Editable Textbox in Slate expect OnKeyChar to be called to signal a specific character being
	 * send to the widget.  So for those cases you should use SendKeyChar.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual bool PressKey(FKey Key, bool bRepeat = false);

	/**
	 * Releases a key as if it had been released by the keyboard.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual bool ReleaseKey(FKey Key);

	/**
	 * Does both the press and release of a simulated keyboard key.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual bool PressAndReleaseKey(FKey Key);

	/**
	 * Transmits a list of characters to a widget by simulating a OnKeyChar event for each key listed in
	 * the string.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual bool SendKeyChar(FString Characters, bool bRepeat = false);

	/**
	 * Sends a scroll wheel event to the widget under the last hit result.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual void ScrollWheel(float ScrollDelta);

	/**
	 * Returns true if a widget under the hit result is interactive.  e.g. Slate widgets
	 * that return true for IsInteractable().
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool IsOverInteractableWidget() const;

	/**
	 * Returns true if a widget under the hit result is focusable.  e.g. Slate widgets that
	 * return true for SupportsKeyboardFocus().
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool IsOverFocusableWidget() const;

	/**
	 * Returns true if a widget under the hit result is has a visibility that makes it hit test
	 * visible.  e.g. Slate widgets that return true for GetVisibility().IsHitTestVisible().
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool IsOverHitTestVisibleWidget() const;

	/**
	 * Gets the widget path for the slate widgets under the last hit result.
	 */
	const FWeakWidgetPath& GetHoveredWidgetPath() const;

	/**
	 * Gets the last hit location on the widget in 2D, local pixel units of the render target.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FVector2D Get2DHitLocation() const;

	/**
	 * Set custom hit result.  This is only taken into account if InteractionSource is set to EWidgetInteractionSource::Custom.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetCustomHitResult(const FDreamUIHitResult& HitResult);

	/**
	 * Set the focus target of the virtual user managed by this component
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetFocus(UWidget* FocusWidget);

protected:
	/**
	 * Represents the virtual user in slate.  When this component is registered, it gets a handle to the
	 * virtual slate user it will be, so virtual slate user 0, is probably real slate user 8, as that's the first
	 * index by default that virtual users begin - the goal is to never have them overlap with real input
	 * hardware as that will likely conflict with focus states you don't actually want to change - like where
	 * the mouse and keyboard focus input (the viewport), so that things like the player controller receive
	 * standard hardware input.
	 */
	TSharedPtr<class FSlateVirtualUserHandle> VirtualUser;

public:

	/**
	 * Represents the Virtual User Index.  Each virtual user should be represented by a different
	 * index number, this will maintain separate capture and focus states for them.  Each
	 * controller or finger-tip should get a unique PointerIndex.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI, meta = (ClampMin = "0", ExposeOnSpawn = true))
	int32 VirtualUserIndex;


protected:
	int32 PrevPointerIndex = -1;

	// Gets the key and char codes for sending keys for the platform.
	void GetKeyAndCharCodes(const FKey& Key, bool& bHasKeyCode, uint32& KeyCode, bool& bHasCharCode, uint32& CharCode);

	/** Is it safe for this interaction component to run?  Might not be in a server situation with no slate application. */
	bool CanSendInput();

	/** Performs the simulation of pointer movement.  Does not run if bEnableHitTesting is set to false. */
	void SimulatePointerMovement();

	struct FWidgetTraceResult
	{
		FWidgetTraceResult()
			: LocalHitLocation(FVector2D::ZeroVector)
			, HitWidgetPath()
		{
		}

		FVector2D LocalHitLocation;
		FWidgetPath HitWidgetPath;
	};

	/** Returns true if the inteaction component can interact with the supplied widget component */
	bool CanInteractWithComponent(UDreamUMGWidget* Component) const;

protected:

	/** The last widget path under the hit result. */
	FWeakWidgetPath LastWidgetPath;

	/** The modifier keys to simulate during key presses. */
	FModifierKeysState ModifierKeys;

	/** The current set of pressed keys we maintain the state of. */
	TSet<FKey> PressedKeys;

	/** Stores the custom hit result set by the player. */
	UPROPERTY(Transient)
	FDreamUIHitResult CustomHitResult;

	/** The 2D location on the widget component that was hit. */
	UPROPERTY(Transient)
	FVector2D LocalHitLocation;

	/** The last 2D location on the widget component that was hit. */
	UPROPERTY(Transient)
	FVector2D LastLocalHitLocation;

	/** The widget component we're currently hovering over. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamUMGWidget> WidgetComponent;

	/** Are we hovering over any interactive widgets. */
	UPROPERTY(Transient)
	bool bIsHoveredWidgetInteractable;

	/** Are we hovering over any focusable widget? */
	UPROPERTY(Transient)
	bool bIsHoveredWidgetFocusable;

	/** Are we hovered over a widget that is hit test visible? */
	UPROPERTY(Transient)
	bool bIsHoveredWidgetHitTestVisible;

private:

	/** Returns the path to the widget that is currently beneath the pointer */
	FWidgetPath DetermineWidgetUnderPointer();
};
