// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamBaseEventData.h"
#include "Engine/HitResult.h"
#include "DreamPointerEventData.generated.h"

class UDreamWidget;
class UDreamBaseRaycaster;
class UDreamDragDropOperation;

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUINavigationDirection :uint8
{
	None,
	Left,
	Right,
	Up,
	Down,
	Next,
	Prev,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIPointerInputType :uint8
{
	Pointer,
	Navigation,
};

UCLASS(BlueprintType, classGroup = DreamGUI)
class DREAMGUI_API UDreamPointerEventData: public UDreamBaseEventData
{
	GENERATED_BODY()
public:
	/**
	 * pointer or navigation input?
	 * note some data is not valid when in navigation input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		EDreamUIPointerInputType InputType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		int UserIndex = 0;
	/** id of the pointer (touch id) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		int PointerID = 0;
	/** current pointer position (mouse position or touchpoint position in screen space. X&Y for mouse position) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector PointerPosition = FVector::ZeroVector;
	/** pointer position when press (mouse position or touchpoint position in screen space. X&Y for mouse position) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector PressPointerPosition = FVector::ZeroVector;

	/** entered component */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> EnterWidget = nullptr;
	/** a stack list for store entered component. the latest enter one stay at num-1, first stay at 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TArray<TObjectPtr<UDreamWidget>> EnterWidgetStack;
	/** a collection that current pointer hovering objects. the top most one stay at index 0 in array. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TArray<TObjectPtr<UDreamWidget>> HoverComponentArray;
	/** current world space hit point */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector WorldPoint = FVector(0, 0, 0);
	/** current world space hit normal */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector WorldNormal = FVector(0, 0, 1);
	/**
	 * current hit object's triangle face index.
	 * For UI element, only valid when target's RaycastType is Geometry.
	 * For world space static mesh, only valid when DreamWorldSpaceRaycaster->bRequireFaceIndex is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		int32 FaceIndex = -1;

	/** scroll event. X for horizontal, Y for vertical. if use mouse input, X equals Y */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector2D ScrollAxisValue = FVector2D::ZeroVector;
	/** current raycaster */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamBaseRaycaster> Raycaster;
	/** mouse input type */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		EDreamUIMouseButtonType MouseButtonType = EDreamUIMouseButtonType::Left;

	/** hit component when press */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> PressWidget = nullptr;
	/** world space hit point when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector PressWorldPoint = FVector(0, 0, 0);
	/** world space normal direction when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector PressWorldNormal = FVector(0, 0, 1);
	/** ray distance when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		float PressDistance = 0;
	/** ray origin when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector PressRayOrigin;
	/** ray direction when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector PressRayDirection;
	/** world to press component's local transform when trigger press, useful to calculate local space point/normal/delta */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FTransform PressWorldToLocalTransform;
	/** raycaster when press */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamBaseRaycaster> PressRaycaster;
	/**
	 * The three timestamps below all start at zero, meaning "the thing they date has not happened
	 * yet". They were the only fields on this class without an initialiser, which was survivable
	 * only for as long as nobody read one before writing it -- and PressTime IS read that way:
	 * ShouldStartDrag subtracts it from the current time and compares the difference against a hold
	 * duration. An event data that has never seen a button go down is completely ordinary (a hover,
	 * a scroll, the object a raycaster hands around before the first press), and with indeterminate
	 * bytes in PressTime hold-to-drag would begin a drag, or refuse to, at random on such a pointer.
	 */

	/** the last time when trigger click(time is get from GetWorld()->TimeSeconds), can be used to tell double click */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		double ClickTime = 0;
	/**
	 * How many clicks this pointer has landed on the same widget in an unbroken run: 1 for a single
	 * click, 2 for the second of a double, and so on. Reset to 1 by a press on a different widget from
	 * the last click, with a different button, too late (see UDreamEventSystem::DoubleClickTime), or
	 * farther from where the last click was pressed than the press raycaster's drag threshold.
	 *
	 * Counted at the PRESS, and the click that press ends in carries the same number, so a down, a
	 * double click and a click all read one answer. Every even press is dispatched as a double click
	 * instead of a down, which is how a triple click reads as down, double, down and a quadruple as two
	 * doubles -- the same shape as the desktop.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		int32 ClickCount = 0;
	/** What the previous click landed on. A click elsewhere starts the count over rather than continuing it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> LastClickWidget = nullptr;
	/**
	 * Which button the previous click was made with. A run is one button's, as a desktop double click
	 * is: a left click and a quick right press are two single presses, not a double click -- which
	 * matters now that the second press of a double click is delivered as the double click and not as
	 * a down, since a right press read as a left double click would never reach anything as a press.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		EDreamUIMouseButtonType LastClickMouseButtonType = EDreamUIMouseButtonType::Left;
	/**
	 * Where the previous click was PRESSED, in the pointer's screen position (its PressPointerPosition).
	 * A second press only continues the run if it lands within the press raycaster's drag threshold of
	 * this -- see UDreamBaseRaycaster::IsWithinDoubleClickDistance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector LastClickPressPointerPosition = FVector::ZeroVector;
	/** The same press in the world (its PressWorldPoint), for a pointer whose screen position never moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		FVector LastClickPressWorldPoint = FVector::ZeroVector;
	/** the last time when trigger release(time is get from GetWorld()->TimeSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		double ReleaseTime = 0;
	/** the last time when trigger press(time is tell from GetWorld()->TimeSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		double PressTime = 0;

	/** is dragging? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		bool bIsDragging = false;
	/** current dragging component */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> DragWidget = nullptr;
	/**
	 * What this drag MEANS -- payload, tag, drag visual. Set by the drag's source when it begins
	 * (UDreamUIDragSource does), read by drop targets, cleared wherever DragWidget is: it lives
	 * exactly as long as the drag. Null for a drag nothing gave meaning to (a scroll, a slider).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		TObjectPtr<UDreamDragDropOperation> DragOperation = nullptr;

	/**
	 * Has the long press already gone out for the press that is currently held?
	 *
	 * Long press fires once when the hold time is reached, not once a frame afterwards and not on
	 * release. Cleared when the trigger goes down, which is what makes it per-press.
	 */
	bool bIsLongPressFiredForThisPress = false;

	bool bIsUpFiredAtCurrentFrame = false;//PointerUp event is called at current frame?
	bool bIsExitFiredAtCurrentFrame = false;//PointerExit event is called at current frame?
	bool bIsEndDragFiredAtCurrentFrame = false;//EndDrag event is called at current frame?

	bool bNowIsTriggerPressed = false;
	bool bPrevIsTriggerPressed = false;

	TWeakObjectPtr<UDreamWidget> HighlightWidgetForNavigation = nullptr;
	float NavigateTickTime = 0;
	EDreamUINavigationDirection NavigateDirection = EDreamUINavigationDirection::None;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetHighlightedWidgetForNavigation(UDreamWidget* InWidget);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidget* GetHighlightedComponentForNavigation()const { return HighlightWidgetForNavigation.Get(); }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		bool IsPointerOverUI();

	virtual FString ToString()const override;
	/** Use a line-plane intersection to get world point. The plane is pressComponent's x-axis plane. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") 
		FVector GetWorldPointInPlane()const;
	/** Use a line-plane intersection to get world point, and convert to pressComponent's local space. The plane is pressComponent's x-axis plane.  */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") 
		FVector GetLocalPointInPlane()const;
	/** Use (ray direction) * (press line distance) + (ray origin) to calculated world point, so the result is a sphere with (ray origin) as center point. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetWorldPointSpherical()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetDragRayOrigin()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetDragRayDirection()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetCumulativeMoveDelta()const;
};

USTRUCT(BlueprintType)
struct FDreamUIHitResult
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	int32 FaceIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	float Time = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	float Distance = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FVector ImpactPoint = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FVector Normal = FVector::ForwardVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FVector TraceStart = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FVector TraceEnd = FVector::ZeroVector;

	TWeakObjectPtr<UDreamWidget> Widget = nullptr;
};
struct FDreamUIHitResultContainer
{
	FDreamUIHitResult HitResult;

	FVector RayOrigin = FVector(0, 0, 0), RayDirection = FVector(1, 0, 0), RayEnd = FVector(1, 0, 0);

	UDreamBaseRaycaster* Raycaster = nullptr;

	TArray<UDreamWidget*> HoverArray;
};
