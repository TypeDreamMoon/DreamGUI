// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUISpriteInfo.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "LexPostProcessRenderElement.generated.h"

class ULexVisualPostProcess;
/**
 * This component will grab post-process result image and display here. Not support rotate.
 * NOTE!!! This only valid when target PostProcess RenderType is set to RenderTarget and FullScreen is set to false.
 */
UCLASS(ClassGroup = (LGUI), Blueprintable)
class LGUI_API ULexPostProcessRenderElement : public ULexVisualBatchMesh
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay()override;
	virtual void EndPlay() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, Category = "LGUI")
	TWeakObjectPtr<ULexVisualPostProcess> PostProcess;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	TObjectPtr<UMaterialInterface> Material;
	FLexUISpriteInfo SpriteInfo;

	bool bHasRegisterPostProcessUpdateEvent = false;
	void RegisterPostProcessUpdateEvent();
	void UnregisterPostProcessUpdateEvent();
	void UpdateSpriteData();

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void OnTransformChanged() override;
	
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;
	virtual void OnBeforeCreateOrUpdateGeometry() override;
	virtual void OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged) override;
};
