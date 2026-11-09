// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/DreamUIClipMask.h"
#include "DreamBackgroundBlur.generated.h"

/** What a background blur's strength varies by across its rect. */
UENUM(BlueprintType)
enum class EDreamBlurStrengthMapKind : uint8
{
	/** The same strength everywhere: the blur as it always was. */
	None,
	/** A texture's channel, stretched over the rect. */
	Texture,
	/** From one point to another. */
	Linear,
	/** Out from a centre, between two radii. */
	Radial,
	/** One of a clip mask's patterns (EDreamClipMaskPattern). */
	Procedural,
};

/**
 * How strong a background blur is across its rect, from 0 (none) to 1 (its Blur Strength), for a progressive blur: frosted at
 * the top and clear at the bottom, clear in the middle and frosted round it. Points are fractions of the rect from its top
 * left; a radius is a fraction of its height.
 */
USTRUCT(BlueprintType)
struct DREAMGUIEXTENSIONS_API FDreamBlurStrengthMap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map")
	EDreamBlurStrengthMapKind Kind = EDreamBlurStrengthMapKind::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Texture", EditConditionHides))
	TObjectPtr<UTexture> Texture = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Texture", EditConditionHides))
	EDreamClipMaskChannel Channel = EDreamClipMaskChannel::Alpha;
	/** Linear: where the start strength is; Radial: the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Linear || Kind == EDreamBlurStrengthMapKind::Radial", EditConditionHides))
	FVector2D Start = FVector2D(0.5, 0.0);
	/** Linear: where the end strength is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Linear", EditConditionHides))
	FVector2D End = FVector2D(0.5, 1.0);
	/** Radial: the radius the start strength holds out to, and the one the end strength is reached at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Radial", EditConditionHides, ClampMin = "0"))
	float InnerRadius = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Radial", EditConditionHides, ClampMin = "0"))
	float OuterRadius = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Procedural", EditConditionHides))
	EDreamClipMaskPattern Pattern = EDreamClipMaskPattern::Noise;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Procedural", EditConditionHides, ClampMin = "0.01"))
	float Frequency = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind == EDreamBlurStrengthMapKind::Procedural", EditConditionHides))
	int32 Seed = 0;
	/** The strengths at the start (inner radius, a pattern's 0) and at the end (outer radius, a pattern's 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind != EDreamBlurStrengthMapKind::None && Kind != EDreamBlurStrengthMapKind::Texture", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float StartStrength = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind != EDreamBlurStrengthMapKind::None && Kind != EDreamBlurStrengthMapKind::Texture", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float EndStrength = 0.0f;
	/** How many levels of the blur pyramid the strongest blur reaches: each one twice as wide as the one before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength Map", meta = (EditCondition = "Kind != EDreamBlurStrengthMapKind::None", EditConditionHides, ClampMin = "1", ClampMax = "5"))
	int32 MaxLevel = 5;
};

/** 
 * UI element that can add blur effect on background image, just like UMG's BackgroundBlur.
 * Use it in ScreenSpace or WorldSpace-DreamUIRenderer.
 * If android OpenGL ES3.1, need to enable "ProjectSettings/Platforms/Android/Build/Support Backbuffer Sampling on OpenGL".
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUIEXTENSIONS_API UDreamBackgroundBlur : public UDreamVisualPostProcess
{
	GENERATED_BODY()

public:	
	UDreamBackgroundBlur(const FObjectInitializer& ObjectInitializer);

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	/**
	 * Blur effect strength.
	 *
	 * The three below name their setters, so a write from Blueprint or from a .dui binding lands on
	 * the function rather than on the field: each of them has to mark the render proxy dirty, and a
	 * bare property write is a change the renderer never hears about.
	 */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetBlurStrength", Category = "DreamGUI", meta = (ClampMin = 0.0, ClampMax = 1.0f))
		float BlurStrength = 0.1f;
	/** Will alpha affect blur strength? If true, then 0 alpha means 0 blur strength, and 1 alpha means full blur strength. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetApplyAlphaToBlur", Category = "DreamGUI")
		bool ApplyAlphaToBlur = true;

	UPROPERTY(EditAnywhere, BlueprintSetter = "SetMaxDownSampleLevel", Category = "DreamGUI", AdvancedDisplay, meta = (ClampMin = 0, UIMin = 0, UIMax = 8))
		int MaxDownSampleLevel = 7;
	/**
	 * How the strength varies across the rect: a progressive blur. None is the uniform blur it always was. A map other than
	 * None is drawn from a blur pyramid over what is behind, each pixel at its own level, and is not drawn full size or into
	 * an output render target.
	 */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetStrengthMap", Category = "DreamGUI")
		FDreamBlurStrengthMap StrengthMap;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const FDreamBlurStrengthMap& GetStrengthMap() const { return StrengthMap; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetStrengthMap(const FDreamBlurStrengthMap& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetBlurStrength() const { return BlurStrength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	int GetMaxDownSampleLevel() const { return MaxDownSampleLevel; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetApplyAlphaToBlur()const { return ApplyAlphaToBlur; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBlurStrength(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMaxDownSampleLevel(int Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetApplyAlphaToBlur(bool Value);

	virtual FDreamVisualPostProcessRenderProxyPtr GetRenderProxy()override;
	virtual void MarkAllDirty()override;
private:
	FORCEINLINE float GetBlurStrengthInternal();
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendOthersDataToRenderProxy();
	void SendStrengthMapToRenderProxy();
};
