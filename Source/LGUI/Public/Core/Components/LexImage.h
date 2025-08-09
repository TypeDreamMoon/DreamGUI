// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexVisualBatchMesh.h"
#include "Core/ILexUISpriteRenderInterface.h"
#include "Core/LexUIImageBrush.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "LexImage.generated.h"

UCLASS(BlueprintType)
class LGUI_API ULexImage : public ULexVisualBatchMesh, public ILexUISpriteRenderInterface
{
	GENERATED_BODY()
public:
	ULexImage(const FObjectInitializer& ObjectInitializer);

	static FName GetPropertyName_Brush()
	{
		return GET_MEMBER_NAME_CHECKED(ULexImage, Brush);
	}
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexUIImageBrush Brush;

	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;
	virtual void OnUpdateGeometry(FLexUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	bool bHasAddToSprite = false;
public:
#pragma region ILexUISpriteRenderInterface
	virtual ULexUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override;
	virtual void ApplyAtlasTextureScaleUp_Implementation()override;
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion
	UFUNCTION(BlueprintCallable, Category = "Image")
	const FLexUIImageBrush& GetBrush()const { return Brush; }

	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush(const FLexUIImageBrush& Value);
	//If you keep using LexUISpriteData brush in this LexImage, then this function is better performance than SetBrush
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_LexUISprite(ULexUISpriteData_BaseObject* Value);
	//If you keep using SlateTextureAtlas brush in this LexImage, then this function is better performance than SetBrush
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value);
	
	UFUNCTION(BlueprintCallable, Category = "Image")
	void SetBrushTintColor(FColor Value);

	virtual float GetPreferredWidth() const override;
	virtual float GetPreferredHeight() const override;
};
