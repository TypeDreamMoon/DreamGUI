// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "Input/Events.h"
#include "Core/DreamUIBehaviour.h"
#include "Subsystems/WorldSubsystem.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "DreamUMGWidgetInteraction.generated.h"

class UDreamUMGWidget;
class UDreamUMGWidgetInteraction;
class UWidget;

/**
 * The one place that answers "which of the components sharing a virtual Slate user is driving it", per world.
 *
 * A virtual user is a Slate-wide resource keyed by index, and nothing stops several DreamUMGWidgetInteraction
 * components from being authored with the same VirtualUserIndex -- several UMG panels in a level sharing one
 * simulated cursor is the intended use. Only one of them may hold that cursor at a time, so the hover that arrives
 * first claims CurrentInteraction and the rest stand down until it is given back.
 *
 * A world subsystem, holding its components weakly. It used to be a rooted object in a static, shared by every
 * world and made and unmade by hand as components enrolled and left: two worlds' components contended for one
 * container, and the first un-enrolled component to go could destroy it out from under the rest.
 */
UCLASS()
class DREAMGUICONTROLS_API UDreamUMGWidgetInteractionManager : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/** InWorldContext's world's manager, or null in a world that has none (a preview, a world-less authoring tree). */
	static UDreamUMGWidgetInteractionManager* Get(const UObject* InWorldContext);

	struct FInteractionContainer
	{
		TArray<TWeakObjectPtr<UDreamUMGWidgetInteraction>> AllInteractions;
		TWeakObjectPtr<UDreamUMGWidgetInteraction> CurrentInteraction;
	};
	TMap<int, FInteractionContainer> MapVirtualUserIndexToInteraction;
};

