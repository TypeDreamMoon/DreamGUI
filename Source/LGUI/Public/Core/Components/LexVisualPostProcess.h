// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexVisual.h"
#include "Core/LexUIRender/LexUIVertex.h"
#include "Core/LexUISpriteInfo.h"
#include "LexVisualPostProcess.generated.h"

class FLexVisualPostProcessRenderProxy;
struct FLexUIPostProcessVertex;


/** 
 * UI element that can do post-processing effect on screen space.
 * Only valid on LexUIRenderer (ScreenSpaceUI or WorldSpace-LexUIRenderer).
 */
UCLASS(Abstract, NotBlueprintable)
class LGUI_API ULexVisualPostProcess : public ULexVisual
{
	GENERATED_BODY()

public:	
	ULexVisualPostProcess(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	TSharedPtr<FLexUIGeometry> Geometry = nullptr;
	virtual void UpdateGeometry()override final;

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;

	virtual void MarkAllDirty()override;

protected:
	friend class FLexVisualPostProcessCustomization;
	/** Use maskTexture's red channel to mask out effect result. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (DisplayThumbnail = "false"))
		TObjectPtr<UTexture2D> MaskTexture;
	/** MaskTexture UV offset and scale info. Only get good result when MaskTextureType is Simple */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FVector4 MaskTextureUVRect = FVector4(0, 0, 1, 1);
	void SendMaskTextureToRenderProxy();
public:
	FLexUIGeometry* GetGeometry()const { return Geometry.Get(); }
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		UTexture2D* GetMaskTexture()const { return MaskTexture; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const FVector4& GetMaskTextureUVRect()const { return MaskTextureUVRect; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetMaskTexture(UTexture2D* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetMaskTextureUVRect(const FVector4& Value);
public:
	void MarkVertexPositionDirty();
	void MarkUVDirty();
public:
	virtual TSharedPtr<FLexVisualPostProcessRenderProxy> GetRenderProxy()PURE_VIRTUAL(UUIPostProcessRenderable::GetRenderProxy, return 0;);
	virtual bool IsRenderProxyValid()const;
	virtual bool HaveValidData()const;

	virtual bool LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)const override;
private:
	/** local vertex position changed */
	uint8 bLocalVertexPositionChanged : 1;
	/** vertex's uv change */
	uint8 bUVChanged : 1;
protected:
	TSharedPtr<FLexVisualPostProcessRenderProxy> RenderProxy = nullptr;
	/** update ui geometry */
	virtual void OnUpdateGeometry(bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged);
	/** update region vertex data */
	virtual void UpdateRegionVertex();
	TArray<FLexUIPostProcessCopyMeshRegionVertex> RenderScreenToMeshRegionVertexArray;
	TArray<FLexUIPostProcessVertex> RenderMeshRegionToScreenVertexArray;

	virtual void SendRegionVertexDataToRenderProxy();
};
