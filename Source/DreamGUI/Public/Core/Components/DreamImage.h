// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "Core/DreamUIImageBrush.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "DreamImage.generated.h"

/**
 * DreamImage is render entry for Sprite & Texture & Material
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable)
class DREAMGUI_API UDreamImage : public UDreamVisualBatchMesh, public IDreamUISpriteRenderInterface
{
	GENERATED_BODY()
public:
	UDreamImage(const FObjectInitializer& ObjectInitializer);

	static FName GetPropertyName_Brush()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamImage, Brush);
	}
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", Getter, Setter, meta = (AllowPrivateAccess = true))
	FDreamUIImageBrush Brush;

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
	bool bHasAddToSprite = false;
	void UnregisterFromSprite();
public:
#pragma region IDreamUISpriteRenderInterface
	virtual UDreamUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override;
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion
	UFUNCTION(BlueprintCallable, Category = "Image")
	const FDreamUIImageBrush& GetBrush()const { return Brush; }

	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush(const FDreamUIImageBrush& Value);
	//If you keep using DreamUISpriteData brush in this DreamImage, then this function is better performance than SetBrush
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_DreamUISprite(UDreamUISpriteData_BaseObject* Value);
	//If you keep using SlateTextureAtlas brush in this DreamImage, then this function is better performance than SetBrush
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value);
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_Texture(UTexture* Value);
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_Material(UTexture* Value);
	
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrushTintColor(FColor Value);

	virtual float GetPreferredWidth() const override;
	virtual float GetPreferredHeight() const override;
};
