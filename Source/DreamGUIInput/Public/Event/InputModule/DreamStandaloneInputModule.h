// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "DreamStandaloneInputModule.generated.h"

/**
 * Common standalone platform input, or mouse input.
 *
 * Presses, releases and wheel turns are queued on the player and dispatched on the player's next frame, in the
 * order they arrived; moves and navigation are state the next frame reads.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUIINPUT_API UDreamStandaloneInputModule : public UDreamPointerInputModule
{
	GENERATED_BODY()

public:
	/** input for mouse press and release */
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (AdvancedDisplay = "inMouseButtonType"))
		void InputTrigger(const FVector& InMousePosition, bool InTriggerPress, EDreamUIMouseButtonType InMouseButtonType = EDreamUIMouseButtonType::Left);
	/**
	 * input for scroll
	 * @param	InAxisValue		Use a 2d vector for scroll value. For mouse scroll just fill X&Y with mouse scroll value; For touchpad input use X for horizontal and Y for vertical.
	 * @param	InPointerID		Which pointer scrolled. Defaults to the mouse; a touchpad gesture bound to
	 *							its own pointer can say so rather than scrolling whatever pointer 0 hovers.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void InputScroll(const FVector2D& InAxisValue, int InPointerID = 0);
	/**
	 * see "bOverrideMousePosition" property
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void InputMouseMove(const FVector& InMousePosition);
	/** input for touch press and release. InTouchID is the finger; its pointer is GetTouchPointerID(InTouchID). */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputTouchTrigger(bool InTouchPress, int InTouchID, const FVector& InTouchPointPosition);
	/** input for touch point moved */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputTouchMoved(int InTouchID, const FVector& InTouchPointPosition);
	/**
	 * The pointer id finger InTouchID is tracked under: 100 plus the finger, so that no finger is the mouse.
	 */
	UFUNCTION(BlueprintPure, Category = DreamGUI)
	static int32 GetTouchPointerID(int32 InTouchID);
	/**
	 * Where the mouse is on the game viewport, in its pixels -- as the viewport itself reckons it, so off the viewport
	 * whenever the cursor is not on it: (-1,-1) once it has left or with no viewport at all, and the position past the
	 * left or top edge while a drag the viewport captured is out there. A position off the viewport is over nothing.
	 * The substituted pointer while SetOverrideMousePosition is on.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void GetMousePosition(FVector2D& OutMousePos)const;

	/**
	 * While on, the pointer position comes from SetOverridePointerPosition rather than the OS mouse: GetMousePosition
	 * answers with the override, so every caller that routes through it -- the event system actor's clicks and moves
	 * included -- follows the substituted pointer for free. This is the virtual-cursor seam.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetOverrideMousePosition(bool bInOverride);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetOverrideMousePosition() const { return bOverrideMousePosition; }
	/** Move the substituted pointer. Pushes the position into the pointer pipeline when overriding. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetOverridePointerPosition(const FVector2D& InPosition);

	/** input for gamepad or keyboard navigation */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputNavigation(EDreamUINavigationDirection InDirection, bool InPressOrRelease, int InPointerID);
	/** input for gamepad or keyboard press and release */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputTriggerForNavigation(bool InTriggerPress, int InPointerID);
protected:
	void CommonInputTrigger(const FVector& InPointerPosition, bool InTriggerPress, int InPointerID, EDreamUIMouseButtonType InMouseButtonType = EDreamUIMouseButtonType::Left, bool bInIsTouch = false);

	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	bool bOverrideMousePosition = false;
	FVector2D OverridePointerPosition = FVector2D::ZeroVector;
};
