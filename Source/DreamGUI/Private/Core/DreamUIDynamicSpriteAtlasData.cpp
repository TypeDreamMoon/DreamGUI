// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIDynamicSpriteAtlasData.h"

#include "DreamGUI.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUISpriteData.h"
#include "Utils/DreamUIUtils.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "RenderingThread.h"
#include "Rendering/Texture2DResource.h"


void FDreamUIDynamicSpriteAtlasData::EnsureAtlasTexture()
{
	if (AtlasTextureArray.Num() == 0)
	{
		int32 AtlasTextureSize = UDreamUISettings::GetAtlasTextureMaxSize(PackingTag);
		CreateAtlasTexture(AtlasTextureSize);
	}
}
void FDreamUIDynamicSpriteAtlasData::CreateAtlasTexture(int InTextureSize)
{
	static int TextureNameSuffix = 0;
	auto NewTexture = FDreamUIUtils::CreateTexture(InTextureSize, FColor::Transparent
		, GetTransientPackage()
		, FName(*FString::Printf(TEXT("DreamUIDynamicSpriteAtlasData_Texture_%d"), TextureNameSuffix++))
	);

	NewTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
	NewTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	NewTexture->SRGB = UDreamUISettings::GetAtlasTextureSRGB(PackingTag);
	NewTexture->Filter = UDreamUISettings::GetAtlasTextureFilter(PackingTag);
	NewTexture->UpdateResource();
	NewTexture->AddToRoot();
	
	this->AtlasTextureArray.Add(NewTexture);

	rbp::MaxRectsBinPack AtlasBinPack(InTextureSize, InTextureSize);
	AtlasBinPackArray.Add(AtlasBinPack);
}
void FDreamUIDynamicSpriteAtlasData::ExpandAtlasAreaArray()
{
	int32 AtlasTextureSize = UDreamUISettings::GetAtlasTextureMaxSize(PackingTag);
	//create new texture
	this->CreateAtlasTexture(AtlasTextureSize);
}
void FDreamUIDynamicSpriteAtlasData::CheckSprite()
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
				if (itemSprite->GetPackingTag() != PackingTag)
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
			if (!IsValid(IDreamUISpriteRenderInterface::Execute_SpriteRenderGetSprite(itemSprite.Get())))
			{
				this->RenderSpriteArray.RemoveAt(i);
			}
			else
			{
				if (auto spriteData = Cast<UDreamUISpriteData>(IDreamUISpriteRenderInterface::Execute_SpriteRenderGetSprite(itemSprite.Get())))
				{
					if (spriteData->GetPackingTag() != PackingTag)
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

int32 FDreamUIDynamicSpriteAtlasData::GetAtlasTextureSize()
{
	return UDreamUISettings::GetAtlasTextureMaxSize(PackingTag);
}

bool FDreamUIDynamicSpriteAtlasData::PackSprite(UDreamUISpriteData* Sprite)
{
	int32 SpaceBetweenSprites = UDreamUISettings::GetAtlasTexturePadding(PackingTag);

	auto SpriteTexture = Sprite->GetSpriteTexture();
	int InsertRectWidth = SpriteTexture->GetSizeX() + SpaceBetweenSprites + SpaceBetweenSprites;
	int InsertRectHeight = SpriteTexture->GetSizeY() + SpaceBetweenSprites + SpaceBetweenSprites;

	for (int32 i = 0; i < this->AtlasBinPackArray.Num(); i++)
	{
		auto& AtlasBintPack = this->AtlasBinPackArray[i];
		auto PackedRect = AtlasBintPack.Insert(InsertRectWidth, InsertRectHeight, rbp::MaxRectsBinPack::RectBestAreaFit);
		if (PackedRect.height <= 0)//means this area cannot fit the texture
		{
			if (i + 1 == this->AtlasBinPackArray.Num())//last one, means all area can't fit the texture
			{
				if (AtlasBintPack.IsEmpty())//the latest one is empty, means it is newly added area, but it still can't fit the texture
				{
					return false;
				}
				else
				{
					//expand array to get a new area
					this->ExpandAtlasAreaArray();
				}
			}
			continue;
		}
		//this area can fit the texture, then copy pixels to the area
		{
			Sprite->AtlasTexture = this->AtlasTextureArray[i];
			//remove space
			PackedRect.x += SpaceBetweenSprites;
			PackedRect.y += SpaceBetweenSprites;
			PackedRect.width -= SpaceBetweenSprites + SpaceBetweenSprites;
			PackedRect.height -= SpaceBetweenSprites + SpaceBetweenSprites;
			//pixels
			CopySpriteTextureToAtlas(Sprite, Sprite->AtlasTexture, PackedRect, SpaceBetweenSprites);
			//add to Sprite
			auto AtlasTextureSize = this->GetAtlasTextureSize();
			float InvAtlasTextureSize = 1.0f / AtlasTextureSize;
			Sprite->SpriteInfo.ApplyUV(PackedRect.x, PackedRect.y, PackedRect.width, PackedRect.height, InvAtlasTextureSize, InvAtlasTextureSize);
			Sprite->SpriteInfo.ApplyBorderUV(InvAtlasTextureSize, InvAtlasTextureSize);
			this->SpriteDataArray.AddUnique(Sprite);
			return true;
		}
	}
	return false;
}

void FDreamUIDynamicSpriteAtlasData::CopySpriteTextureToAtlas(UDreamUISpriteData* InSprite, UTexture2D* InAtlasTexture, rbp::Rect InPackedRect, int32 InAtlasTexturePadding)
{
	auto SpriteTexture = InSprite->GetSpriteTexture();
	if (!SpriteTexture->GetResource())
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Sprite:%s Texture:%s Resource is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InSprite->GetPathName(), *SpriteTexture->GetPathName());
		return;
	}
	if (!InAtlasTexture->GetResource())
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d PackingTag:%s Texture:%s Resource is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->PackingTag.ToString(), *InAtlasTexture->GetPathName());
		return;
	}
	
	FBox2D srcRegionBox(FVector2D(0, 0), FVector2D(InPackedRect.width, InPackedRect.height));
	FBox2D dstRegionBox(FVector2D(InPackedRect.x, InPackedRect.y), FVector2D(InPackedRect.x + InPackedRect.width, InPackedRect.y + InPackedRect.height));

	struct FUpdateTextureRegionsData
	{
		FTexture2DResource* SpriteTextureResource;
		FTexture2DResource* AtlasTextureResource;
		FBox2D SrcRegionBox;
		FBox2D DstRegionBox;
		int32 SpaceBetweenSprites;
		rbp::Rect PackedRect;
	};
	FUpdateTextureRegionsData RegionData;
	RegionData.SpriteTextureResource = (FTexture2DResource*)SpriteTexture->GetResource();
	RegionData.AtlasTextureResource = (FTexture2DResource*)InAtlasTexture->GetResource();
	RegionData.SrcRegionBox = srcRegionBox;
	RegionData.DstRegionBox = dstRegionBox;
	RegionData.SpaceBetweenSprites = InSprite->bUseEdgePixelPadding ? InAtlasTexturePadding : 0;
	RegionData.PackedRect = InPackedRect;

	ENQUEUE_RENDER_COMMAND(FDreamUISpriteData_CopyTextureData)(
		[RegionData = MoveTemp(RegionData)](FRHICommandListImmediate& RHICmdList)
	{
		auto spriteTextureRHIRef = RegionData.SpriteTextureResource->GetTexture2DRHI();
		auto atlasTextureRHIRef = RegionData.AtlasTextureResource->GetTexture2DRHI();
		auto srcRegionPosition = RegionData.SrcRegionBox.Min;
		auto srcRegionSize = RegionData.SrcRegionBox.GetSize();
		auto dstRegionPosition = RegionData.DstRegionBox.Min;
		auto packedRect = RegionData.PackedRect;
		auto spaceBetweenSprites = RegionData.SpaceBetweenSprites;
		//origin image
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.SourcePosition = FIntVector(srcRegionPosition.X, srcRegionPosition.Y, 0);
		CopyInfo.Size = FIntVector(srcRegionSize.X, srcRegionSize.Y, 0);
		CopyInfo.DestPosition = FIntVector(dstRegionPosition.X, dstRegionPosition.Y, 0);
		RHICmdList.CopyTexture(
			spriteTextureRHIRef,
			atlasTextureRHIRef,
			CopyInfo
		);
		if (spaceBetweenSprites > 0)
		{
			//pixel padding
			for (int paddingIndex = 0; paddingIndex < spaceBetweenSprites; paddingIndex++)
			{
				//Left
				CopyInfo.SourcePosition = FIntVector(0, 0, 0);
				CopyInfo.Size = FIntVector(1, packedRect.height, 0);
				CopyInfo.DestPosition = FIntVector(packedRect.x - paddingIndex - 1, packedRect.y, 0);
				RHICmdList.CopyTexture(
					spriteTextureRHIRef,
					atlasTextureRHIRef,
					CopyInfo
				);
				//Right
				CopyInfo.SourcePosition = FIntVector(packedRect.width - 1, 0, 0);
				CopyInfo.Size = FIntVector(1, packedRect.height, 0);
				CopyInfo.DestPosition = FIntVector(packedRect.x + packedRect.width + paddingIndex, packedRect.y, 0);
				RHICmdList.CopyTexture(
					spriteTextureRHIRef,
					atlasTextureRHIRef,
					CopyInfo
				);
				//Top
				CopyInfo.SourcePosition = FIntVector(0, packedRect.height - 1, 0);
				CopyInfo.Size = FIntVector(packedRect.width, 1, 0);
				CopyInfo.DestPosition = FIntVector(packedRect.x, packedRect.y + packedRect.height + paddingIndex, 0);
				RHICmdList.CopyTexture(
					spriteTextureRHIRef,
					atlasTextureRHIRef,
					CopyInfo
				);
				//Bottom
				CopyInfo.SourcePosition = FIntVector(0, 0, 0);
				CopyInfo.Size = FIntVector(packedRect.width, 1, 0);
				CopyInfo.DestPosition = FIntVector(packedRect.x, packedRect.y - paddingIndex - 1, 0);
				RHICmdList.CopyTexture(
					spriteTextureRHIRef,
					atlasTextureRHIRef,
					CopyInfo
				);
			}
			for (int paddingIndexY = 0; paddingIndexY < spaceBetweenSprites; paddingIndexY++)
			{
				for (int paddingIndexX = 0; paddingIndexX < spaceBetweenSprites; paddingIndexX++)
				{
					//LeftTop
					CopyInfo.SourcePosition = FIntVector(0, packedRect.height - 1, 0);
					CopyInfo.Size = FIntVector(1, 1, 0);
					CopyInfo.DestPosition = FIntVector(packedRect.x - spaceBetweenSprites + paddingIndexX, packedRect.y + packedRect.height + paddingIndexY, 0);
					RHICmdList.CopyTexture(
						spriteTextureRHIRef,
						atlasTextureRHIRef,
						CopyInfo
					);
					//RightTop
					CopyInfo.SourcePosition = FIntVector(packedRect.width - 1, packedRect.height - 1, 0);
					CopyInfo.Size = FIntVector(1, 1, 0);
					CopyInfo.DestPosition = FIntVector(packedRect.x + packedRect.width + paddingIndexX, packedRect.y + packedRect.height + paddingIndexY, 0);
					RHICmdList.CopyTexture(
						spriteTextureRHIRef,
						atlasTextureRHIRef,
						CopyInfo
					);
					//LeftBottom
					CopyInfo.SourcePosition = FIntVector(0, 0, 0);
					CopyInfo.Size = FIntVector(1, 1, 0);
					CopyInfo.DestPosition = FIntVector(packedRect.x - spaceBetweenSprites + paddingIndexX, packedRect.y - 1 - paddingIndexY, 0);
					RHICmdList.CopyTexture(
						spriteTextureRHIRef,
						atlasTextureRHIRef,
						CopyInfo
					);
					//RightBottom
					CopyInfo.SourcePosition = FIntVector(packedRect.width - 1, 0, 0);
					CopyInfo.Size = FIntVector(1, 1, 0);
					CopyInfo.DestPosition = FIntVector(packedRect.x + packedRect.width + paddingIndexX, packedRect.y - 1 - paddingIndexY, 0);
					RHICmdList.CopyTexture(
						spriteTextureRHIRef,
						atlasTextureRHIRef,
						CopyInfo
					);
				}
			}
		}
	});
}

