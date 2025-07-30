// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIRichTextImageData.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/UISprite.h"
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
	imageMap = value;
	OnDataChange.Broadcast();
}
void ULexUIRichTextImageData::SetAnimationFps(float value)
{
	animationFps = value;
	OnDataChange.Broadcast();
}
void ULexUIRichTextImageData::BroadcastOnDataChange()
{
	OnDataChange.Broadcast();
}

void ULexUIRichTextImageData::CreateOrUpdateObject(ULexWidget* parent, const TArray<FLexUIText_RichTextImageTag>& imageTagData, TArray<TObjectPtr<ULexWidget>>& createdImageObjectArray, bool listImageObjectInEditorOutliner)
{
	//destroy extra
	while (createdImageObjectArray.Num() > imageTagData.Num())
	{
		auto lastIndex = createdImageObjectArray.Num() - 1;
		auto imageObj = createdImageObjectArray[lastIndex];
		FLexUIUtils::DestroyActorWithHierarchy(imageObj->GetOwner());
		createdImageObjectArray.RemoveAt(lastIndex);
	}
	//create more
	while (createdImageObjectArray.Num() < imageTagData.Num())
	{
		auto spriteActor = parent->GetWorld()->SpawnActor<ALexWidgetActor>();
		spriteActor->SetFlags(EObjectFlags::RF_Transient);
		spriteActor->GetLexWidget()->AttachToComponent(parent, FAttachmentTransformRules::KeepRelativeTransform);
		createdImageObjectArray.Push(spriteActor->GetLexWidget());
	}
	//apply data
	for (int i = 0; i < imageTagData.Num(); i++)
	{
		auto ImageWidget = createdImageObjectArray[i];
		auto ImageVisual = (UUISprite*)ImageWidget->GetVisual();
#if WITH_EDITOR
		ImageWidget->GetOwner()->SetActorLabel(FString::Printf(TEXT("[%s]"), *imageTagData[i].TagName.ToString()));
		if (!parent->GetWorld()->IsGameWorld())//set it only in edit mode
		{
			auto bListedInSceneOutliner_Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bListedInSceneOutliner"));
			bListedInSceneOutliner_Property->SetPropertyValue_InContainer(ImageWidget->GetOwner(), listImageObjectInEditorOutliner);
		}
#endif
		if (auto imageItemPtr = imageMap.Find(imageTagData[i].TagName))
		{
			auto& spriteFrames = imageItemPtr->frames;
			auto sequencePlayerComp = ImageWidget->GetOwner()->FindComponentByClass<UUISpriteSequencePlayer>();
			ULexUISpriteData_BaseObject* sprite = nullptr;
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
				sprite = spriteFrames[0];
				if (IsValid(sequencePlayerComp))
				{
					sequencePlayerComp->DestroyComponent();
				}
			}
			else
			{
				sprite = spriteFrames[0];
				if (!IsValid(sequencePlayerComp))
				{
					sequencePlayerComp = NewObject<UUISpriteSequencePlayer>(ImageWidget->GetOwner());
					sequencePlayerComp->SetSnapSpriteSize(false);
					sequencePlayerComp->RegisterComponent();
					ImageWidget->GetOwner()->AddInstanceComponent(sequencePlayerComp);
				}
				sequencePlayerComp->SetSpriteSequence(spriteFrames);
				sequencePlayerComp->SetFps(imageItemPtr->overrideAnimationFps < 0 ? animationFps : imageItemPtr->overrideAnimationFps);
				if (parent->GetWorld()->IsGameWorld())
				{
					sequencePlayerComp->Play();
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
#if WITH_EDITOR
	if (!parent->GetWorld()->IsGameWorld())//refresh on editor
	{
		ULGUIPrefabManagerObject::MarkBroadcastLevelActorListChanged();
	}
#endif
}
bool ULexUIRichTextImageData::GetImageSize(const FName& imageTag, FIntVector2& outSize)
{
	auto ImageItemData = imageMap.Find(imageTag);
	if (!ImageItemData)return false;
	if (ImageItemData->frames.Num() == 0)
		return false;
	ULexUISpriteData_BaseObject* sprite = ImageItemData->frames[0].Get();
	if (!IsValid(sprite))
		return false;

	auto spriteWidth = sprite->GetSpriteInfo().width;
	auto spriteHeight = sprite->GetSpriteInfo().height;
	outSize = FIntVector2(spriteWidth, spriteHeight);
	return true;
}
