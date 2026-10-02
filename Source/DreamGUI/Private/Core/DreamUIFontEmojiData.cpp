// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUIWorldContext.h"

#include "Extensions/UISpriteSequencePlayer.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamSprite.h"
#include "Engine/World.h"
#include "UObject/DreamGUIObjectVersion.h"

#if WITH_EDITOR

void FDreamUIFontEmojiKey::ApplyEmoji()
{
	// One grapheme cluster, segmented by exactly the function the text pipeline segments with, so what
	// an author can paste in here is what a text will look up: a plain emoji, a BMP symbol wearing
	// U+FE0F, a ZWJ family, a skin tone, a flag. Registering used to require a bare surrogate pair and
	// truncate everything longer to its first pair, which registered a different character than the one
	// on screen. The key is the whole cluster (Sequence), the variation selectors left out because they
	// choose a presentation, not a picture; EmojiCode is its base, which a key was before sequences.
	const FString Source = EmojiChar;
	const int32 SourceLength = Source.Len();
	if (SourceLength > 0)
	{
		int CharIndex = 0;
		const auto Element = FDreamUIText_CodePoint::ReadCodePoint(Source, SourceLength, CharIndex);
		if (Element.Type == EDreamUIText_CodeType::Emoji)
		{
			EmojiChar = Source.Mid(Element.StringIndex, Element.Length);
			const FDreamUIFontEmojiKey Key = UDreamUIFontEmojiData::MakeKey(EmojiChar);
			EmojiCode = Element.Unicode;
			VariantSelector = Key.VariantSelector;
			Sequence = Key.Sequence;
			return;
		}
	}
	EmojiChar = "";
	EmojiCode = 0;
	VariantSelector = 0;
	Sequence.Reset();
}

void UDreamUIFontEmojiData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif

void UDreamUIFontEmojiData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDreamGUIObjectVersion::GUID);
	Super::Serialize(Ar);
	if (Ar.IsLoading() && Ar.CustomVer(FDreamGUIObjectVersion::GUID) < FDreamGUIObjectVersion::EmojiKeyBySequence)
	{
		// A key saved before sequences stands for its base alone. Its empty Sequence already hashed and compared as
		// {EmojiCode}, so writing that in changes neither, and the key is filled in where it sits in the map -- no entry
		// moves, and none collapses into another.
		for (TPair<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& Pair : DataMap)
		{
			if (Pair.Key.Sequence.Num() == 0 && Pair.Key.EmojiCode != 0)
			{
				Pair.Key.Sequence.Add((int32)Pair.Key.EmojiCode);
			}
		}
	}
}

FDreamUIFontEmojiKey UDreamUIFontEmojiData::MakeKey(const FString& InCluster)
{
	FDreamUIFontEmojiKey Key;
	const int32 Length = InCluster.Len();
	for (int32 Index = 0; Index < Length;)
	{
		int Units = 0;
		const uint32 Codepoint = FDreamUIText_CodePoint::DecodeCodePointAt(InCluster, Length, Index, Units);
		Index += FMath::Max(Units, 1);
		// U+FE0E and U+FE0F pick the presentation, not the picture: U+2764 U+FE0F and a bare U+2764 find the same entry.
		if (FDreamUIText_CodePoint::IsVariationSelector(Codepoint))
		{
			if (Key.VariantSelector == 0)
			{
				Key.VariantSelector = (uint16)Codepoint;
			}
			continue;
		}
		Key.Sequence.Add((int32)Codepoint);
	}
	Key.EmojiCode = Key.Sequence.Num() > 0 ? (uint32)Key.Sequence[0] : 0;
	return Key;
}

const FDreamUIFontEmojiDataItem* UDreamUIFontEmojiData::FindBySequence(const FString& InCluster) const
{
	const FDreamUIFontEmojiKey Key = MakeKey(InCluster);
	// Nothing but variation selectors, or nothing at all, is no emoji: never the entry of an empty key.
	if (Key.Sequence.Num() == 0)
	{
		return nullptr;
	}
	return DataMap.Find(Key);
}

const FDreamUIFontEmojiDataItem* UDreamUIFontEmojiData::FindByCodepoint(uint32 InCodepoint) const
{
	if (InCodepoint == 0)
	{
		return nullptr;
	}
	// A key with no sequence stands for its code point alone, which is what every entry from before sequences is.
	return DataMap.Find(FDreamUIFontEmojiKey(InCodepoint));
}

bool UDreamUIFontEmojiData::GetItemImageSize(const FDreamUIFontEmojiDataItem& InItem, FIntVector2& OutSize)
{
	if (InItem.Frames.Num() == 0)
	{
		return false;
	}
	UDreamUISpriteData_BaseObject* Sprite = InItem.Frames[0].Get();
	if (!IsValid(Sprite))
	{
		return false;
	}
	const auto SpriteWidth = Sprite->GetSpriteInfo().Width;
	const auto SpriteHeight = Sprite->GetSpriteInfo().Height;
	OutSize = FIntVector2(SpriteWidth, SpriteHeight);
	return true;
}

