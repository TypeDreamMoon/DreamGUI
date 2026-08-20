// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "DreamCustomMesh.generated.h"

class UDreamUICustomMeshSource;

/**
 * Render UI element with DreamGUICustomMesh.
 */
UCLASS(ClassGroup = DreamGUI, NotBlueprintable)
class DREAMGUI_API UDreamCustomMesh : public UDreamVisualBatchMesh
{
	GENERATED_BODY()
	
public:	
	UDreamCustomMesh(const FObjectInitializer& ObjectInitializer);
protected:
	virtual bool SupportDrawCallBatching()const override;
	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;

	/** Use a mesh generator to create your own mesh instead of a simple rect */
	UPROPERTY(EditAnywhere, Instanced, Category = DreamGUI)
		TObjectPtr<UDreamUICustomMeshSource> CustomMesh = nullptr;

public:

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamUICustomMeshSource* GetCustomMesh()const { return CustomMesh; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetCustomMesh(UDreamUICustomMeshSource* Value);
};
