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
	// Owned by the atlas entry, not by the root set. AtlasTextureArray is a UPROPERTY of a struct held
	// in the (rooted) manager's AtlasMap, so the page lives exactly as long as the entry that lists
	// it -- and dropping the entry is enough to let it go. AddToRoot here made a second, independent
	// claim that only a matching RemoveFromRoot could release, which is why every code path that
	// removes an entry had to remember to un-root by hand, and why forgetting once leaked a whole page.
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

void FDreamUIDynamicSpriteAtlasData::ReleaseAtlasTextures()
{
	// A packed sprite holds the page it landed in and a UV rect inside it, and answers
	// GetAtlasTexture()/GetSpriteInfo() from those without asking anyone. Both have to go back to
	// "not packed yet", or the page survives through the sprite's own reference (so nothing is
	// actually freed) and the sprite samples a rect no bin pack owns any more.
	for (auto& SpriteData : SpriteDataArray)
	{
		if (!IsValid(SpriteData))continue;
		SpriteData->bIsInitialized = false;
		SpriteData->AtlasTexture = nullptr;
	}
	SpriteDataArray.Reset();
	AtlasTextureArray.Reset();
	AtlasBinPackArray.Reset();
}

int32 FDreamUIDynamicSpriteAtlasData::GetAtlasTextureSize()
{
	return UDreamUISettings::GetAtlasTextureMaxSize(PackingTag);
}

int32 FDreamUIDynamicSpriteAtlasData::GetInsertAreaForSprite(const UDreamUISpriteData* Sprite)const
{
	if (!IsValid(Sprite))return 0;
	auto SpriteTexture = Sprite->GetSpriteTexture();
	if (!IsValid(SpriteTexture))return 0;
	const int32 SpaceBetweenSprites = UDreamUISettings::GetAtlasTexturePadding(PackingTag);
	const int32 InsertRectWidth = SpriteTexture->GetSizeX() + SpaceBetweenSprites + SpaceBetweenSprites;
	const int32 InsertRectHeight = SpriteTexture->GetSizeY() + SpaceBetweenSprites + SpaceBetweenSprites;
	return InsertRectWidth * InsertRectHeight;
}

void FDreamUIDynamicSpriteAtlasData::TouchSprite(UDreamUISpriteData* Sprite)
{
	if (!IsValid(Sprite))return;
	// SpriteDataArray is kept in least-recently-used order, oldest first, and this is the only thing
	// that decides that order. Touched on every pack and on every registration, which between them
	// are the two moments something starts using a sprite.
	SpriteDataArray.Remove(Sprite);
	SpriteDataArray.Add(Sprite);
}

int32 FDreamUIDynamicSpriteAtlasData::EvictUnusedSprites(int32 InRequiredArea)
{
	// "Unused" is not a guess: RenderSpriteArray is every element currently drawing from this atlas,
	// and each one can be asked which sprite it draws. A sprite nothing in that list names is packed
	// into a rectangle no pixel is ever sampled from -- dead area that the bin pack cannot hand back,
	// which is why an atlas that only ever grows grows forever.
	CheckSprite();
	TSet<const UObject*> InUseSprites;
	InUseSprites.Reserve(RenderSpriteArray.Num());
	for (auto& RenderSprite : RenderSpriteArray)
	{
		if (!RenderSprite.IsValid())continue;
		if (auto UsedSprite = IDreamUISpriteRenderInterface::Execute_SpriteRenderGetSprite(RenderSprite.Get()))
		{
			InUseSprites.Add(UsedSprite);
		}
	}

	TArray<TObjectPtr<UDreamUISpriteData>> Victims;
	int32 FreedArea = 0;
	//oldest first: the least recently used unused sprite is the one whose rectangle is cheapest to lose
	for (auto& SpriteData : SpriteDataArray)
	{
		if (!IsValid(SpriteData))continue;
		if (InUseSprites.Contains((const UObject*)SpriteData.Get()))continue;
		Victims.Add(SpriteData);
		FreedArea += GetInsertAreaForSprite(SpriteData);
		if (InRequiredArea > 0 && FreedArea >= InRequiredArea)
		{
			//enough room asked for; the warmer unused sprites keep their rectangles and their pixels
			break;
		}
	}
	if (Victims.Num() == 0)
	{
		return 0;
	}

	for (auto& Victim : Victims)
	{
		SpriteDataArray.Remove(Victim);
		//back to "not packed yet": the next time anything asks for its texture it repacks from scratch
		Victim->bIsInitialized = false;
		Victim->AtlasTexture = nullptr;
	}
	RepackPages();
	return Victims.Num();
}

