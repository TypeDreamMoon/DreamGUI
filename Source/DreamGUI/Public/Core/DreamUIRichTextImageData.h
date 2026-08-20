// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUITextData.h"
#include "DreamUIRichTextImageData_BaseObject.h"
#include "DreamUIRichTextImageData.generated.h"

USTRUCT(BlueprintType)
struct FDreamUIRichTextImageItemData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TArray<TObjectPtr<class UDreamUISpriteData_BaseObject>> Frames;
	/** use this value as animation-fps, -1 means not override */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float OverrideAnimationFps = -1;
};
/** use Sprite to render image for UIText */
UCLASS(NotBlueprintable, BlueprintType)
class DREAMGUI_API UDreamUIRichTextImageData :public UDreamUIRichTextImageData_BaseObject
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TMap<FName, FDreamUIRichTextImageItemData> ImageMap;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float AnimationFps = 4;
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetImageMap(const TMap<FName, FDreamUIRichTextImageItemData>& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetAnimationFps(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TMap<FName, FDreamUIRichTextImageItemData>& GetImageMap()const { return ImageMap; }
	/** Get this to directly modify the data. After modify is done, call BroadcastOnDataChange function to notify. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		TMap<FName, FDreamUIRichTextImageItemData>& GetMutableImageMap() { return ImageMap; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void BroadcastOnDataChange();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetAnimationFps()const { return AnimationFps; }

	virtual void CreateOrUpdateObject(class UDreamWidget* parent, const TArray<FDreamUIText_RichTextImageTag>& imageTagArray, TArray<TObjectPtr<class UDreamWidget>>& inOutCreatedImageObjectArray)override;
	virtual bool GetImageSize(const FName& imageTag, FIntVector2& outSize)override;
};