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
class DREAMGUI_API UDreamCanvasRenderTargetPreviewer : public UDreamVisualBatchMesh
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
protected:
	virtual void BeginPlay()override;
	virtual void EndPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TWeakObjectPtr<UDreamCanvas> Canvas;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UMaterialInterface> Material;
	FDreamUISpriteInfo SpriteInfo;

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
