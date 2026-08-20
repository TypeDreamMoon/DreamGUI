// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontEmojiData.h"

#include "Extensions/UISpriteSequencePlayer.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamSprite.h"
#include "Engine/World.h"

#if WITH_EDITOR

void FDreamUIFontEmojiKey::ApplyEmoji()
{
	int ValidLength = 0;
	if (EmojiChar.Len() >= 2)
	{
		auto highSurrogate = EmojiChar[0];
		auto lowSurrogate = EmojiChar[1];
		if (highSurrogate >= FDreamUIText_CodePoint::HIGH_SURROGATE_START && highSurrogate <= FDreamUIText_CodePoint::HIGH_SURROGATE_END
		&& lowSurrogate >= FDreamUIText_CodePoint::LOW_SURROGATE_START && lowSurrogate <= FDreamUIText_CodePoint::LOW_SURROGATE_END)
		{
			if (EmojiChar.Len() == 3
				&& (EmojiChar[2] == FDreamUIText_CodePoint::UNICODE_VS_BLACK || EmojiChar[2] == FDreamUIText_CodePoint::UNICODE_VS_COLOR))
			{
				ValidLength = 3;
				VariantSelector = EmojiChar[2];
			}
			else
			{
				ValidLength = 2;
				VariantSelector = 0;
			}
		}
		EmojiCode = FDreamUIText_CodePoint::ConvertToUTF32(highSurrogate, lowSurrogate);
		EmojiChar = EmojiChar.Left(ValidLength);
	}
	if (ValidLength == 0)
	{
		EmojiChar = "";
		EmojiCode = 0;
		VariantSelector = 0;
	}
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
					ImageWidget->AddComponent<UUISpriteSequencePlayer>();
					sequencePlayerComp->SetSnapSpriteSize(false);
				}
				sequencePlayerComp->SetSpriteSequence(spriteFrames);
				sequencePlayerComp->SetFps(imageItemPtr->OverrideAnimationFps < 0 ? AnimationFps : imageItemPtr->OverrideAnimationFps);
				if (parent->GetWorld()->IsGameWorld())
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
