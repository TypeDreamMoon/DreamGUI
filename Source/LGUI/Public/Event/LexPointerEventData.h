// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexBaseEventData.h"
#include "Engine/HitResult.h"
#include "LexPointerEventData.generated.h"

class ULexWidget;
class ULexBaseRaycaster;

UENUM(BlueprintType, Category = LGUI)
enum class ELexUINavigationDirection :uint8
{
	None,
	Left,
	Right,
	Up,
	Down,
	Next,
	Prev,
};
UENUM(BlueprintType, Category = LGUI)
enum class ELexUIPointerInputType :uint8
{
	Pointer,
	Navigation,
};

UCLASS(BlueprintType, classGroup = LGUI)
class LGUI_API ULexPointerEventData: public ULexBaseEventData
{
	GENERATED_BODY()
public:
	/**
	 * pointer or navigation input?
	 * note some data is not valid when in navigation input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		ELexUIPointerInputType InputType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		int UserIndex = 0;
	/** id of the pointer (touch id) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		int PointerID = 0;
	/** current pointer position (mouse position or touch point position in screen space. X&Y for mouse position) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector PointerPosition = FVector::ZeroVector;
	/** pointer position when press (mouse position or touch point position in screen space. X&Y for mouse position) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector PressPointerPosition = FVector::ZeroVector;

	/** entered component */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TObjectPtr<ULexWidget> EnterWidget = nullptr;
	/** a stack list for store entered component. the latest enter one stay at num-1, first stay at 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TArray<TObjectPtr<ULexWidget>> EnterWidgetStack;
	/** a collection that current pointer hovering objects. the top most one stay at index 0 in array. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TArray<TObjectPtr<ULexWidget>> HoverComponentArray;
	/** current world space hit point */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector WorldPoint = FVector(0, 0, 0);
	/** current world space hit normal */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector WorldNormal = FVector(0, 0, 1);
	/**
	 * current hit object's triangle face index.
	 * For UI element, only valid when target's RaycastType is Geometry.
	 * For world space static mesh, only valid when LexWorldSpaceRaycaster->bRequireFaceIndex is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		int32 FaceIndex = -1;

	/** scroll event. X for horizontal, Y for vertical. if use mouse input, X equals Y */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector2D ScrollAxisValue = FVector2D::ZeroVector;
	/** current raycaster */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TObjectPtr<ULexBaseRaycaster> Raycaster;
	/** mouse input type */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		ELexUIMouseButtonType MouseButtonType = ELexUIMouseButtonType::Left;

	/** hit component when press */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TObjectPtr<ULexWidget> PressWidget = nullptr;
	/** world space hit point when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector PressWorldPoint = FVector(0, 0, 0);
	/** world space normal direction when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector PressWorldNormal = FVector(0, 0, 1);
	/** ray distance when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		float PressDistance = 0;
	/** ray origin when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector PressRayOrigin;
	/** ray direction when press and hit something */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FVector PressRayDirection;
	/** world to press component's local transform when trigger press, useful to calculate local space point/normal/delta */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		FTransform PressWorldToLocalTransform;
	/** raycaster when press */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TObjectPtr<ULexBaseRaycaster> PressRaycaster;
	/** the last time when trigger click(time is get from GetWorld()->TimeSeconds), can be used to tell double click */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		double ClickTime;
	/** the last time when trigger release(time is get from GetWorld()->TimeSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		double ReleaseTime;
	/** the last time when trigger press(time is tell from GetWorld()->TimeSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		double PressTime;

	/** is dragging? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		bool bIsDragging = false;
	/** current dragging component */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI")
		TObjectPtr<ULexWidget> DragWidget = nullptr;

	ELexUIEventFireType EnterComponentEventFireType = ELexUIEventFireType::TargetActorAndAllItsComponents;
	ELexUIEventFireType PressComponentEventFireType = ELexUIEventFireType::TargetActorAndAllItsComponents;
	ELexUIEventFireType DragComponentEventFireType = ELexUIEventFireType::TargetActorAndAllItsComponents;

	bool bIsUpFiredAtCurrentFrame = false;//PointerUp event is called at current frame?
	bool bIsExitFiredAtCurrentFrame = false;//PointerExit event is called at current frame?
	bool bIsEndDragFiredAtCurrentFrame = false;//EndDrag event is called at current frame?

	bool bNowIsTriggerPressed = false;
	bool bPrevIsTriggerPressed = false;

	TWeakObjectPtr<ULexWidget> HighlightWidgetForNavigation = nullptr;
	float NavigateTickTime = 0;
	ELexUINavigationDirection NavigateDirection = ELexUINavigationDirection::None;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetHighlightedWidgetForNavigation(ULexWidget* InWidget);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexWidget* GetHighlightedComponentForNavigation()const { return HighlightWidgetForNavigation.Get(); }

	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool IsPointerOverUI();

	virtual FString ToString()const override;
	/** Use a line-plane intersection to get world point. The plane is pressComponent's x-axis plane. */
	UFUNCTION(BlueprintCallable, Category = "LGUI") 
		FVector GetWorldPointInPlane()const;
	/** Use a line-plane intersection to get world point, and convert to pressComponent's local space. The plane is pressComponent's x-axis plane.  */
	UFUNCTION(BlueprintCallable, Category = "LGUI") 
		FVector GetLocalPointInPlane()const;
	/** Use (ray direction) * (press line distance) + (ray origin) to calculated world point, so the result is a sphere with (ray origin) as center point. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FVector GetWorldPointSpherical()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FVector GetDragRayOrigin()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FVector GetDragRayDirection()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FVector GetCumulativeMoveDelta()const;
};

USTRUCT(BlueprintType)
struct FLexUIHitResult
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	int32 FaceIndex;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	float Time;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	float Distance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	FVector Location;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	FVector ImpactPoint;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	FVector Normal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	FVector TraceStart;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
	FVector TraceEnd;

	TWeakObjectPtr<ULexWidget> Component;

};
struct FLexUIHitResultContainer
{
	FLexUIHitResult HitResult;
	ELexUIEventFireType EventFireType = ELexUIEventFireType::TargetActorAndAllItsComponents;

	FVector RayOrigin = FVector(0, 0, 0), RayDirection = FVector(1, 0, 0), RayEnd = FVector(1, 0, 0);

	ULexBaseRaycaster* Raycaster = nullptr;

	TArray<ULexWidget*> HoverArray;
};
