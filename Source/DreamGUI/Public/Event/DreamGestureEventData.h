// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamPointerEventData.h"
#include "DreamGestureEventData.generated.h"

class UDreamWidget;

/** Which gesture this is. One type per recognizer; the fields below say which ones are meaningful. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIGestureType : uint8
{
	None,
	/** Two pointers moving apart (Scale > 1) or together (Scale < 1). */
	Pinch,
	/** One pointer travelling far enough, fast enough, in one direction, and then released. */
	Swipe,
};

/**
 * What a multi-pointer gesture was, once the recognizer has decided.
 *
 * Multi-touch has always produced one independent pointer per finger, which is the right foundation
 * and by itself never adds up to anything: nothing looked at two of them together, so pinch-to-zoom
 * and swipe-to-dismiss had to be hand-rolled by every project out of raw pointer positions.
 *
 * A gesture is deliberately NOT a pointer. It has no press widget, no drag, no hover -- it is a
 * conclusion drawn from pointers that already dispatched their own events. It is still a
 * UDreamBaseEventData so that it travels the same dispatch path as everything else: handlers on the
 * widget, bubbling up the hierarchy, and the event system's InputEvent for anyone watching globally.
 */
UCLASS(BlueprintType, classGroup = DreamGUI)
class DREAMGUI_API UDreamGestureEventData : public UDreamBaseEventData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		EDreamUIGestureType GestureType = EDreamUIGestureType::None;
	/** Whose gesture. Matches the user index of the pointers it was recognized from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		int UserIndex = 0;
	/** The pointers involved: two for a pinch, one for a swipe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TArray<int32> PointerIDs;
	/** Where the gesture is happening: the midpoint for a pinch, the current position for a swipe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector2D ScreenPosition = FVector2D::ZeroVector;
	/** The widget the gesture is dispatched to -- what the first of its pointers was pressed on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> Widget = nullptr;

	/** Pinch: current finger distance over the distance when the two fingers were first both down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		float PinchScale = 1.0f;
	/** Pinch: the change in finger distance since the previous report, in viewport pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		float PinchDistanceDelta = 0.0f;
	/** Pinch: the current distance between the two fingers, in viewport pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		float PinchDistance = 0.0f;

	/**
	 * Swipe: which way, snapped to the four directions.
	 *
	 * The same enum directional navigation uses, on purpose: a swipe and a stick flick mean the same
	 * thing to a screen, and a handler that already knows what Left means should not have to learn a
	 * second spelling of it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		EDreamUINavigationDirection SwipeDirection = EDreamUINavigationDirection::None;
	/** Swipe: from press to release, in viewport pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector2D SwipeDelta = FVector2D::ZeroVector;
	/** Swipe: how long the finger was down, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		float SwipeDuration = 0.0f;

	virtual FString ToString()const override;
};
