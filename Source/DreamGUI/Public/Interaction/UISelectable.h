// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIImageBrush.h"
#include "UISelectable.generated.h"

class UUINavigationInputSelectionHandler;
class UUISelectable;
class UDreamVisual;
class UDreamTweener;
class UDreamSelectableStyle;

UENUM(BlueprintType, Category = DreamGUI)
enum class EUISelectableTransitionType:uint8
{
	None,
	Color,
	/** This mode need a DreamImage as TransitionTarget */
	ImageBrush,
	/** You can implement custom UISelectableTransition to do the transition */
	Custom,
};
/**
 * WHEN a mouse click counts -- UMG's EButtonClickMethod, spelled in our own enum because this header
 * must not include UMG and because .dui, the designer and Blueprint all see a UENUM the same way.
 *
 * The distinction is not pedantry: a press-and-drag-away is how a player CANCELS a click on every
 * desktop, and DownAndUp is the only method that honours it. The other three exist for the cases
 * that genuinely want something else -- a key on a virtual keyboard repeating on press, a button
 * under a scrolling list that must not fire when the finger was really scrolling.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIClickMethod : uint8
{
	/** Press on it and release on it. The desktop's rule, and the default. */
	DownAndUp,
	/** The press alone fires it; the release is nothing. */
	MouseDown,
	/** The release alone fires it, wherever the press happened to land. */
	MouseUp,
	/** DownAndUp, and additionally refused once the pointer has begun dragging. */
	PreciseClick,
};

/** WHEN a touch counts -- UMG's EButtonTouchMethod. Consulted while the active device is Touch. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITouchMethod : uint8
{
	/** Touch it and lift off it. */
	DownAndUp,
	/** The touch alone fires it -- the responsive answer, and the wrong one inside a scroll view. */
	Down,
	/** DownAndUp, and additionally refused once the finger has begun dragging. */
	PreciseTap,
};

