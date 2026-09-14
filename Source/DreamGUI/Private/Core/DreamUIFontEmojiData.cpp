// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUIWorldContext.h"

#include "Extensions/UISpriteSequencePlayer.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamSprite.h"
#include "Engine/World.h"

#if WITH_EDITOR

void FDreamUIFontEmojiKey::ApplyEmoji()
{
	// One grapheme cluster, segmented by exactly the function the text pipeline segments with, so what
	// an author can paste in here is what a text will look up: a plain emoji, a BMP symbol wearing
	// U+FE0F, a ZWJ family, a skin tone, a flag. Registering used to require a bare surrogate pair and
	// truncate everything longer to its first pair, which registered a different character than the one
	// on screen. The key is still the cluster's BASE code point -- FDreamUIFontEmojiKey hashes and
	// compares EmojiCode alone, and VariantSelector has always been carried but not keyed -- so the
	// variants of one base share an entry.
	const FString Source = EmojiChar;
	const int32 SourceLength = Source.Len();
	if (SourceLength > 0)
	{
		int CharIndex = 0;
		const auto Element = FDreamUIText_CodePoint::ReadCodePoint(Source, SourceLength, CharIndex);
		if (Element.Type == EDreamUIText_CodeType::Emoji)
		{
			EmojiCode = Element.Unicode;
			EmojiChar = Source.Mid(Element.StringIndex, Element.Length);
			VariantSelector = 0;
			for (int32 i = Element.StringIndex; i < Element.StringIndex + Element.Length; i++)
			{
				if (FDreamUIText_CodePoint::IsVariationSelector((uint32)Source[i]))
				{
					VariantSelector = (uint16)Source[i];
					break;
				}
			}
			return;
		}
	}
	EmojiChar = "";
	EmojiCode = 0;
	VariantSelector = 0;
}

void UDreamUIFontEmojiData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif

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
		if (auto imageItemPtr = DataMap.Find(emojiData[i].EmojiCode))
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
	auto ImageItemData = DataMap.Find(emojiCode);
	if (!ImageItemData)return false;
	if (ImageItemData->Frames.Num() == 0)
		return false;
	UDreamUISpriteData_BaseObject* sprite = ImageItemData->Frames[0].Get();
	if (!IsValid(sprite))
		return false;

	auto spriteWidth = sprite->GetSpriteInfo().Width;
	auto spriteHeight = sprite->GetSpriteInfo().Height;
	outSize = FIntVector2(spriteWidth, spriteHeight);
	return true;
}
