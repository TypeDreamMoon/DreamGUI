// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIDynamicSpriteAtlasData.h"
#include "Core/LexUIStaticSpriteAtlasData.h"
#include "LGUI.h"
#include "Core/LGUISettings.h"
#include "Core/LGUIManager.h"
#include "LGUI/Public/Core/Components/UISpriteBase.h"
#include "Core/LexUISpriteData.h"
#include "Utils/LexUIUtils.h"
#include "Rendering/Texture2DResource.h"
#include "Core/ILexUISpriteRenderInterface.h"
#include "RenderingThread.h"


void FLexUIDynamicSpriteAtlasData::EnsureAtlasTexture(const FName& packingTag)
{
	if (!IsValid(AtlasTexture))
	{
#if WITH_EDITOR
		int32 defaultAtlasTextureSize = ULGUISettings::GetAtlasTextureInitialSize(packingTag);
#else
		static int32 defaultAtlasTextureSize = ULGUISettings::GetAtlasTextureInitialSize(PackingTag);
#endif
		AtlasBinPack.Init(defaultAtlasTextureSize, defaultAtlasTextureSize);
		CreateAtlasTexture(packingTag, 0, defaultAtlasTextureSize);
	}
}
void FLexUIDynamicSpriteAtlasData::CreateAtlasTexture(const FName& packingTag, int oldTextureSize, int newTextureSize)
{
#if WITH_EDITOR
	bool atlasSRGB = ULGUISettings::GetAtlasTextureSRGB(packingTag);
	auto filter = ULGUISettings::GetAtlasTextureFilter(packingTag);
#else
	static bool atlasSRGB = ULGUISettings::GetAtlasTextureSRGB(PackingTag);
	static auto filter = ULGUISettings::GetAtlasTextureFilter(PackingTag);
#endif
	static int TextureNameSuffix = 0;
	auto texture = FLexUIUtils::CreateTexture(newTextureSize, FColor::Transparent
		, GetTransientPackage()
		, FName(*FString::Printf(TEXT("LexUIDynamicSpriteAtlasData_Texture_%d"), TextureNameSuffix++))
	);

	texture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
	texture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	texture->SRGB = atlasSRGB;
	texture->Filter = filter;
	texture->UpdateResource();
	texture->AddToRoot();//@todo: is this really need to AddToRoot?
	auto OldTexture = this->AtlasTexture;
	this->AtlasTexture = texture;

	//copy old texture to new one
	if (IsValid(OldTexture) && oldTextureSize > 0)
	{
		auto NewTexture = texture;
		if (OldTexture->GetResource() != nullptr && NewTexture->GetResource() != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(FLGUIDynamicSpriteAtlas_CopyAtlasTexture)(
				[OldTexture, NewTexture, oldTextureSize](FRHICommandListImmediate& RHICmdList)
			{
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.SourcePosition = FIntVector(0, 0, 0);
				CopyInfo.Size = FIntVector(oldTextureSize, oldTextureSize, 0);
				CopyInfo.DestPosition = FIntVector(0, 0, 0);
				RHICmdList.CopyTexture(
					((FTexture2DResource*)OldTexture->GetResource())->GetTexture2DRHI(),
					((FTexture2DResource*)NewTexture->GetResource())->GetTexture2DRHI(),
					CopyInfo
				);
				OldTexture->RemoveFromRoot();//ready for gc
			});
		}
		else
		{
			OldTexture->RemoveFromRoot();//ready for gc
		}
	}
}
int32 FLexUIDynamicSpriteAtlasData::ExpendTextureSize(const FName& packingTag)
{
	int32 oldTextureSize = this->AtlasBinPack.GetBinWidth();
	int32 newTextureSize = oldTextureSize + oldTextureSize;

	this->AtlasBinPack.ExpendSize(newTextureSize, newTextureSize);
	//create new texture
	this->CreateAtlasTexture(packingTag, oldTextureSize, newTextureSize);
	//scale down Sprite uv
	for (ULexUISpriteData* spriteItem : this->SpriteDataArray)
	{
		if (IsValid(spriteItem))
		{
			spriteItem->AtlasTexture = this->AtlasTexture;
			spriteItem->SpriteInfo.ScaleUV(0.5f);
		}
	}
	//tell UISprite to scale down uv
	for (auto itemSprite : this->RenderSpriteArray)
	{
		if (itemSprite.IsValid())
		{
			ILexUISpriteRenderInterface::Execute_ApplyAtlasTextureScaleUp(itemSprite.Get());
		}
	}
	//callback function
	if (OnTextureSizeExpanded.IsBound())
	{
		OnTextureSizeExpanded.Broadcast(this->AtlasTexture, newTextureSize);
	}

	return newTextureSize;
}
int32 FLexUIDynamicSpriteAtlasData::GetWillExpendTextureSize()const
{
	int32 oldTextureSize = this->AtlasBinPack.GetBinWidth();
	return oldTextureSize + oldTextureSize;
}
void FLexUIDynamicSpriteAtlasData::CheckSprite(const FName& packingTag)
{
	for (int i = this->SpriteDataArray.Num() - 1; i >= 0; i--)
	{
		auto itemSprite = this->SpriteDataArray[i];
		if (IsValid(itemSprite))
		{
			if (IsValid(itemSprite->GetPackingAtlas()))
			{
				this->SpriteDataArray.RemoveAt(i);
			}
			else
			{
				if (itemSprite->GetPackingTag() != packingTag)
				{
					this->SpriteDataArray.RemoveAt(i);
				}
			}
		}
		else
		{
			this->SpriteDataArray.RemoveAt(i);
		}
	}
	for (int i = this->RenderSpriteArray.Num() - 1; i >= 0; i--)
	{
		auto itemSprite = this->RenderSpriteArray[i];
		if (itemSprite.IsValid())
		{
			if (!IsValid(ILexUISpriteRenderInterface::Execute_SpriteRenderGetSprite(itemSprite.Get())))
			{
				this->RenderSpriteArray.RemoveAt(i);
			}
			else
			{
				if (auto spriteData = Cast<ULexUISpriteData>(ILexUISpriteRenderInterface::Execute_SpriteRenderGetSprite(itemSprite.Get())))
				{
					if (spriteData->GetPackingTag() != packingTag)
					{
						this->RenderSpriteArray.RemoveAt(i);
					}
				}
				else
				{
					this->RenderSpriteArray.RemoveAt(i);
				}
			}
		}
		else
		{
			this->RenderSpriteArray.RemoveAt(i);
		}
	}
}

