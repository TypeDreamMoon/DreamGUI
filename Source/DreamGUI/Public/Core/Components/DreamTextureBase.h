// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisualBatchMesh.h"
#include "Engine/Texture.h"
#include "DreamTextureBase.generated.h"

/** 
 * This is base class for create custom mesh based on UITexture. Just override OnCreateGeometry() and OnUpdateGeometry(...) to create or update your own geometry
 */
UCLASS(ClassGroup = (DreamGUI), Abstract, Blueprintable)
class DREAMGUI_API UDreamTextureBase : public UDreamVisualBatchMesh
{
	GENERATED_BODY()

public:	
	UDreamTextureBase(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void CheckTexture();
#endif
protected:
	virtual void BeginPlay()override;
	friend class FDreamTextureBaseCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DisplayThumbnail = "false"))
	TObjectPtr<UTexture> Texture = nullptr;
	/** Use a custom material to render this texture */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	UMaterialInterface* OverrideMaterial = nullptr;

	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;

	virtual bool ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UTexture* GetTexture()const { return Texture; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UMaterialInterface* GetOverrideMaterial()const{return OverrideMaterial;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		virtual void SetTexture(UTexture* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSizeFromTexture();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetOverrideMaterial(UMaterialInterface* Value);

	virtual float GetPreferredWidth()const override;
	virtual float GetPreferredHeight()const override;
};
