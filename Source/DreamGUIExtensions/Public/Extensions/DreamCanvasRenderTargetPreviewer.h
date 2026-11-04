// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUISpriteInfo.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "DreamCanvasRenderTargetPreviewer.generated.h"

class UDreamCanvas;
/**
 * This component will grab canvas RenderTarget and display here.
 * NOTE!!! This only valid when target Canvas RenderMode is set to RenderTarget.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable)
class DREAMGUIEXTENSIONS_API UDreamCanvasRenderTargetPreviewer : public UDreamVisualBatchMesh
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamCanvas* GetPreviewCanvas()const { return Canvas.Get(); }
	/** Which canvas's render target to display. Re-registers the change event and re-reads the texture. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetPreviewCanvas(UDreamCanvas* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UMaterialInterface* GetPreviewMaterial()const { return Material; }
	/**
	 * Material the render target is drawn through, or null for the default. This is the hook
	 * UDreamRetainerBox uses for its effect material.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetPreviewMaterial(UMaterialInterface* Value);
	/**
	 * Draw the target as a group drawn already (bInComposite), put over this element's rect through the composite every
	 * group goes through (FDreamUICompositeDesc::bExternal): by InBlendMode, at InOpacity times this element's own alpha,
	 * through the clip this widget is drawn with, its own mask included. Where a canvas does not draw groups apart, and
	 * while a preview material is set, the target is drawn as a textured quad, as it was before. UDreamRetainerBox sets it.
	 */
	void SetComposite(bool bInComposite, EDreamUIBlendMode InBlendMode, float InOpacity);
	virtual bool GetCompositeSource(FDreamUICompositeDesc& OutDesc)const override;
protected:
	virtual void BeginPlay()override;
	virtual void EndPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
#endif

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TWeakObjectPtr<UDreamCanvas> Canvas;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UMaterialInterface> Material;
	FDreamUISpriteInfo SpriteInfo;
	/** See SetComposite. */
	bool bComposite = false;
	EDreamUIBlendMode CompositeBlendMode = EDreamUIBlendMode::Alpha;
	float CompositeOpacity = 1.0f;
	/** Whether the target is drawn through the composite now: asked for, and no preview material in the way. */
	void UpdateDrawsThroughComposite();

	bool bHasRegisterRenderTargetChangedEvent = false;
	void RegisterRenderTargetChangedEvent();
	void UnregisterRenderTargetChangedEvent();
	void UpdateSpriteData();

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void OnTransformChanged(bool InPositionChanged, bool InScaleChanged) override;
	
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;
	virtual void OnBeforeCreateOrUpdateGeometry() override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged) override;
};
