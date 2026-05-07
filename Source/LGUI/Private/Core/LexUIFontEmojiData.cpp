// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIFontEmojiData.h"

#include "Core/LexUIManager.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/Actor/LexWidgetContainer.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexSprite.h"
#include "Engine/World.h"

#if WITH_EDITOR

void FLexUIFontEmojiKey::ApplyEmoji()
{
	int ValidLength = 0;
	if (EmojiChar.Len() >= 2)
	{
		auto highSurrogate = EmojiChar[0];
		auto lowSurrogate = EmojiChar[1];
		if (highSurrogate >= FLexUIText_CodePoint::HIGH_SURROGATE_START && highSurrogate <= FLexUIText_CodePoint::HIGH_SURROGATE_END
		&& lowSurrogate >= FLexUIText_CodePoint::LOW_SURROGATE_START && lowSurrogate <= FLexUIText_CodePoint::LOW_SURROGATE_END)
		{
			if (EmojiChar.Len() == 3
				&& (EmojiChar[2] == FLexUIText_CodePoint::UNICODE_VS_BLACK || EmojiChar[2] == FLexUIText_CodePoint::UNICODE_VS_COLOR))
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
		EmojiCode = FLexUIText_CodePoint::ConvertToUTF32(highSurrogate, lowSurrogate);
		EmojiChar = EmojiChar.Left(ValidLength);
	}
	if (ValidLength == 0)
	{
		EmojiChar = "";
		EmojiCode = 0;
		VariantSelector = 0;
	}
}

void ULexUIFontEmojiData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif

void ULexUIFontEmojiData::SetDataMap(const TMap<FLexUIFontEmojiKey, FLexUIFontEmojiDataItem>& Value)
{
	DataMap = Value;
	OnDataChange.Broadcast();
}
void ULexUIFontEmojiData::SetAnimationFps(float Value)
{
	AnimationFps = Value;
	OnDataChange.Broadcast();
}
void ULexUIFontEmojiData::BroadcastOnDataChange()
{
	OnDataChange.Broadcast();
}

void ULexUIFontEmojiData::CreateOrUpdateObject(ULexWidget* parent, const TArray<FLexUIText_Emoji>& emojiData, TArray<TObjectPtr<ULexWidget>>& createdImageObjectArray, bool listImageObjectInEditorOutliner)
{
	//destroy extra
	while (createdImageObjectArray.Num() > emojiData.Num())
	{
		auto lastIndex = createdImageObjectArray.Num() - 1;
		auto imageObj = createdImageObjectArray[lastIndex];
		FLexUIUtils::DestroyActorWithHierarchy(imageObj->GetOwner());
		createdImageObjectArray.RemoveAt(lastIndex);
	}
	//create more
	while (createdImageObjectArray.Num() < emojiData.Num())
	{
		auto spriteActor = parent->GetWorld()->SpawnActor<ULexWidgetContainer>();
		spriteActor->SetFlags(EObjectFlags::RF_Transient);
		spriteActor->GetLexWidget()->AttachToComponent(parent, FAttachmentTransformRules::KeepRelativeTransform);
		createdImageObjectArray.Push(spriteActor->GetLexWidget());
	}
	//apply data
	for (int i = 0; i < emojiData.Num(); i++)
	{
		auto ImageWidget = createdImageObjectArray[i];
		auto ImageVisual = Cast<ULexSprite>(ImageWidget->GetVisual());
		if (!ImageVisual)
		{
			ImageVisual = ImageWidget->CreateNewVisual<ULexSprite>();
		}
#if WITH_EDITOR
		ImageWidget->GetOwner()->SetActorLabel(FString::Printf(TEXT("[%d]"), emojiData[i].EmojiCode));
		if (!parent->GetWorld()->IsGameWorld())//set it only in edit mode
		{
			auto bListedInSceneOutliner_Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bListedInSceneOutliner"));
			bListedInSceneOutliner_Property->SetPropertyValue_InContainer(ImageWidget->GetOwner(), listImageObjectInEditorOutliner);
		}
#endif
		if (auto imageItemPtr = DataMap.Find(emojiData[i].EmojiCode))
		{
			auto& spriteFrames = imageItemPtr->Frames;
			auto sequencePlayerComp = ImageWidget->GetOwner()->FindComponentByClass<UUISpriteSequencePlayer>();
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
					sequencePlayerComp = NewObject<UUISpriteSequencePlayer>(ImageWidget->GetOwner());
					sequencePlayerComp->SetSnapSpriteSize(false);
					sequencePlayerComp->RegisterComponent();
					ImageWidget->GetOwner()->AddInstanceComponent(sequencePlayerComp);
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
#if WITH_EDITOR
	if (!parent->GetWorld()->IsGameWorld())//refresh on editor
	{
		ULexUIManagerObject::MarkBroadcastLevelActorListChanged();
	}
#endif
}
bool ULexUIFontEmojiData::GetImageSize(const uint32& emojiCode, FIntVector2& outSize)
{
	auto ImageItemData = DataMap.Find(emojiCode);
	if (!ImageItemData)return false;
	if (ImageItemData->Frames.Num() == 0)
		return false;
	ULexUISpriteData_BaseObject* sprite = ImageItemData->Frames[0].Get();
	if (!IsValid(sprite))
		return false;

	auto spriteWidth = sprite->GetSpriteInfo().Width;
	auto spriteHeight = sprite->GetSpriteInfo().Height;
	outSize = FIntVector2(spriteWidth, spriteHeight);
	return true;
}