void FDreamUIDynamicSpriteAtlasData::RepackPages()
{
	// rbp::MaxRectsBinPack has no "free this rectangle" operation -- the MAXRECTS structure cannot
	// take one back without rebuilding its free list -- so reclaiming space means rebuilding the
	// pages from the sprites that survive. Order is LRU, oldest first, which keeps the pages that
	// hold the coldest sprites at the front and leaves the tail free to be dropped.
	const int32 AtlasTextureSize = GetAtlasTextureSize();
	for (auto& AtlasBinPack : AtlasBinPackArray)
	{
		AtlasBinPack.Init(AtlasTextureSize, AtlasTextureSize);
	}
	//the pixels left behind by evicted sprites are not cleared: nothing samples them, and the next
	//sprite to land there overwrites them. Clearing would cost a full-page upload per eviction.
	TArray<TObjectPtr<UDreamUISpriteData>> ToRepack = MoveTemp(SpriteDataArray);
	SpriteDataArray.Reset();
	for (auto& SpriteData : ToRepack)
	{
		if (!IsValid(SpriteData))continue;
		if (!TryPackSpriteIntoExistingPages(SpriteData))
		{
			// It fitted before the rebuild and does not fit now, which a heuristic packer is allowed
			// to do. Losing it is recoverable -- it repacks on its next use -- but it is worth saying.
			SpriteData->bIsInitialized = false;
			SpriteData->AtlasTexture = nullptr;
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d PackingTag:%s Sprite:%s did not fit when its atlas was rebuilt; it will repack on next use.")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->PackingTag.ToString(), *SpriteData->GetPathName());
		}
	}
	DropEmptyPages();
	//last on purpose: a notified element may ask its sprite for a texture, and a sprite that lost its
	//place above would repack from inside that call -- which is safe only once this is consistent
	NotifyRenderSpritesAtlasChanged();
}

int32 FDreamUIDynamicSpriteAtlasData::DropEmptyPages()
{
	// Trailing pages only, and never the first: a page is addressed by the sprites packed into it,
	// and after a rebuild everything still packed sits in the earliest pages that could hold it.
	// This is where the memory actually goes back -- one page is a full atlas texture.
	int32 DroppedCount = 0;
	for (int32 i = AtlasBinPackArray.Num() - 1; i >= 1; i--)
	{
		if (!AtlasBinPackArray[i].IsEmpty())break;
		AtlasBinPackArray.RemoveAt(i);
		if (AtlasTextureArray.IsValidIndex(i))
		{
			AtlasTextureArray.RemoveAt(i);
		}
		DroppedCount++;
	}
	return DroppedCount;
}

void FDreamUIDynamicSpriteAtlasData::NotifyRenderSpritesAtlasChanged()
{
	//a rebuild moves every surviving sprite's rectangle, so everything drawing from this atlas has
	//stale UVs until it is told -- the same notification the static atlas sends after a repack
	for (int32 i = RenderSpriteArray.Num() - 1; i >= 0; i--)
	{
		auto& RenderSprite = RenderSpriteArray[i];
		if (!RenderSprite.IsValid())continue;
		IDreamUISpriteRenderInterface::Execute_ApplyAtlasTextureChange(RenderSprite.Get());
	}
}

