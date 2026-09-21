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

	// OnDimensionChanged, OnTransformChanged, GetTextureToCreateGeometry and OnUpdateGeometry are not
	// overridden: they did nothing but call Super, which reads as "something happens here" and hides
	// what this class actually specialises, which is the material below.
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;
	virtual void OnBeforeCreateOrUpdateGeometry() override;
};
