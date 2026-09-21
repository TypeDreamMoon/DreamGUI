// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "Core/DreamUISpriteInfo.h"
#include "Core/DreamUIImageBrush.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "DreamImage.generated.h"

/**
 * DreamImage is render entry for Sprite & Texture & Material
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable)
class DREAMGUI_API UDreamImage : public UDreamVisualBatchMesh, public IDreamUISpriteRenderInterface
{
	GENERATED_BODY()
public:
	UDreamImage(const FObjectInitializer& ObjectInitializer);

	static FName GetPropertyName_Brush()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamImage, Brush);
	}
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", Getter, Setter, meta = (AllowPrivateAccess = true))
	FDreamUIImageBrush Brush;

	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	bool bHasAddToSprite = false;
	void UnregisterFromSprite();
	/**
	 * The pixel size the sprite in this brush was authored at, or negative when the brush holds
	 * anything else (a texture, a material, nothing yet) and the brush's own ImageSize is the answer.
	 *
	 * Copied at the moments this component is holding the sprite data anyway, and never read out of
	 * the sprite during a measure pass: UDreamUISpriteData::GetSpriteInfo reads like a getter and is
	 * an initialiser -- on a first touch it waits for texture compilation, packs a runtime atlas, and
	 * on failure dirties the asset and raises an editor notification. See UDreamSpriteBase, which
	 * keeps the same cache for the same reason.
	 *
	 * Deliberately not a UPROPERTY: it duplicates something the sprite asset already owns, and
	 * serialising it would let a stale copy outlive the truth.
	 */
	FVector2f CachedSpriteSourceSize = FVector2f(-1.0f, -1.0f);
	/** Re-read the brush sprite's source size into the cache. Only from sites already touching it. */
	void CacheSpriteSourceSize();
public:
#pragma region IDreamUISpriteRenderInterface
	virtual UDreamUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override;
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion
	UFUNCTION(BlueprintCallable, Category = "Image")
	const FDreamUIImageBrush& GetBrush()const { return Brush; }

	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush(const FDreamUIImageBrush& Value);
	//If you keep using DreamUISpriteData brush in this DreamImage, then this function is better performance than SetBrush
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_DreamUISprite(UDreamUISpriteData_BaseObject* Value);
	//If you keep using SlateTextureAtlas brush in this DreamImage, then this function is better performance than SetBrush
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value);
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_Texture(UTexture* Value);
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_Material(UTexture* Value);
	
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrushTintColor(FColor Value);
	/**
	 * How much of a Filled brush is shown, 0..1. Separate from SetBrush because a progress bar writes
	 * this every frame and SetBrush copies the whole brush and re-runs the layout; this touches the
	 * vertices and nothing else.
	 */
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrushFillAmount(float Value);

	virtual float GetPreferredWidth() const override;
	virtual float GetPreferredHeight() const override;

	/**
	 * What the geometry builders need to know about whatever the brush holds: the authored size and
	 * the UV rectangle. A sprite answers for itself; a texture, a material or nothing at all is
	 * described by the brush's own ImageSize and UVRegion.
	 */
	FDreamUISpriteInfo GetBrushSpriteInfo()const;

	/**
	 * How many tiles fit along one axis, and how much of the last one is shown.
	 * Public and static so the convention (the count INCLUDES the partial tile) can be pinned by a
	 * test without standing up a canvas -- the geometry builders all need one.
	 */
	static void CalculateTileCount(float InRectSize, float InTileSize, int32& OutCount, float& OutRemainedSize);

	/**
	 * The RoundedBox outline: a triangle fan around the rect's centre whose ring runs
	 * counter-clockwise from the bottom-left corner, one arc per corner, CornerSegments + 1 points on
	 * each. UVs are the position's place in the rect mapped into the sprite's UV rect, so the image
	 * inside the shape is the one a plain Image would have drawn.
	 *
	 * Static, and canvas-free by design: everything it needs is a parameter (including whether the
	 * canvas wants normals), so the shape can be asserted without a canvas to pump.
	 */
	static void BuildRoundedBoxGeometry(FDreamUIGeometry* UIGeo
		, float Width, float Height, const FVector2f& Pivot
		, const FDreamUISpriteInfo& SpriteInfo, const FVector4f& InCornerRadius, int32 InCornerSegments
		, FColor Color, bool bRequireNormalAndTangent
		, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
};