bool FDreamUIDynamicSpriteAtlasData::PackSprite(UDreamUISpriteData* Sprite)
{
	if (TryPackSpriteIntoExistingPages(Sprite))
	{
		return true;
	}
	// Every page is full. RECYCLE BEFORE GROWING: the rectangles held by sprites nothing draws any
	// more are dead area, and adding a page instead of reclaiming them is exactly how a long session
	// climbs in video memory and never comes back down.
	if (EvictUnusedSprites(GetInsertAreaForSprite(Sprite)) > 0)
	{
		if (TryPackSpriteIntoExistingPages(Sprite))
		{
			return true;
		}
	}
	//nothing left to reclaim, so the atlas genuinely needs more room
	this->ExpandAtlasAreaArray();
	return TryPackSpriteIntoExistingPages(Sprite);
}

bool FDreamUIDynamicSpriteAtlasData::TryPackSpriteIntoExistingPages(UDreamUISpriteData* Sprite)
{
	if (!IsValid(Sprite) || !IsValid(Sprite->GetSpriteTexture()))return false;
	if (AtlasBinPackArray.Num() == 0 || AtlasTextureArray.Num() == 0)return false;
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
			//most recently used, which is what decides who is evicted first when the pages fill up
			this->TouchSprite(Sprite);
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
			AtlasMapKeyValue.Value.ReleaseAtlasTextures();
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
		if (auto AtlasData = Instance->AtlasMap.Find(InPackingTag))
		{
			//the sprites that were packed into it have to be told, or they keep pointing at a page nothing rebuilds
			AtlasData->ReleaseAtlasTextures();
		}
		Instance->AtlasMap.Remove(InPackingTag);
		if (Instance->OnAtlasMapChanged.IsBound())
		{
			Instance->OnAtlasMapChanged.Broadcast();
		}
	}
}

bool UDreamUIDynamicSpriteAtlasManager::TrimAtlasByPackingTag(FName InPackingTag)
{
	if (Instance == nullptr)return false;
	auto AtlasData = Instance->AtlasMap.Find(InPackingTag);
	if (AtlasData == nullptr)return false;
	//drop entries whose sprite or renderer is gone or has moved to another tag first: they are what usually keeps a page "in use"
	AtlasData->CheckSprite();
	if (AtlasData->RenderSpriteArray.Num() > 0)
	{
		//something on screen still draws from this atlas; releasing its pages now would blank those elements
		return false;
	}
	if (AtlasData->AtlasTextureArray.Num() == 0)
	{
		return false;
	}
	AtlasData->ReleaseAtlasTextures();
	if (Instance->OnAtlasMapChanged.IsBound())
	{
		Instance->OnAtlasMapChanged.Broadcast();
	}
	return true;
}

int32 UDreamUIDynamicSpriteAtlasManager::EvictUnusedSpritesByPackingTag(FName InPackingTag)
{
	if (Instance == nullptr)return 0;
	auto AtlasData = Instance->AtlasMap.Find(InPackingTag);
	if (AtlasData == nullptr)return 0;
	const int32 EvictedCount = AtlasData->EvictUnusedSprites();
	if (EvictedCount > 0 && Instance->OnAtlasMapChanged.IsBound())
	{
		Instance->OnAtlasMapChanged.Broadcast();
	}
	return EvictedCount;
}

int32 UDreamUIDynamicSpriteAtlasManager::TrimUnusedAtlases()
{
	if (Instance == nullptr)return 0;
	TArray<FName> PackingTags;
	Instance->AtlasMap.GetKeys(PackingTags);
	int32 TrimmedCount = 0;
	for (const auto& PackingTag : PackingTags)
	{
		if (TrimAtlasByPackingTag(PackingTag))
		{
			TrimmedCount++;
		}
	}
	return TrimmedCount;
}
