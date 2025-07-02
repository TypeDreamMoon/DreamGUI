// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexUITextData.h"
#include "LexUIRichTextImageData_BaseObject.h"
#include "LexUIRichTextImageData.generated.h"

USTRUCT(BlueprintType)
struct FLexUIRichTextImageItemData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "LGUI")
		TArray<TObjectPtr<class ULexUISpriteData_BaseObject>> frames;
	/** use this value as animation-fps, -1 means not override */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float overrideAnimationFps = -1;
};
/** use sprite to render image for UIText */
UCLASS(NotBlueprintable, BlueprintType)
class LGUI_API ULexUIRichTextImageData :public ULexUIRichTextImageData_BaseObject
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "LGUI")
		TMap<FName, FLexUIRichTextImageItemData> imageMap;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float animationFps = 4;
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetImageMap(const TMap<FName, FLexUIRichTextImageItemData>& value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetAnimationFps(float value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TMap<FName, FLexUIRichTextImageItemData>& GetImageMap()const { return imageMap; }
	/** Get this to directly modify the data. After modify is done, call BroadcastOnDataChange function to notify. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		TMap<FName, FLexUIRichTextImageItemData>& GetMutableImageMap() { return imageMap; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void BroadcastOnDataChange();
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetAnimationFps()const { return animationFps; }

	virtual void CreateOrUpdateObject(class ULexWidget* parent, const TArray<FUIText_RichTextImageTag>& imageTagArray, TArray<TObjectPtr<class ULexWidget>>& inOutCreatedImageObjectArray, bool listImageObjectInEditorOutliner)override;
	virtual bool GetImageSize(const FName& imageTag, FIntVector2& outSize);
};