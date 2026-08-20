// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamText.h"
#include "DreamPostProcessRenderElement_Text.generated.h"

class UDreamVisualPostProcess;
/**
 * This component will grab post-process result image and display here.
 * NOTE!!! This only valid when target PostProcess RenderType is set to RenderTarget and bUseFullSize is set to false.
 * UV channel:
 *		UV0 ~ UV2: Check DreamText
 *		UV3: TextureCoordinate for sampling PostProcess RenderTarget
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable)
class DREAMGUI_API UDreamPostProcessRenderElement_Text : public UDreamText
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay()override;
	virtual void EndPlay() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TWeakObjectPtr<UDreamVisualPostProcess> PostProcess;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", Transient)
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstanceDynamic;

	bool bHasRegisterPostProcessChangedEvent = false;
	void RegisterPostProcessChangedEvent();
	void UnregisterPostProcessChangedEvent();
	void SetMaterialParameter();
	void CheckMaterialInstanceDynamic();

	static FName DreamUI_PostProcessTexture;

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void OnTransformChanged(bool InPositionChanged, bool InScaleChanged) override;
	
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;
	virtual void OnBeforeCreateOrUpdateGeometry() override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged) override;
};
