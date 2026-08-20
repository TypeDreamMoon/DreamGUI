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
