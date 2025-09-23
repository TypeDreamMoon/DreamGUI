// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LGUIComponentReference.h"
#include "PrefabSystem/ILGUIPrefabInterface.h"
#include "Core/Components/LexCustomMesh.h"
#include "Core/Actor/LexWidgetActor.h"
#include "LGUIRenderTargetInteraction.h"
#include "UIRenderTarget.generated.h"

class ULexCanvas;
class ULexCustomMeshSource;

/**
 * LGUI Render Target provide a solution to display a LGUICanvas with RenderMode of RenderTarget, just like "Retainer Box", and interact it with UIRenderTargetInteraction component.
 */
UCLASS(ClassGroup = LGUI, Blueprintable, meta = (BlueprintSpawnableComponent), hidecategories = (Object, Activation, "Components|Activation"))
class LGUI_API UUIRenderTarget : public ULexCustomMesh, public ILGUIRenderTargetInteractionSourceInterface
{
	GENERATED_BODY()
	
public:	
	UUIRenderTarget(const FObjectInitializer& ObjectInitializer);
protected:
	virtual bool SupportDrawCallBatching()const override;
	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual void OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;

	virtual void BeginPlay()override;

	UPROPERTY(EditAnywhere, Category = LGUI)
		FLGUIComponentReference TargetCanvas;

	mutable TWeakObjectPtr<class ULexCanvas> TargetCanvasObject = nullptr;
public:

	// Begin IUIRenderTargetInteractionSourceInterface
	virtual ULexCanvas* GetTargetCanvas_Implementation()const override;
	virtual bool PerformLineTrace_Implementation(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)override;
	// End IUIRenderTargetInteractionSourceInterface
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	UFUNCTION(BlueprintCallable, Category = LGUI)
	ULexCanvas* GetCanvas()const;

	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetCanvas(ULexCanvas* Value);
};

