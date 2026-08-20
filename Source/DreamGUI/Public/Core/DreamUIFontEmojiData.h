// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUITextData.h"
#include "DreamUIFontEmojiData.generated.h"

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIFontEmojiKey
{
	GENERATED_BODY()
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=DreamGUI)
	FString EmojiChar;
#endif
	UPROPERTY()
	uint32 EmojiCode = 0;
	UPROPERTY()
	uint16 VariantSelector = 0;

	FDreamUIFontEmojiKey(){}
	FDreamUIFontEmojiKey(uint32 InEmojiCode)
	{
		this->EmojiCode = InEmojiCode;
	}
	bool operator==(const FDreamUIFontEmojiKey& other)const
	{
		return this->EmojiCode == other.EmojiCode;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIFontEmojiKey& other)
	{
		return GetTypeHash(other.EmojiCode);
	}

#if WITH_EDITOR
	void ApplyEmoji();
#endif
};

USTRUCT(BlueprintType)
struct FDreamUIFontEmojiDataItem
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TArray<TObjectPtr<class UDreamUISpriteData_BaseObject>> Frames;
	/** use this value as animation-fps, -1 means not override */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float OverrideAnimationFps = -1;
};

UCLASS(NotBlueprintable, BlueprintType)
class DREAMGUI_API UDreamUIFontEmojiData :public UObject
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem> DataMap;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float AnimationFps = 4;
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	DECLARE_EVENT(UDreamUIFontEmojiData, FDreamUIFontEmojiDataRefreshEvent);
	/** Called when any data change, and need UIText to refresh. */
	FDreamUIFontEmojiDataRefreshEvent OnDataChange;
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetDataMap(const TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetAnimationFps(float Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& GetDataMap()const { return DataMap; }
	/** Get this to directly modify the data. After modify is done, call BroadcastOnDataChange function to notify. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& GetMutableDataMap() { return DataMap; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void BroadcastOnDataChange();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetAnimationFps()const { return AnimationFps; }

	void CreateOrUpdateObject(class UDreamWidget* parent, const TArray<FDreamUIText_Emoji>& emojiArray, TArray<TObjectPtr<class UDreamWidget>>& inOutCreatedImageObjectArray);
	bool GetImageSize(const uint32& emojiCode, FIntVector2& outSize);
};