/** WHEN a gamepad or keyboard press counts -- UMG's EButtonPressMethod. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIPressMethod : uint8
{
	/** Press and release, both on this control. */
	DownAndUp,
	/** The press alone. */
	ButtonPress,
	/** The release alone. */
	ButtonRelease,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EUISelectableSelectionState :uint8
{
	/** Not hovered by pointer, just a normal state. */
	Normal,
	/** Hovered by pointer. */
	Hovered,
	/** Pressed by pointer. */
	Pressed,
	/** Disabled, not interactable. */
	Disabled,
	/**
	 * Holds keyboard/gamepad focus. Distinct from Hovered: a pointer can rest on one control while
	 * focus sits on another, and the two want to look different when both are on screen at once.
	 * Appended rather than slotted in beside Hovered so no saved asset changes meaning.
	 */
	Focused,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EUISelectableNavigationMode:uint8
{
	/** No navigation, cannot navigate out from this. */
	None,
	/** Navigation is controlled by DreamGUI. */
	Auto,
	/** Control your navigation behaviour on your own. */
	Explicit,
};


UCLASS(ClassGroup = (DreamUI), Abstract, Blueprintable, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UUITransition :public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UUITransition();
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "DreamGUI-Transition")
		TArray<TObjectPtr<UDreamTweener>> TweenerCollection;
public:
	/**
	 * Stop any transition inside TweenerCollection if playing, so remember to collect your tweener object by calling function CollectTweener.
	 * Call this before start any transition, in case of other transition is in progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Transition")
	virtual void StopTransition();
	/** Add tweener to TweenerCollection, so the function StopTransition will take effect. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Transition")
	virtual void CollectTweener(UDreamTweener* InItem);
	/** Add tweener set to TweenerCollection, so the function StopTransition will take effect. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Transition")
	virtual void CollectTweeners(const TSet<UDreamTweener*>& InItems);
};

UCLASS(ClassGroup = (DreamUI), Abstract, Blueprintable)
class DREAMGUI_API UUISelectableTransition :public UUITransition
{
	GENERATED_BODY()
public:

	UFUNCTION()
	UUISelectable* GetSelectableComponent()const;
protected:
	UPROPERTY(Transient, BlueprintReadOnly, Getter=GetSelectableComponent, Category = "DreamGUI-Transition", DisplayName=UISelectable)
	mutable TObjectPtr<UUISelectable> UISelectableComp;

	/** 
	 * Called when UISelectableComponent's transition state = normal.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnNormal"))
		void ReceiveOnNormal(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnHovered"))
		void ReceiveOnHovered(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = pressed.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnPressed"))
		void ReceiveOnPressed(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = disabled.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnDisabled"))
		void ReceiveOnDisabled(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = focused.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnFocused"))
		void ReceiveOnFocused(bool InImmediateSet);
public:
	/**
	 * Called when UISelectableComponent's transition state = normal.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnNormal();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnNormal(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnHighlighted();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnHovered(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = pressed.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnPressed();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnPressed(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = disabled.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnDisabled();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnDisabled(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = focused, and only when the selectable is
	 * set to give focus its own look; otherwise a focused control routes to OnHovered as before.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnFocused(bool InImmediateSet);
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUISelectable : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerSelectDeselectInterface
	, public IDreamNavigationInterface
{
	GENERATED_BODY()
public:
	UUISelectable();
protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void Awake() override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;

	friend class FUISelectableCustomization;
	
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		bool AllowEventBubbleUp = false;
	/**
	 * This control's own enabled flag. False draws it Disabled and refuses press, select, click and
	 * click feedback; hover enter/exit still arrive, so a tooltip can say why it is off.
	 *
	 * Distinct from UDreamWidget::SetInteractable, which turns the RAYCAST off for a whole subtree.
	 * This one leaves the widget hit-testable -- a disabled button still stops a click reaching the
	 * panel behind it, which is what a disabled button is for.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		bool bInteractable = true;

	virtual void OnInteractableChanged(bool IsEnabled) override;

#pragma region Transition
	/** Optional shared style. Inline values below remain the backwards-compatible fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Selectable", meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamSelectableStyle> Style = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
	TWeakObjectPtr<UDreamVisual> TransitionTarget;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
	EUISelectableTransitionType TransitionType = EUISelectableTransitionType::Color;

	UPROPERTY(EditAnywhere, Category="DreamGUI-Selectable", meta=(EditCondition="TransitionType==EUISelectableTransitionType::Custom"))
	TWeakObjectPtr<UUISelectableTransition> CustomTransition = nullptr;
	UPROPERTY(Transient)TObjectPtr<class UDreamTweener> TransitionTweener = nullptr;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor NormalColor = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor HoveredColor = FColor(200, 200, 200, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor PressedColor = FColor(150, 150, 150, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor DisabledColor = FColor(150, 150, 150, 128);
	/**
	 * Give keyboard and gamepad focus a look of its own. Off, a focused control wears the Hovered
	 * visuals -- what every control authored before focus was a separate state already expects.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		bool bUseFocusedVisuals = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (EditCondition = "bUseFocusedVisuals"))
		FColor FocusedColor = FColor(220, 220, 255, 255);

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush NormalImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush HoveredImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush PressedImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush DisabledImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false", EditCondition = "bUseFocusedVisuals"))
		FDreamUIImageBrush FocusedImageBrush;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (ClampMin = "0.0"))
	float AnimDuration = 0.2f;

	EUISelectableSelectionState CurrentSelectionState = EUISelectableSelectionState::Normal;
	void ApplyPointerSelectionState(bool ImmediateSet);
	/** What feedback last played for, so re-applying the same state stays silent. */
	EUISelectableSelectionState LastFeedbackState = EUISelectableSelectionState::Normal;
	/** Style-driven sound for entering CurrentSelectionState. Called by ApplyPointerSelectionState. */
	void PlaySelectionStateFeedback();
	/** Style-driven click sound and rumble. Button calls this from its click; other subclasses may too. */
	void PlayClickFeedback();
	bool bIsPointerInsideThis = false;
	bool bIsPointerDown = false;
	/**
	 * The pointer resting on this arrived from a navigation move rather than a real pointer. The two
	 * share the enter/exit path -- that is what makes the confirm button press whatever navigation
	 * landed on -- so the input type is the only thing that separates a hover from a focus here.
	 */
	bool bIsEnteredByNavigation = false;
	/** Selected by the event system, whether or not a pointer is on it. Survives the pointer moving away. */
	bool bIsSelected = false;
	bool CheckNavigationSelectionState();
	TWeakObjectPtr<UUINavigationInputSelectionHandler> NavigationSelection;
#pragma endregion
	/**
	 * WHEN this control's click fires, per input kind -- UMG's three, and resolved the way UMG's
	 * SButton resolves them: the active input device picks which of the three is asked.
	 *
	 * Held here rather than on UUIButton because a check box, a tab and a list row all want the same
	 * answer and none of them is a UUIButton; the CLICKERS (UUIButton, UUIToggle) are what consult
	 * them, through ShouldClickOn* below. A selectable that does not click (a slider, a scroll bar)
	 * simply never asks.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		EDreamUIClickMethod ClickMethod = EDreamUIClickMethod::DownAndUp;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		EDreamUITouchMethod TouchMethod = EDreamUITouchMethod::DownAndUp;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		EDreamUIPressMethod PressMethod = EDreamUIPressMethod::DownAndUp;

	/**
	 * Can we navigate from other selectable object to this one?
	 * If other selectable use EUISelectableNavigationMode.Explicit and use this selectable as specific one, then this selectable can still be navigate to.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		bool bCanNavigateHere = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationLeft = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationLeftSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationRight = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationRightSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationUp = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationUpSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationDown = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationDownSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationNext = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationNextSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationPrev = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationPrevSpecific;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		UDreamVisual* GetTransitionTarget()const { return TransitionTarget.Get(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	FColor GetNormalColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	FColor GetHoveredColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	FColor GetPressedColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	FColor GetDisabledColor()const;
	/** The focused colour, or the hovered one when focus has no look of its own. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	FColor GetFocusedColor()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	const FDreamUIImageBrush& GetNormalImageBrush()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	const FDreamUIImageBrush& GetHoveredImageBrush()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	const FDreamUIImageBrush& GetPressedImageBrush()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	const FDreamUIImageBrush& GetDisabledImageBrush()const;
	/** The focused brush, or the hovered one when focus has no look of its own. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	const FDreamUIImageBrush& GetFocusedImageBrush()const;
	/** Whether focus is drawn differently from hover on this control; reads the Style when one is set. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	bool GetUseFocusedVisuals()const;
	/** True while this control holds keyboard/gamepad focus, whatever a pointer happens to be doing. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	bool IsFocused()const{ return bIsSelected || (bIsPointerInsideThis && bIsEnteredByNavigation); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetStyle(UDreamSelectableStyle* Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Selectable")
	UDreamSelectableStyle* GetStyle()const { return Style; }
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
		EUISelectableSelectionState GetSelectionState()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetTransitionTarget(UDreamVisual* Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetNormalColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetHoveredColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetPressedColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetDisabledColor(FColor Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetNormalImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetHoveredImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetPressedImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetDisabledImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetFocusedColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetFocusedImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetUseFocusedVisuals(bool Value);
	/**
	 * How long a state change takes, in seconds. Zero snaps.
	 *
	 * A setter because a native control has to be able to push it: everything else about how a
	 * control looks is a style knob, and a transition speed that only a hand-placed behaviour could
	 * reach is the one part of the feel a project sheet could not state.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetAnimDuration(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Selectable")
	float GetAnimDuration() const { return AnimDuration; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetSelectionState(EUISelectableSelectionState NewState);

	/**
	 * True only when this control AND the tree above it are all enabled and visible. What the look,
	 * the navigation scan and the pointer handlers all ask.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		bool IsInteractable()const;
	/**
	 * This control's OWN flag, without the hierarchy. Read it to find out what was authored here;
	 * read IsInteractable to find out whether the player can use the thing.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Selectable")
		bool GetInteractable()const { return bInteractable; }
	/**
	 * Turn this control on or off at runtime, and repaint it.
	 *
	 * There was no setter at all: bInteractable was EditAnywhere and nothing else, so the only way to
	 * disable a control from game code was UDreamWidget::SetInteractable, which takes the whole
	 * subtree out of the raycast -- a different thing, and the wrong one for a button that must stay
	 * visible, keep its tooltip and keep blocking what is behind it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetInteractable(bool Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		EDreamUIClickMethod GetClickMethod()const { return ClickMethod; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetClickMethod(EDreamUIClickMethod Value) { ClickMethod = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		EDreamUITouchMethod GetTouchMethod()const { return TouchMethod; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetTouchMethod(EDreamUITouchMethod Value) { TouchMethod = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		EDreamUIPressMethod GetPressMethod()const { return PressMethod; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetPressMethod(EDreamUIPressMethod Value) { PressMethod = Value; }

	/**
	 * Whether InEventData's DOWN should fire this control's click, whether its UP should, and whether
	 * the event system's own click (down and up both on this widget) still should.
	 *
	 * One resolution, shared by every clicker, because "which of the three enums applies" is a
	 * question all of them have and none should answer twice. Which one applies is decided the way
	 * UMG's SButton decides it: a NAVIGATION event is the gamepad's, so PressMethod; a pointer event
	 * while the event system's current device is Touch is TouchMethod; everything else is the mouse's.
	 */
	bool ShouldClickOnDown(const UDreamPointerEventData* InEventData)const;
	bool ShouldClickOnUp(const UDreamPointerEventData* InEventData)const;
	bool ShouldClickOnClick(const UDreamPointerEventData* InEventData)const;

#pragma region Navigation
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		bool GetCanNavigateHere()const { return bCanNavigateHere; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationLeft()const { return NavigationLeft; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationRight()const { return NavigationRight; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationUp()const { return NavigationUp; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationDown()const { return NavigationDown; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationPrev()const { return NavigationPrev; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationNext()const { return NavigationNext; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationLeftExplicit()const { return NavigationLeftSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationRightExplicit()const { return NavigationRightSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationUpExplicit()const { return NavigationUpSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationDownExplicit()const { return NavigationDownSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationPrevExplicit()const { return NavigationPrevSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationNextExplicit()const { return NavigationNextSpecific.Get(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetCanNavigateHere(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationLeft(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationRight(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationUp(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationDown(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationPrev(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationNext(EUISelectableNavigationMode Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationLeftExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationRightExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationUpExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationDownExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationPrevExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationNextExplicit(UUISelectable* Value);

	/**
	 * Find UISelectable component on specific direction.
	 */
	virtual UUISelectable* FindSelectable(FVector InDirection);
	/**
	 * Find UISelectable component inside InParent on specific direction.
	 */
	virtual UUISelectable* FindSelectable(FVector InDirection, UDreamWidget* InParent);
protected:
	/** How many nested areas a single Escape move may climb out of before it gives up. */
	static constexpr int32 MaxNavigationEscapeDepth = 8;
public:
	/**
	 * Where InDirection leads, as the BEHAVIOUR that will receive the move.
	 *
	 * The real navigation entry point, and the reason the six FindSelectableOnX finders are now thin
	 * views onto it: a move can land on a widget whose only navigation is a UDreamWidgetNavigation,
	 * and a return type of UUISelectable* cannot say so. Rules are consulted in one fixed order --
	 * the widget's own navigation panel, then this component's per-direction mode -- so the answer
	 * never depends on which component the pipeline's walk happened to reach first.
	 *
	 * Returns this when there is nothing that way, and null when navigation is switched off for that
	 * direction; the two are different and the caller is expected to tell them apart.
	 */
	virtual UDreamUIBehaviour* FindNavigableOn(EDreamUINavigationDirection InDirection);
	/**
	 * The directional scan with the boundary rules applied, answering behaviours rather than only
	 * selectables. FindSelectable is this with the result cast.
	 * @param bResolveCanvasParent  ignore InParent and derive it from this widget's root canvas.
	 */
	UDreamUIBehaviour* FindNavigableIn(FVector InDirection, UDreamWidget* InParent, bool bResolveCanvasParent = false);
protected:
	/** Scan, and when it finds nothing let InRestrictNode's boundary rule decide what happens next. */
	UDreamUIBehaviour* FindNavigableWithin(const FVector& InDirection, UDreamWidget* InParent, const UDreamWidget* InRestrictNode, int32 InEscapeDepth);
	/**
	 * Where an EXPLICIT link in InDirection actually lands: InTarget when it can be navigated to, or
	 * the next hop of the author's own chain when it cannot, or null when the chain runs out.
	 *
	 * The Auto scan has always skipped anything not interactable, not visible or not navigable-to;
	 * the explicit path skipped every one of those tests and handed back whatever was wired. A
	 * gamepad could therefore land on a button disabled because the save is not unlocked, wear the
	 * Disabled look, and be unable to leave -- the next hop was computed from that same dead control.
	 * Following the chain rather than refusing outright keeps a hand-authored row usable when one of
	 * its entries is conditionally hidden, which is the ordinary reason for one to be.
	 */
	static UUISelectable* ResolveExplicitTarget(UUISelectable* InTarget, EDreamUINavigationDirection InDirection);
	/**
	 * The Prev target this control WOULD have under Auto, whatever its authored navigation modes say.
	 *
	 * Exists because the default-focus walk wants the positional answer and nothing else, and the way
	 * it used to get one was to write Auto into three of ANOTHER selectable's navigation UPROPERTYs,
	 * call the ordinary finder, and write the originals back. Any early return or exception in
	 * between left that authored data permanently rewritten, and in an editor world the write marked
	 * the asset dirty for a query that was supposed to change nothing.
	 */
	UUISelectable* FindAutoPrev();
public:
	/**
     * Default selectable is the most "Prev" one (left top most).
	 *
	 * When a navigation scope is active for InUserIndex it answers instead, because a scope knows
	 * where its screen wants focus and this scan only knows what registered first.
	 */
	static UUISelectable* FindDefaultSelectable(UObject* WorldContextObject, int32 InUserIndex = 0);
	/**
	 * The most "Prev" selectable inside InParent, never leaving it. A null InParent searches
	 * everything, which is what FindDefaultSelectable falls back to.
	 */
	static UUISelectable* FindDefaultSelectableIn(UObject* WorldContextObject, const UDreamWidget* InParent);
	virtual UUISelectable* FindSelectableOnLeft();
	virtual UUISelectable* FindSelectableOnRight();
	virtual UUISelectable* FindSelectableOnUp();
	virtual UUISelectable* FindSelectableOnDown();
	virtual UUISelectable* FindSelectableOnNext();
	virtual UUISelectable* FindSelectableOnPrev();
#pragma endregion
protected:
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerSelect_Implementation(UDreamBaseEventData* EventData)override;
	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)override;
	virtual bool CanNavigateHere_Implementation() const override;
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)override;
};