UDreamUIDynamicSpriteAtlasManager* UDreamUIDynamicSpriteAtlasManager::Instance = nullptr;
bool UDreamUIDynamicSpriteAtlasManager::InitCheck()
{
	if (Instance == nullptr)
	{
		Instance = NewObject<UDreamUIDynamicSpriteAtlasManager>();
		Instance->AddToRoot();
	}
	return true;
}
void UDreamUIDynamicSpriteAtlasManager::BeginDestroy()
{
	ResetAtlasMap();
#if WITH_EDITOR
	UDreamUISpriteData::MarkAllSpritesNeedToReinitialize();
#endif
	Instance = nullptr;
	Super::BeginDestroy();
}

FDreamUIDynamicSpriteAtlasData* UDreamUIDynamicSpriteAtlasManager::FindOrAdd(const FName& InPackingTag)
{
	if (InitCheck())
	{
		if (!Instance->AtlasMap.Contains(InPackingTag))
		{
			auto Result = &(Instance->AtlasMap.Add(InPackingTag));
			if (Instance->OnAtlasMapChanged.IsBound())
			{
				Instance->OnAtlasMapChanged.Broadcast();
			}
			Result->PackingTag = InPackingTag;
			return Result;
		}
		return Instance->AtlasMap.Find(InPackingTag);
	}
	return nullptr;
}
FDreamUIDynamicSpriteAtlasData* UDreamUIDynamicSpriteAtlasManager::Find(const FName& InPackingTag)
{
	if (Instance != nullptr)
	{
		return Instance->AtlasMap.Find(InPackingTag);
	}
	return nullptr;
}
void UDreamUIDynamicSpriteAtlasManager::ResetAtlasMap()
{
	if (Instance != nullptr)
	{
		for (auto& AtlasMapKeyValue : Instance->AtlasMap)
		{
			for (auto& AtlasTexture : AtlasMapKeyValue.Value.AtlasTextureArray)
			{
				AtlasTexture->RemoveFromRoot();
			}
		}
		Instance->AtlasMap.Empty();
		if (Instance->OnAtlasMapChanged.IsBound())
		{
			Instance->OnAtlasMapChanged.Broadcast();
		}
	}
}

void UDreamUIDynamicSpriteAtlasManager::DisposeAtlasByPackingTag(FName InPackingTag)
{
	if (Instance != nullptr)
	{
		Instance->AtlasMap.Remove(InPackingTag);
	}
}
