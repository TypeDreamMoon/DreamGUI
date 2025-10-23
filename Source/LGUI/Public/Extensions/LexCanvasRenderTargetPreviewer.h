// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUISpriteInfo.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "LexCanvasRenderTargetPreviewer.generated.h"

class ULexCanvas;
/**
 * This component will grab canvas RenderTarget and display here.
 * NOTE!!! This only valid when target Canvas RenderMode is set to RenderTarget.
 */
UCLASS(ClassGroup = (LGUI), Blueprintable)
class LGUI_API ULexCanvasRenderTargetPreviewer : public ULexVisualBatchMesh
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

	UPROPERTY(EditAnywhere, Category = "LGUI")
	TWeakObjectPtr<ULexCanvas> Canvas;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	TObjectPtr<UMaterialInterface> Material;
	FLexUISpriteInfo SpriteInfo;

	bool bHasRegisterRenderTargetChangedEvent = false;
	void RegisterRenderTargetChangedEvent();
	void UnregisterRenderTargetChangedEvent();
	void UpdateSpriteData();

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void OnTransformChanged() override;
	
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;
	virtual void OnBeforeCreateOrUpdateGeometry() override;
	virtual void OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged) override;
};