ULexUIDynamicSpriteAtlasManager* ULexUIDynamicSpriteAtlasManager::Instance = nullptr;
bool ULexUIDynamicSpriteAtlasManager::InitCheck()
{
	if (Instance == nullptr)
	{
		Instance = NewObject<ULexUIDynamicSpriteAtlasManager>();
		Instance->AddToRoot();
	}
	return true;
}
void ULexUIDynamicSpriteAtlasManager::BeginDestroy()
{
	ResetAtlasMap();
#if WITH_EDITOR
	ULexUISpriteData::MarkAllSpritesNeedToReinitialize();
#endif
	Instance = nullptr;
	Super::BeginDestroy();
}

FLexUIDynamicSpriteAtlasData* ULexUIDynamicSpriteAtlasManager::FindOrAdd(const FName& packingTag)
{
	if (InitCheck())
	{
		if (!Instance->AtlasMap.Contains(packingTag))
		{
			auto Result = &(Instance->AtlasMap.Add(packingTag));
			if (Instance->OnAtlasMapChanged.IsBound())
			{
				Instance->OnAtlasMapChanged.Broadcast();
			}
			return Result;
		}
		return Instance->AtlasMap.Find(packingTag);
	}
	return nullptr;
}
FLexUIDynamicSpriteAtlasData* ULexUIDynamicSpriteAtlasManager::Find(const FName& packingTag)
{
	if (Instance != nullptr)
	{
		return Instance->AtlasMap.Find(packingTag);
	}
	return nullptr;
}
void ULexUIDynamicSpriteAtlasManager::ResetAtlasMap()
{
	if (Instance != nullptr)
	{
		for (auto& item : Instance->AtlasMap)
		{
			if (IsValid(item.Value.AtlasTexture))
			{
				item.Value.AtlasTexture->RemoveFromRoot();
				item.Value.AtlasTexture->ConditionalBeginDestroy();
			}
		}
		Instance->AtlasMap.Empty();
		if (Instance->OnAtlasMapChanged.IsBound())
		{
			Instance->OnAtlasMapChanged.Broadcast();
		}
	}
}

void ULexUIDynamicSpriteAtlasManager::DisposeAtlasByPackingTag(FName inPackingTag)
{
	if (Instance != nullptr)
	{
		if (auto atlasData = Find(inPackingTag))
		{
			atlasData->AtlasTexture->RemoveFromRoot();
			Instance->AtlasMap.Remove(inPackingTag);
		}
	}
}
