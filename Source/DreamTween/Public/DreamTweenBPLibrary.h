// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamTweenManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamTweenDelegateHandleWrapper.h"
#include "DreamTweenBPLibrary.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenFloatSetterDynamic, float, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenDoubleSetterDynamic, double, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenIntSetterDynamic, int, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenVector2SetterDynamic, const FVector2D&, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenVector3SetterDynamic, const FVector&, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenVector4SetterDynamic, const FVector4&, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenColorSetterDynamic, const FColor&, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenLinearColorSetterDynamic, const FLinearColor&, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenQuaternionSetterDynamic, const FQuat&, value);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenRotatorSetterDynamic, const FRotator&, value);

UCLASS()
class DREAMTWEEN_API UDreamTweenBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* FloatTo(UObject* WorldContextObject, const FDreamTweenFloatSetterDynamic& setter, float startValue = 0.0f, float endValue = 1.0f, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* DoubleTo(UObject* WorldContextObject, const FDreamTweenDoubleSetterDynamic& setter, double startValue = 0.0f, double endValue = 1.0f, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* IntTo(UObject* WorldContextObject, const FDreamTweenIntSetterDynamic& setter, int startValue, int endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* Vector2To(UObject* WorldContextObject, const FDreamTweenVector2SetterDynamic& setter, FVector2D startValue, FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* Vector3To(UObject* WorldContextObject, const FDreamTweenVector3SetterDynamic& setter, FVector startValue, FVector endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* Vector4To(UObject* WorldContextObject, const FDreamTweenVector4SetterDynamic& setter, FVector4 startValue, FVector4 endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* ColorTo(UObject* WorldContextObject, const FDreamTweenColorSetterDynamic& setter, FColor startValue, FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* LinearColorTo(UObject* WorldContextObject, const FDreamTweenLinearColorSetterDynamic& setter, FLinearColor startValue, FLinearColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* QuaternionTo(UObject* WorldContextObject, const FDreamTweenQuaternionSetterDynamic& setter, FQuat startValue, FQuat endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = "DreamTween")
		static UDreamTweener* RotatorTo(UObject* WorldContextObject, const FDreamTweenRotatorSetterDynamic& setter, FRotator startValue, FRotator endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma region PositionXYZ
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position X To"), Category = "DreamTween")
		static UDreamTweener* LocalPositionXTo(USceneComponent* target, double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Y To"), Category = "DreamTween")
		static UDreamTweener* LocalPositionYTo(USceneComponent* target, double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Z To"), Category = "DreamTween")
		static UDreamTweener* LocalPositionZTo(USceneComponent* target, double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position X To (Sweep)"), Category = DreamTween)
		static UDreamTweener* LocalPositionXTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Y To (Sweep)"), Category = DreamTween)
		static UDreamTweener* LocalPositionYTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Z To (Sweep)"), Category = DreamTween)
		static UDreamTweener* LocalPositionZTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position X To"), Category = "DreamTween")
		static UDreamTweener* WorldPositionXTo(USceneComponent* target, double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Y To"), Category = "DreamTween")
		static UDreamTweener* WorldPositionYTo(USceneComponent* target, double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Z To"), Category = "DreamTween")
		static UDreamTweener* WorldPositionZTo(USceneComponent* target, double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position X To (Sweep)"), Category = DreamTween)
		static UDreamTweener* WorldPositionXTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Y To (Sweep)"), Category = DreamTween)
		static UDreamTweener* WorldPositionYTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Z To (Sweep)"), Category = DreamTween)
		static UDreamTweener* WorldPositionZTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion


#pragma region Position
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTween")
		static UDreamTweener* LocalPositionTo(USceneComponent* target, FVector endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position To (Sweep)"), Category = DreamTween)
		static UDreamTweener* LocalPositionTo_Sweep(USceneComponent* target, FVector endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTween")
		static UDreamTweener* WorldPositionTo(USceneComponent* target, FVector endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position To (Sweep)"), Category = DreamTween)
		static UDreamTweener* WorldPositionTo_Sweep(USceneComponent* target, FVector endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion Position

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = DreamTween)
		static UDreamTweener* LocalScaleTo(USceneComponent* target, FVector endValue = FVector(1.0f, 1.0f, 1.0f), float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

#pragma region Rotation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate eulerAngle relative to current rotation value"), Category = "DreamTween")
		static UDreamTweener* LocalRotateEulerAngleTo(USceneComponent* target, FVector eulerAngle, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute quaternion rotation value"), Category = "DreamTween")
		static UDreamTweener* LocalRotationQuaternionTo(USceneComponent* target, const FQuat& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute rotator value"), Category = "DreamTween")
		static UDreamTweener* LocalRotatorTo(USceneComponent* target, FRotator endValue, bool shortestPath, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Rotate Euler Angle To (Sweep)", ToolTip = "Rotate eulerAngle relative to current rotation value"), Category = "DreamTween")
		static UDreamTweener* LocalRotateEulerAngleTo_Sweep(USceneComponent* target, FVector eulerAngle, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Rotation Quaternion To (Sweep)", ToolTip = "Rotate absolute quaternion rotation value"), Category = "DreamTween")
		static UDreamTweener* LocalRotationQuaternionTo_Sweep(USceneComponent* target, const FQuat& endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Rotator To (Sweep)", ToolTip = "Rotate absolute rotator value"), Category = "DreamTween")
		static UDreamTweener* LocalRotatorTo_Sweep(USceneComponent* target, FRotator endValue, bool shortestPath, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate eulerAngle relative to current rotation value"), Category = "DreamTween")
		static UDreamTweener* WorldRotateEulerAngleTo(USceneComponent* target, FVector eulerAngle, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute quaternion rotation value"), Category = "DreamTween")
		static UDreamTweener* WorldRotationQuaternionTo(USceneComponent* target, const FQuat& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute rotator value"), Category = "DreamTween")
		static UDreamTweener* WorldRotatorTo(USceneComponent* target, FRotator endValue, bool shortestPath, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Rotate Euler Angle To (Sweep)", ToolTip = "Rotate eulerAngle relative to current rotation value"), Category = "DreamTween")
		static UDreamTweener* WorldRotateEulerAngleTo_Sweep(USceneComponent* target, FVector eulerAngle, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Rotation Quaternion To (Sweep)", ToolTip = "Rotate absolute quaternion rotation value"), Category = "DreamTween")
		static UDreamTweener* WorldRotationQuaternionTo_Sweep(USceneComponent* target, const FQuat& endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Rotator To (Sweep)", ToolTip = "Rotate absolute rotator value"), Category = "DreamTween")
		static UDreamTweener* WorldRotatorTo_Sweep(USceneComponent* target, FRotator endValue, bool shortestPath, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion Rotation

#pragma region Material
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* MaterialScalarParameterTo(UObject* WorldContextObject, class UMaterialInstanceDynamic* target, FName parameterName, float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* MaterialVectorParameterTo(UObject* WorldContextObject, class UMaterialInstanceDynamic* target, FName parameterName, FLinearColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = DreamTween)
		static UDreamTweener* MeshMaterialScalarParameterTo(class UPrimitiveComponent* target, int materialIndex, FName parameterName, float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = DreamTween)
		static UDreamTweener* MeshMaterialVectorParameterTo(class UPrimitiveComponent* target, int materialIndex, FName parameterName, FLinearColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion

#pragma region UMG
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_CanvasPanelSlot_PositionTo(UObject* WorldContextObject, class UCanvasPanelSlot* target, const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_CanvasPanelSlot_SizeTo(UObject* WorldContextObject, class UCanvasPanelSlot* target, const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_HorizontalBoxSlot_PaddingTo(UObject* WorldContextObject, class UHorizontalBoxSlot* target, const FMargin& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_VerticalBoxSlot_PaddingTo(UObject* WorldContextObject, class UVerticalBoxSlot* target, const FMargin& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_OverlaySlot_PaddingTo(UObject* WorldContextObject, class UOverlaySlot* target, const FMargin& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_ButtonSlot_PaddingTo(UObject* WorldContextObject, class UButtonSlot* target, const FMargin& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_BorderSlot_PaddingTo(UObject* WorldContextObject, class UBorderSlot* target, const FMargin& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_RenderTransform_TranslationTo(UObject* WorldContextObject, class UWidget* target, const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_RenderTransform_AngleTo(UObject* WorldContextObject, class UWidget* target, float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_RenderTransform_ScaleTo(UObject* WorldContextObject, class UWidget* target, const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_RenderTransform_ShearTo(UObject* WorldContextObject, class UWidget* target, const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_RenderOpacityTo(UObject* WorldContextObject, class UWidget* target, float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_UserWidget_ColorAndOpacityTo(UObject* WorldContextObject, class UUserWidget* target, const FLinearColor& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_Image_ColorAndOpacityTo(UObject* WorldContextObject, class UImage* target, const FLinearColor& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_Button_ColorAndOpacityTo(UObject* WorldContextObject, class UButton* target, const FLinearColor& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* UMG_Border_ContentColorAndOpacityTo(UObject* WorldContextObject, class UBorder* target, const FLinearColor& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion

	UFUNCTION(BlueprintCallable, meta = (ToolTip = "Assign start or update or omplete functions", WorldContext = "WorldContextObject", AutoCreateRefTerm="start,update,complete"), Category = DreamTween)
		static UDreamTweener* VirtualCall(UObject* WorldContextObject, float duration, float delay, const FDreamTweenSimpleDynamicDelegate& start, const FDreamTweenFloatDynamicDelegate& update, const FDreamTweenSimpleDynamicDelegate& complete)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->OnStart(start)->OnUpdate(update)->OnComplete(complete);
		}
		return Tweener;
	}
	static UDreamTweener* VirtualCall(UObject* WorldContextObject, float duration, float delay, FSimpleDelegate start, FDreamTweenUpdateDelegate update, FSimpleDelegate complete)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->OnStart(start)->OnUpdate(update)->OnComplete(complete);
		};
		return Tweener;
	}
	static UDreamTweener* VirtualCall(UObject* WorldContextObject, float duration, float delay, const TFunction<void()>& start, const TFunction<void(float)>& update, const TFunction<void()>& complete)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->OnStart(start)->OnUpdate(update)->OnComplete(complete);
		}
		return Tweener;
	}
	static UDreamTweener* VirtualCall(UObject* WorldContextObject, float duration)
	{
		return UDreamTweenManager::VirtualTo(WorldContextObject, duration);
	}

	UFUNCTION(BlueprintCallable, meta = (ToolTip = "MainThread delay call function, Assign delayComplete to call", WorldContext = "WorldContextObject", AdvancedDisplay="affectByGamePause,affectByTimeDilation"), Category = DreamTween)
		static UDreamTweener* DelayCall(UObject* WorldContextObject, float delayTime, const FDreamTweenSimpleDynamicDelegate& delayComplete, bool affectByGamePause = true, bool affectByTimeDilation = true)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, delayTime);
		if (Tweener)
		{
			Tweener->OnComplete(delayComplete)->SetAffectByGamePause(affectByGamePause)->SetAffectByTimeDilation(affectByTimeDilation);
		}
		return Tweener;
	}
	static UDreamTweener* DelayCall(UObject* WorldContextObject, float delayTime, FSimpleDelegate delayComplete, bool affectByGamePause = true, bool affectByTimeDilation = true)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, delayTime);
		if (Tweener)
		{
			Tweener->OnComplete(delayComplete)->SetAffectByGamePause(affectByGamePause)->SetAffectByTimeDilation(affectByTimeDilation);
		}
		return Tweener;
	}
	static UDreamTweener* DelayCall(UObject* WorldContextObject, float delayTime, const TFunction<void()>& delayComplete, bool affectByGamePause = true, bool affectByTimeDilation = true)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, delayTime);
		if (Tweener)
		{
			Tweener->OnComplete(delayComplete)->SetAffectByGamePause(affectByGamePause)->SetAffectByTimeDilation(affectByTimeDilation);
		}
		return Tweener;
	}
	UFUNCTION(BlueprintCallable, meta = (ToolTip = "MainThread delay frame call function, Assign delayComplete to call", WorldContext = "WorldContextObject", AdvancedDisplay = "affectByGamePause"), Category = DreamTween)
		static UDreamTweener* DelayFrameCall(UObject* WorldContextObject, int frameCount, const FDreamTweenSimpleDynamicDelegate& delayComplete, bool affectByGamePause = true)
	{
		auto Tweener = UDreamTweenManager::DelayFrameCall(WorldContextObject, frameCount);
		if (Tweener)
		{
			Tweener->OnComplete(delayComplete)->SetAffectByGamePause(affectByGamePause);
		}
		return Tweener;
	}
	static UDreamTweener* DelayFrameCall(UObject* WorldContextObject, int frameCount, FSimpleDelegate delayComplete, bool affectByGamePause = true)
	{
		auto Tweener = UDreamTweenManager::DelayFrameCall(WorldContextObject, frameCount);
		if (Tweener)
		{
			Tweener->OnComplete(delayComplete)->SetAffectByGamePause(affectByGamePause);
		}
		return Tweener;
	}
	static UDreamTweener* DelayFrameCall(UObject* WorldContextObject, int frameCount, const TFunction<void()>& delayComplete, bool affectByGamePause = true)
	{
		auto Tweener = UDreamTweenManager::DelayFrameCall(WorldContextObject, frameCount);
		if (Tweener)
		{
			Tweener->OnComplete(delayComplete)->SetAffectByGamePause(affectByGamePause);
		}
		return Tweener;
	}

	UFUNCTION(BlueprintCallable, meta = (ToolTip = "Assign start or update or omplete functions", WorldContext = "WorldContextObject", AutoCreateRefTerm = "start,update,complete"), Category = DreamTween)
		static UDreamTweener* UpdateCall(UObject* WorldContextObject, const FDreamTweenFloatDynamicDelegate& update)
	{
		auto Tweener = UDreamTweenManager::UpdateCall(WorldContextObject);
		if (Tweener)
		{
			Tweener->OnUpdate(update);
		}
		return Tweener;
	}
	static UDreamTweener* UpdateCall(UObject* WorldContextObject, FDreamTweenUpdateDelegate update)
	{
		auto Tweener = UDreamTweenManager::UpdateCall(WorldContextObject);
		if (Tweener)
		{
			Tweener->OnUpdate(update);
		};
		return Tweener;
	}
	static UDreamTweener* UpdateCall(UObject* WorldContextObject, const TFunction<void(float)>& update)
	{
		auto Tweener = UDreamTweenManager::UpdateCall(WorldContextObject);
		if (Tweener)
		{
			Tweener->OnUpdate(update);
		}
		return Tweener;
	}

	UFUNCTION(BlueprintPure, Category = DreamTween, meta = (WorldContext = "WorldContextObject"))
		static bool IsTweening(UObject* WorldContextObject, UDreamTweener* inTweener)
	{
		return UDreamTweenManager::IsTweening(WorldContextObject, inTweener);
	}
	/**
	 * Force stop this animation. if callComplete = true, OnComplete will call after stop.
	 * This method will check if the tweener is valid.
	 */
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "callComplete", WorldContext = "WorldContextObject"), Category = DreamTween)
		static void KillIfIsTweening(UObject* WorldContextObject, UDreamTweener* inTweener, bool callComplete = false)
	{
		UDreamTweenManager::KillIfIsTweening(WorldContextObject, inTweener, callComplete);
	}
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "callComplete", WorldContext = "WorldContextObject"), Category = DreamTween)
	static void KillAllTweensOnTarget(UObject* WorldContextObject, UObject* Target, bool callComplete = false)
	{
		UDreamTweenManager::KillAllTweensOnTarget(WorldContextObject, Target, callComplete);
	}
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Kill If Is Tweening (Array)", AdvancedDisplay = "callComplete", WorldContext = "WorldContextObject"), Category = DreamTween)
		static void ArrayKillIfIsTweening(UObject* WorldContextObject, const TArray<UDreamTweener*>& inTweenerArray, bool callComplete = false)
	{
		auto Instance = UDreamTweenManager::GetDreamTweenInstance(WorldContextObject);
		if (!IsValid(Instance))return;

		for (auto tweener : inTweenerArray)
		{
			Instance->KillIfIsTweening(WorldContextObject, tweener, callComplete);
		}
	}
	static void ArrayKillIfIsTweening(UObject* WorldContextObject, const TArray<TWeakObjectPtr<UDreamTweener>>& inTweenerArray, bool callComplete = false)
	{
		auto Instance = UDreamTweenManager::GetDreamTweenInstance(WorldContextObject);
		if (!IsValid(Instance))return;

		for (auto tweener : inTweenerArray)
		{
			Instance->KillIfIsTweening(tweener.Get(), callComplete);
		}
	}

	/**
	 * Repeatedly call function.
	 * @param delayTime delay time before the first call
	 * @param interval interval time between every call
	 * @param repeatCount repeat count, -1 means infinite
	 * @return tweener
	 */
	static UDreamTweener* RepeatCall(UObject* WorldContextObject, const TFunction<void()>& callFunction, float delayTime, float interval, int repeatCount = 1)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, interval);
		if (Tweener)
		{
			Tweener
				->SetDelay(delayTime)
				->SetLoop(repeatCount == 1 || repeatCount == 0 ? EDreamTweenLoop::Once : EDreamTweenLoop::Restart, repeatCount)
				->OnCycleStart(callFunction)
				;
		}
		return Tweener;
	}
	/**
	 * Repeatedly call function.
	 * @param delayTime delay time before the first call
	 * @param interval interval time between every call
	 * @param repeatCount repeat count, -1 means infinite
	 * \return tweener
	 */
	static UDreamTweener* RepeatCall(UObject* WorldContextObject, const FSimpleDelegate& callFunction, float delayTime, float interval, int repeatCount = 1)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, interval);
		if (Tweener)
		{
			Tweener
				->SetDelay(delayTime)
				->SetLoop(repeatCount == 1 || repeatCount == 0 ? EDreamTweenLoop::Once : EDreamTweenLoop::Restart, repeatCount)
				->OnCycleStart(callFunction)
				;
		}
		return Tweener;
	}
	/**
	 * Repeatedly call function.
	 * @param delayTime delay time before the first call
	 * @param interval interval time between every call
	 * @param repeatCount repeat count, -1 means infinite
	 * @return tweener
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		static UDreamTweener* RepeatCall(UObject* WorldContextObject, FDreamTweenSimpleDynamicDelegate callFunction, float delayTime, float interval = 1.0f, int repeatCount = 1)
	{
		auto Tweener = UDreamTweenManager::VirtualTo(WorldContextObject, interval);
		if (Tweener)
		{
			Tweener
				->SetDelay(delayTime)
				->SetLoop(repeatCount == 1 || repeatCount == 0 ? EDreamTweenLoop::Once : EDreamTweenLoop::Restart, repeatCount)
				->OnCycleStart(callFunction)
				;
		}
		return Tweener;
	}
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		static class UDreamTweenerSequence* CreateSequence(UObject* WorldContextObject)
	{
		return UDreamTweenManager::CreateSequence(WorldContextObject);
	}
};
