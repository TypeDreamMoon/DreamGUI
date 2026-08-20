// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamVisualBatchMesh.h"
#include "DreamVisualEmpty.generated.h"

/**
 * DreamVisualEmpty is just an empty visual, it will not render, but can handle raycast event
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable)
class DREAMGUI_API UDreamVisualEmpty : public UDreamVisualBatchMesh
{
	GENERATED_BODY()
public:
	UDreamVisualEmpty(const FObjectInitializer& ObjectInitializer);

protected:
	
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	
};
