// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIRichTextImageData.h"

#include "Core/LexUIManager.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexSprite.h"
#include "Engine/World.h"

#if WITH_EDITOR
void ULexUIRichTextImageData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif

void ULexUIRichTextImageData::SetImageMap(const TMap<FName, FLexUIRichTextImageItemData>& value)
{
	ImageMap = value;
	OnDataChange.Broadcast();
}
void ULexUIRichTextImageData::SetAnimationFps(float value)
{
	AnimationFps = value;
	OnDataChange.Broadcast();
}
void ULexUIRichTextImageData::BroadcastOnDataChange()
{
	OnDataChange.Broadcast();
}

void ULexUIRichTextImageData::CreateOrUpdateObject(ULexWidget* parent, const TArray<FLexUIText_RichTextImageTag>& imageTagData, TArray<TObjectPtr<ULexWidget>>& CreatedImageObjectArray)
{
	//destroy extra
	while (CreatedImageObjectArray.Num() > imageTagData.Num())
	{
		auto lastIndex = CreatedImageObjectArray.Num() - 1;
		auto imageObj = CreatedImageObjectArray[lastIndex];
		imageObj->DestroyWidget();
		CreatedImageObjectArray.RemoveAt(lastIndex);
	}
	//create more
	while (CreatedImageObjectArray.Num() < imageTagData.Num())
	{
		auto Widget = NewObject<ULexWidget>(parent->GetOuter());
		Widget->SetFlags(EObjectFlags::RF_Transient);
		Widget->SetParent(parent, false);
		Widget->CreateNewVisual<ULexSprite>();
		CreatedImageObjectArray.Push(Widget);
	}
	//apply data
	for (int i = 0; i < imageTagData.Num(); i++)
	{
		auto ImageWidget = CreatedImageObjectArray[i];
		auto ImageVisual = (ULexSprite*)ImageWidget->GetVisual();
		ImageWidget->SetDisplayName(FString::Printf(TEXT("[%s]"), *imageTagData[i].TagName.ToString()));
		if (auto imageItemPtr = ImageMap.Find(imageTagData[i].TagName))
		{
			auto& spriteFrames = imageItemPtr->Frames;
			auto SequencePlayerComp = ImageWidget->GetComponent<UUISpriteSequencePlayer>();
			if (spriteFrames.Num() == 0)
			{
				ImageVisual->SetSprite(nullptr, false);
				if (IsValid(SequencePlayerComp))
				{
					SequencePlayerComp->DestroyComponent();
				}
			}
			else if (spriteFrames.Num() == 1)
			{
				if (IsValid(SequencePlayerComp))
				{
					SequencePlayerComp->DestroyComponent();
				}
			}
			else
			{
				if (!IsValid(SequencePlayerComp))
				{
					SequencePlayerComp = ImageWidget->AddComponent<UUISpriteSequencePlayer>();
					SequencePlayerComp->SetSnapSpriteSize(false);
				}
				SequencePlayerComp->SetSpriteSequence(spriteFrames);
				SequencePlayerComp->SetFps(imageItemPtr->OverrideAnimationFps < 0 ? AnimationFps : imageItemPtr->OverrideAnimationFps);
				if (parent->GetWorld()->IsGameWorld())
				{
					SequencePlayerComp->Play();
				}
			}
			ImageVisual->SetColor(imageTagData[i].TintColor);
			ImageWidget->SetAnchoredPosition(imageTagData[i].Position);
			ImageWidget->SetSizeDelta(imageTagData[i].Size);
		}
		else
		{
			ImageVisual->SetColor(imageTagData[i].TintColor);
			ImageWidget->SetAnchoredPosition(imageTagData[i].Position);
			ImageWidget->SetSizeDelta(FVector2D(imageTagData[i].Size));
		}
	}
}
bool ULexUIRichTextImageData::GetImageSize(const FName& imageTag, FIntVector2& outSize)
{
	auto ImageItemData = ImageMap.Find(imageTag);
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
