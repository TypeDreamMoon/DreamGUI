// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "DreamGlass.generated.h"

/** Where glass's shape comes from. */
UENUM(BlueprintType)
enum class EDreamGlassShape : uint8
{
	/** A rect of the widget's size, its corners rounded as a rect block's are. */
	RoundedRect,
};

/** How glass rises from its edge: the profile its refraction follows. */
UENUM(BlueprintType)
enum class EDreamGlassProfile : uint8
{
	/** Steep at the edge and flat soon after: a squircle's quarter. */
	Squircle,
	/** A quarter circle. */
	Circle,
	/** A straight ramp. */
	Linear,
};

/**
 * Refractive glass: what is behind it seen through a slab whose edge bends the light in, the red and blue channels bending a
 * little apart; optionally frosted, tinted, its saturation and brightness changed; an edge highlight from a light at an
 * angle and an inner shadow. Its shape is a rounded rect of its widget's size, its corners as a rect block's; a click
 * outside the rounded rect misses it.
 *
 * Drawn by DreamGUI's own renderer, as the other effects that read what is behind are. Its alpha (its colour's, its
 * widgets' opacity) fades it.
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUIEXTENSIONS_API UDreamGlass : public UDreamVisualPostProcess
{
	GENERATED_BODY()

public:
	UDreamGlass(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetCornerRadius(const FVector4f& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetCornerRadiusUnitMode(EDreamRectBlockUnitMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetEdgeWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRefraction(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetProfile(EDreamGlassProfile Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetDispersion(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFrost(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetGlassTint(FColor InColor, float InStrength);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetSaturation(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBrightness(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetHighlight(FColor InColor, float InStrength, float InWidth, float InLightAngle);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetInnerShadow(float InWidth, float InStrength);

	/** What the glass draws with, as the composite shader takes it: for a test, or a tool, to look at. */
	FDreamUIGlassParams MakeParams()const;
	/** Whether a point of the widget's rect (its local space, from its centre, +Y up) is on the glass. */
	bool IsPointOnGlass(const FVector2D& InFromCentre)const;

	virtual FDreamVisualPostProcessRenderProxyPtr GetRenderProxy()override;
	virtual void MarkAllDirty()override;
	virtual bool LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
	virtual void SendRegionVertexDataToRenderProxy()override;

	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass")
	EDreamGlassShape Shape = EDreamGlassShape::RoundedRect;
	/** As a rect block's: in units, or as a fraction of half the smaller side. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetCornerRadius", Category = "DreamGUI|Glass")
	FVector4f CornerRadius = FVector4f(24.0f, 24.0f, 24.0f, 24.0f);
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetCornerRadiusUnitMode", Category = "DreamGUI|Glass")
	EDreamRectBlockUnitMode CornerRadiusUnitMode = EDreamRectBlockUnitMode::Value;
	/** The band along the edge that bends the light, in units. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetEdgeWidth", Category = "DreamGUI|Glass|Refraction", meta = (ClampMin = "0.5", UIMax = "128"))
	float EdgeWidth = 24.0f;
	/** How far the edge bends the light, in units: what a pixel at the very edge shows is this far further in. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetRefraction", Category = "DreamGUI|Glass|Refraction", meta = (UIMin = "-64", UIMax = "64"))
	float Refraction = 16.0f;
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetProfile", Category = "DreamGUI|Glass|Refraction")
	EDreamGlassProfile Profile = EDreamGlassProfile::Squircle;
	/** How much less red and more blue bend than green: 0 none. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetDispersion", Category = "DreamGUI|Glass|Refraction", meta = (ClampMin = "0", UIMax = "1"))
	float Dispersion = 0.15f;
	/** 0 clear, 1 as frosted as a progressive blur's strongest level. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetFrost", Category = "DreamGUI|Glass|Frost", meta = (ClampMin = "0", ClampMax = "1"))
	float Frost = 0.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Tone")
	FColor GlassTint = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Tone", meta = (ClampMin = "0", ClampMax = "1"))
	float GlassTintStrength = 0.0f;
	/** What is seen through is this many times as saturated. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetSaturation", Category = "DreamGUI|Glass|Tone", meta = (ClampMin = "0", UIMax = "2"))
	float Saturation = 1.1f;
	/** Added to what is seen through. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetBrightness", Category = "DreamGUI|Glass|Tone", meta = (UIMin = "-0.5", UIMax = "0.5"))
	float Brightness = 0.02f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Highlight")
	FColor HighlightColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Highlight", meta = (ClampMin = "0", ClampMax = "1"))
	float HighlightStrength = 0.6f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Highlight", meta = (ClampMin = "0", UIMax = "32"))
	float HighlightWidth = 3.0f;
	/** Where the light comes from: degrees, 0 from above, clockwise. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Highlight", meta = (UIMin = "0", UIMax = "360"))
	float LightAngle = 315.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Highlight", meta = (ClampMin = "0", UIMax = "64"))
	float ShadowWidth = 10.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Glass|Highlight", meta = (ClampMin = "0", ClampMax = "1"))
	float ShadowStrength = 0.15f;

private:
	void SendGlassToRenderProxy();
	/** The corners' radii in units, as the unit mode resolves them at the widget's size. */
	FVector4f GetResolvedCornerRadius()const;
};
