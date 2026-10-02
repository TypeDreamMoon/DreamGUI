// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUITextData.h"
#include "DreamUIFontEmojiData.generated.h"

/**
 * What an emoji data entry is found by: the whole emoji cluster, its code points with U+FE0E and U+FE0F left out (they
 * pick a presentation, not a picture). A key loaded from before sequences has an empty Sequence and stands for the
 * one-code-point sequence {EmojiCode}, so old and new keys hash and compare alike and nothing has to be rehashed when
 * the load fills Sequence in (FDreamGUIObjectVersion::EmojiKeyBySequence).
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIFontEmojiKey
{
	GENERATED_BODY()
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=DreamGUI)
	FString EmojiChar;
#endif
	/** The cluster's base, its first code point: what a key was before sequences, and what FindByCodepoint matches. */
	UPROPERTY()
	uint32 EmojiCode = 0;
	UPROPERTY()
	uint16 VariantSelector = 0;
	/** Every code point of the cluster in order, variation selectors U+FE0E/U+FE0F left out. Empty: {EmojiCode}. */
	UPROPERTY()
	TArray<int32> Sequence;

	FDreamUIFontEmojiKey(){}
	FDreamUIFontEmojiKey(uint32 InEmojiCode)
	{
		this->EmojiCode = InEmojiCode;
	}
	/** Length of the sequence the key stands for, and its code point at Index. */
	int32 GetSequenceLength() const { return Sequence.Num() > 0 ? Sequence.Num() : 1; }
	int32 GetSequenceAt(int32 Index) const { return Sequence.Num() > 0 ? Sequence[Index] : (int32)EmojiCode; }
	bool operator==(const FDreamUIFontEmojiKey& other)const
	{
		const int32 Length = GetSequenceLength();
		if (Length != other.GetSequenceLength())
		{
			return false;
		}
		for (int32 Index = 0; Index < Length; Index++)
		{
			if (GetSequenceAt(Index) != other.GetSequenceAt(Index))
			{
				return false;
			}
		}
		return true;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIFontEmojiKey& other)
	{
		uint32 Hash = 0;
		for (int32 Index = 0, Length = other.GetSequenceLength(); Index < Length; Index++)
		{
			Hash = HashCombineFast(Hash, ::GetTypeHash(other.GetSequenceAt(Index)));
		}
		return Hash;
	}

#if WITH_EDITOR
	/** Fill EmojiCode, VariantSelector and Sequence from EmojiChar, segmented exactly as a text segments it. */
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
	/** Fills the Sequence of every key saved before FDreamGUIObjectVersion::EmojiKeyBySequence with its {EmojiCode}. */
	virtual void Serialize(FArchive& Ar) override;

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

	/**
	 * One image widget per emoji, each showing the entry FindBySequence finds for its FDreamUIText_Emoji::Sequence, else
	 * the one FindByCodepoint finds for its EmojiCode -- the same order the layout chose them in.
	 */
	void CreateOrUpdateObject(class UDreamWidget* parent, const TArray<FDreamUIText_Emoji>& emojiArray, TArray<TObjectPtr<class UDreamWidget>>& inOutCreatedImageObjectArray);
	/** The size of the entry keyed by this single code point (FindByCodepoint), from its first frame. */
	bool GetImageSize(const uint32& emojiCode, FIntVector2& outSize);

	/**
	 * The entry for exactly this emoji cluster, given as the text that spells it (an element's source span, or the one
	 * character an escaped element stands for); U+FE0E and U+FE0F are ignored. Null when there is none. Precedence step 1
	 * of a text's emoji: an author's own picture for the exact sequence beats a colour font.
	 */
	const FDreamUIFontEmojiDataItem* FindBySequence(const FString& InCluster) const;
	/**
	 * The entry keyed by this one code point -- every entry an asset made before sequences has -- or null. Precedence step
	 * 3: below a colour face that covers the whole cluster, above a monochrome one.
	 */
	const FDreamUIFontEmojiDataItem* FindByCodepoint(uint32 InCodepoint) const;
	/** The pixel size of an entry's first frame; false when it has no usable frame. */
	static bool GetItemImageSize(const FDreamUIFontEmojiDataItem& InItem, FIntVector2& OutSize);
	/** The key a cluster's text is looked up by: its code points with U+FE0E and U+FE0F left out, EmojiCode the first. */
	static FDreamUIFontEmojiKey MakeKey(const FString& InCluster);
};