void UDreamUIFontEmojiData::SetDataMap(const TMap<FDreamUIFontEmojiKey, FDreamUIFontEmojiDataItem>& Value)
{
	DataMap = Value;
	OnDataChange.Broadcast();
}
void UDreamUIFontEmojiData::SetAnimationFps(float Value)
{
	AnimationFps = Value;
	OnDataChange.Broadcast();
}
void UDreamUIFontEmojiData::BroadcastOnDataChange()
{
	OnDataChange.Broadcast();
}

void UDreamUIFontEmojiData::CreateOrUpdateObject(UDreamWidget* parent, const TArray<FDreamUIText_Emoji>& emojiData, TArray<TObjectPtr<UDreamWidget>>& createdImageObjectArray)
{
	//destroy extra
	while (createdImageObjectArray.Num() > emojiData.Num())
	{
		auto lastIndex = createdImageObjectArray.Num() - 1;
		auto imageObj = createdImageObjectArray[lastIndex];
		imageObj->DestroyWidget();
		createdImageObjectArray.RemoveAt(lastIndex);
	}
	//create more
	while (createdImageObjectArray.Num() < emojiData.Num())
	{
		auto Widget = NewObject<UDreamWidget>(parent->GetOuter());
		Widget->SetFlags(EObjectFlags::RF_Transient);
		Widget->SetParent(parent, false);
		Widget->CreateNewVisual<UDreamSprite>();
		createdImageObjectArray.Push(Widget);
	}
	//apply data
	for (int i = 0; i < emojiData.Num(); i++)
	{
		auto ImageWidget = createdImageObjectArray[i];
		auto ImageVisual = (UDreamSprite*)ImageWidget->GetVisual();
		if (!ImageVisual)
		{
			ImageVisual = ImageWidget->CreateNewVisual<UDreamSprite>();
		}
		ImageWidget->SetDisplayName(FString::Printf(TEXT("[%d]"), emojiData[i].EmojiCode));
		// The entry the layout sized the emoji by: the one for the exact sequence, else the one for its base code point.
		const FDreamUIFontEmojiDataItem* imageItemPtr = FindBySequence(emojiData[i].Sequence);
		if (imageItemPtr == nullptr)
		{
			imageItemPtr = FindByCodepoint((uint32)emojiData[i].EmojiCode);
		}
		if (imageItemPtr != nullptr)
		{
			auto& spriteFrames = imageItemPtr->Frames;
			auto sequencePlayerComp = ImageWidget->GetComponent<UUISpriteSequencePlayer>();
			if (spriteFrames.Num() == 0)
			{
				ImageVisual->SetSprite(nullptr, false);
				if (IsValid(sequencePlayerComp))
				{
					sequencePlayerComp->DestroyComponent();
				}
			}
			else if (spriteFrames.Num() == 1)
			{
				ImageVisual->SetSprite(spriteFrames[0], false);
				if (IsValid(sequencePlayerComp))
				{
					sequencePlayerComp->DestroyComponent();
				}
			}
			else
			{
				if (!IsValid(sequencePlayerComp))
				{
					// The assignment, which was missing: the branch condition establishes that the
					// pointer is null, so the next line dereferenced null every time an animated
					// emoji of two frames or more landed on a widget that had no player yet.
					// UDreamUIRichTextImageData is the same code with the assignment in place.
					sequencePlayerComp = ImageWidget->AddComponent<UUISpriteSequencePlayer>();
					sequencePlayerComp->SetSnapSpriteSize(false);
				}
				sequencePlayerComp->SetSpriteSequence(spriteFrames);
				sequencePlayerComp->SetFps(imageItemPtr->OverrideAnimationFps < 0 ? AnimationFps : imageItemPtr->OverrideAnimationFps);
				if (DreamUI::IsGameWorld(parent))
				{
					sequencePlayerComp->Play();
				}
			}
			ImageWidget->SetAnchoredPosition(emojiData[i].Position);
			ImageWidget->SetSizeDelta(emojiData[i].Size);
		}
		else
		{
			ImageWidget->SetAnchoredPosition(emojiData[i].Position);
			ImageWidget->SetSizeDelta(FVector2D(emojiData[i].Size));
		}
	}
}
bool UDreamUIFontEmojiData::GetImageSize(const uint32& emojiCode, FIntVector2& outSize)
{
	const FDreamUIFontEmojiDataItem* ImageItemData = FindByCodepoint(emojiCode);
	return ImageItemData != nullptr && GetItemImageSize(*ImageItemData, outSize);
}
