// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRichTextImageData.h"
#include "Core/DreamUIWorldContext.h"

#include "Core/DreamUIManager.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamSprite.h"
#include "Engine/World.h"

#if WITH_EDITOR
void UDreamUIRichTextImageData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnDataChange.Broadcast();
}
#endif

void UDreamUIRichTextImageData::SetImageMap(const TMap<FName, FDreamUIRichTextImageItemData>& value)
{
	ImageMap = value;
	OnDataChange.Broadcast();
}
void UDreamUIRichTextImageData::SetAnimationFps(float value)
{
	AnimationFps = value;
	OnDataChange.Broadcast();
}
void UDreamUIRichTextImageData::BroadcastOnDataChange()
{
	OnDataChange.Broadcast();
}

void UDreamUIRichTextImageData::CreateOrUpdateObject(UDreamWidget* parent, const TArray<FDreamUIText_RichTextImageTag>& imageTagData, TArray<TObjectPtr<UDreamWidget>>& CreatedImageObjectArray)
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
		auto Widget = NewObject<UDreamWidget>(parent->GetOuter());
		Widget->SetFlags(EObjectFlags::RF_Transient);
		Widget->SetParent(parent, false);
		Widget->CreateNewVisual<UDreamSprite>();
		CreatedImageObjectArray.Push(Widget);
	}
	//apply data
	for (int i = 0; i < imageTagData.Num(); i++)
	{
		auto ImageWidget = CreatedImageObjectArray[i];
		auto ImageVisual = (UDreamSprite*)ImageWidget->GetVisual();
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
				if (DreamUI::IsGameWorld(parent))
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
bool UDreamUIRichTextImageData::GetImageSize(const FName& imageTag, FIntVector2& outSize)
{
	auto ImageItemData = ImageMap.Find(imageTag);
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