/**
 * Perform a raycaster and interaction for DreamUMGWidget, which shows UMG widget.
 * This component should be placed on a actor which have a DreamUMGWidget component.
 *
 * Every DreamGUI pointer over the surface is forwarded as a pointer of its own: the mouse as Slate's cursor
 * (ETouchIndex::CursorPointerIndex), and each finger as a TOUCH, with its own finger index -- a press that
 * reaches OnTouchStarted, moves while it is down with the first one flagged as
 * Slate's first move, and a release with no force -- which is what Slate's own touch input sends, and what a
 * UMG ScrollBox pans on. A second finger's release lets go of the second finger only.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUICONTROLS_API UDreamUMGWidgetInteraction : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerDoubleClickInterface
	, public IDreamPointerScrollInterface
{
	GENERATED_BODY()

public:
	UDreamUMGWidgetInteraction();

protected:
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		bool bAllowEventBubbleUp = false;
	/** The manager of the world this component enrolled in, or null when it never enrolled. */
	TWeakObjectPtr<UDreamUMGWidgetInteractionManager> Helper;

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	/**
	 * The second press of a double click, which the event system delivers in place of that press's
	 * down. It is still a press for the UMG widget underneath, and it is forwarded as one, exactly as
	 * OnPointerDown forwards the first: UE's own UWidgetInteractionComponent routes every press as a
	 * pointer down and never as a double click, and so has this -- without this, the second click of
	 * any quick pair on a world-space UMG panel would lose its press and never click.
	 */
	virtual bool OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;

	/**
	 * The first pointer still over this surface -- the one the key, wheel and focus calls below act for -- held
	 * weakly: the player that owns it retires pointers -- a lifted finger, a player who left -- and a raw pointer to
	 * one outlived it here and was read every tick.
	 */
	TWeakObjectPtr<UDreamPointerEventData> CurrentPointerEventData;

	/**
	 * One DreamGUI pointer as this bridge forwards it: the mouse, or one finger. Per pointer because Slate keys a
	 * press, a capture and a drag by pointer index -- one set of state for the whole surface let a second finger's
	 * release let go of the first, and sent every finger as the mouse.
	 */
	struct FForwardedPointer
	{
		/** The pointer itself. Weak, for the reason CurrentPointerEventData is. */
		TWeakObjectPtr<UDreamPointerEventData> Pointer;
		/** Its index on the virtual Slate user: the finger for a touch, the cursor's index for the mouse. */
		uint32 SlatePointerIndex = 0;
		/** A finger: sent to Slate as touch events. */
		bool bTouch = false;
		/** Over this surface: entered, and not yet exited. */
		bool bHovering = false;
		/**
		 * A press made on this surface is still held. While it is, the pointer is followed on the plane it pressed,
		 * moves go on being forwarded, and an exit waits for the release.
		 */
		bool bPressing = false;
		/** An exit that arrived while the press was held, acted on when it is let go. */
		bool bExitPendingRelease = false;
		/** A finger that went down here and has not been lifted: an active touch on the Slate side. */
		bool bTouchDown = false;
		/** That finger has not moved since it went down, so its next move is Slate's first move. */
		bool bAwaitingFirstMove = false;
		/** The mouse buttons this pointer holds down on the surface. */
		TSet<FKey> PressedKeys;
		/** Where the pointer was when it was last sent, so a finger that has not moved sends no move. */
		FVector LastSentPointerPosition = FVector::ZeroVector;
		/** This pointer's hit on the widget, in its pixels, now and at the previous trace. */
		FVector2D LocalHitLocation = FVector2D::ZeroVector;
		FVector2D LastLocalHitLocation = FVector2D::ZeroVector;
		/** The widgets under this pointer at its last trace, which a press and a release are routed along. */
		FWeakWidgetPath LastWidgetPath;
	};

	/** Every pointer this bridge is forwarding, keyed by DreamGUI pointer id. */
	TMap<int32, FForwardedPointer> ForwardedPointers;

	/** The entry for EventData's pointer, made when there is none. */
	FForwardedPointer& TrackPointer(UDreamPointerEventData* EventData);

	/** The entry the key, wheel and focus calls act for: CurrentPointerEventData's, or null. */
	FForwardedPointer* FindPrimaryPointer();

	/** What an exit of EventData does: stop following it, and hand the shared cursor back once nothing is left. */
	void EndHover(UDreamPointerEventData* EventData);

	/**
	 * Hand one pointer event to Slate on this component's virtual user: a press (a touch start first, for a touch),
	 * a release, a move. The only three roads into Slate's pointer routing, and virtual so a test can read what the
	 * bridge sends without a drawn UMG widget for it to land on.
	 */
	virtual void SendPointerDown(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent);
	virtual void SendPointerUp(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent);
	virtual void SendPointerMove(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent);

	/** Whether this component holds the cursor its virtual user shares with every component of that index. */
	bool HoldsVirtualCursor();

	/** Tick while there is something to forward: the shared cursor is this component's, or a finger is down here. */
	void UpdateTicking();

	/** Trace InPointer, and send the move it made: a mouse move every time, a touch move only when the finger moved. */
	void ForwardPointerMove(FForwardedPointer& InPointer);

	/**
	 * Press or release InKey for InPointer -- a touch's start or end for a touch key, a mouse button otherwise --
	 * traced where the pointer is at that moment. bInEndsPress also lets go of the press this surface was holding
	 * (after the trace, which follows a held press on the plane it was made on). Sends last: InPointer is not
	 * touched again once Slate has been handed the event.
	 */
	void ForwardPointerKey(FForwardedPointer& InPointer, const FKey& InKey, bool bInPressed, bool bInEndsPress = false);

	/** A wheel notch for InPointer, routed along the widgets under it now. */
	void ForwardPointerWheel(FForwardedPointer& InPointer, float InScrollDelta);

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
	 *
	 * For the first pointer still over the surface (CurrentPointerEventData); with none, nothing is
	 * pressed. A touch key presses as that pointer's touch.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual void PressPointerKey(FKey Key);

	/**
	 * Releases a key as if the mouse/pointer were the source of it.  Normally you would just use
	 * Left/Right mouse button for the Key.  However - advanced uses could also be imagined where you
	 * send other keys to signal widgets to take special actions if they're under the cursor.
	 *
	 * For the pointer PressPointerKey acts for.
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
	// Gets the key and char codes for sending keys for the platform.
	void GetKeyAndCharCodes(const FKey& Key, bool& bHasKeyCode, uint32& KeyCode, bool& bHasCharCode, uint32& CharCode);

	/** Is it safe for this interaction component to run?  Might not be in a server situation with no slate application. */
	bool CanSendInput();

	/**
	 * This component's entry in the manager, or null when it has none.
	 *
	 * Null is an ordinary answer, not an error: it is what every component gets wherever there is no
	 * Slate application to hand out a virtual user, and it is what every component gets before Awake
	 * has run. Callers branch on it. The lookup it replaces was TMap::operator[], which checks and
	 * takes the process down on a key that was never added rather than reporting the absence.
	 */
	UDreamUMGWidgetInteractionManager::FInteractionContainer* FindEnrolledInteractions();

	/** Forward every pointer's move this frame: the mouse while this component holds the shared cursor, each finger while it is down. */
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

	/** The widget path under the last hit result, whichever pointer it was. Each pointer keeps its own as well. */
	FWeakWidgetPath LastWidgetPath;

	/** The modifier keys to simulate during key presses. */
	FModifierKeysState ModifierKeys;

	/** The 2D location on the widget component that was hit, by the pointer traced last. */
	UPROPERTY(Transient)
	FVector2D LocalHitLocation;

	/** That pointer's location the time before. */
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

	/** Returns the path to the widget that is currently beneath InPointer, and records where on the widget it is. */
	FWidgetPath DetermineWidgetUnderPointer(FForwardedPointer& InPointer);
};
