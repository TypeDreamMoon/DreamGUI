// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamPointerLight.generated.h"

class UDreamLayerStyle;
class UDreamRectBlock;
class UDreamWidget;

/**
 * A small light over the widget's subtree that follows the pointer, or stands where it is put (and can be animated there): a
 * colour, a strength, how far it reaches and how high it stands above the widgets. What it lights:
 *
 * - every bevel and emboss of a layer style under it (UDreamLayerStyle), lit from where the light is rather than by the style's
 *   own angle and altitude -- the highlight follows the pointer across a raised button;
 * - every rect block under it with a border: the border's gradient is centred on the light, in its colour, so the edge near the
 *   pointer lights up and the rest stays as it was -- a reveal highlight, at the cost of a rect block's row and nothing more.
 *
 * Following the pointer, it fades in while the pointer is over its widget's plane within its reach and out when it leaves;
 * everything it lit goes back to its own light then.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Pointer Light")
class DREAMGUIEXTENSIONS_API UDreamPointerLight : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UDreamPointerLight();

	/** Where the light stands, in the widget's own space from its rect's centre (+Y up), when it does not follow the pointer. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Pointer Light")
	bool IsEnabled() const { return bEnabled; }
	/** Off, it lights nothing; on, it lights again from where it would be now. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetEnabled(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetLightPosition(FVector2D Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Pointer Light")
	FVector2D GetLightPosition() const { return LightPosition; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetFollowPointer(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetLightColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetIntensity(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetRadius(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Pointer Light")
	void SetHeight(float Value);

	/** Where the light is now, in the widget's own space from its rect's centre (+Y up): the pointer's place, smoothed. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Pointer Light")
	FVector2D GetCurrentPosition() const { return CurrentPosition; }
	/** How strong it is now: its intensity, faded in and out as the pointer comes and goes. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Pointer Light")
	float GetCurrentIntensity() const { return CurrentIntensity; }

	/** The light as it is now handed to everything it lights, at once: for a test, or a tool that cannot wait a frame. */
	void Update(float DeltaTime);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Off, it lights nothing: to compare with and without it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (DreamGUI_LayerToggle))
	bool bEnabled = true;
	/** Follow the pointer (on), or stand at LightPosition (off). */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light")
	bool bFollowPointer = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (EditCondition = "!bFollowPointer"))
	FVector2D LightPosition = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light")
	FColor LightColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (ClampMin = "0", UIMax = "4"))
	float Intensity = 1.0f;
	/** How far it reaches, in units: nothing past it is lit. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (ClampMin = "1", UIMax = "1000"))
	float Radius = 180.0f;
	/** How high above the widgets it stands, in units: lower, the light grazes the bevels. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (ClampMin = "1", UIMax = "500"))
	float Height = 80.0f;
	/** How quickly it catches up with the pointer: higher is tighter, 0 is at once. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (ClampMin = "0", UIMax = "60"))
	float FollowSharpness = 18.0f;
	/** How quickly it fades in and out, in intensities per second. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", meta = (ClampMin = "0.1", UIMax = "30"))
	float FadeSpeed = 6.0f;
	/** Light the bevels of the layer styles under it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light")
	bool bLightBevels = true;
	/** Centre the border gradients of the rect blocks under it on the light. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light")
	bool bRevealBorders = true;
	/** Whose pointer it follows. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", AdvancedDisplay)
	int32 UserIndex = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Pointer Light", AdvancedDisplay)
	int32 PointerId = 0;

private:
	/** The light handed to everything under the widget that it lights, and taken back from what it no longer does. */
	void Apply();
	void ReleaseAll();

	FVector2D CurrentPosition = FVector2D::ZeroVector;
	float CurrentIntensity = 0.0f;
	bool bHasPosition = false;
	TArray<TWeakObjectPtr<UDreamLayerStyle>> LitStyles;
	TArray<TWeakObjectPtr<UDreamRectBlock>> RevealedBlocks;
	FDelegateHandle EditorTickHandle;
};
