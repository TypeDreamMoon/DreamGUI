// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUISpriteData.h"
#include "LGUI.h"
#include "Core/LGUISettings.h"
#include "Core/Components/UISpriteBase.h"
#include "Core/LexUIDynamicSpriteAtlasData.h"
#include "Core/LexUIStaticSpriteAtlasData.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUIManager.h"
#include "RHI.h"
#include "Rendering/Texture2DResource.h"
#include "TextureCompiler.h"
#include "Utils/LexUIUtils.h"
#include "RenderingThread.h"

#define LOCTEXT_NAMESPACE "LGUISpriteData"

void FLexUISpriteInfo::ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal)
{
	width = InWidth;
	height = InHeight;

	uv0X = InX * texFullWidthReciprocal;
	uv0Y = (InY + InHeight) * texFullHeightReciprocal;
	uv3X = (InX + InWidth) * texFullWidthReciprocal;
	uv3Y = InY * texFullHeightReciprocal;
}
void FLexUISpriteInfo::ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal, const FVector4& uvRect)
{
	width = InWidth;
	height = InHeight;

	uv0X = InX * texFullWidthReciprocal + uvRect.X;
	uv0Y = (InY + InHeight) * texFullHeightReciprocal * uvRect.W + uvRect.Y;
	uv3X = (InX + InWidth) * texFullWidthReciprocal * uvRect.Z + uvRect.X;
	uv3Y = InY * texFullHeightReciprocal + uvRect.Y;
}
bool FLexUISpriteInfo::HasBorder()const
{
	return borderLeft != 0 || borderRight != 0 || borderTop != 0 || borderBottom != 0;
}
bool FLexUISpriteInfo::HasPadding()const
{
	return paddingLeft != 0 || paddingRight != 0 || paddingTop != 0 || paddingBottom != 0;
}
void FLexUISpriteInfo::ApplyBorderUV(float texFullWidthReciprocal, float texFullHeightReciprocal)
{
	buv0X = uv0X + borderLeft * texFullWidthReciprocal;
	buv3X = uv3X - borderRight * texFullWidthReciprocal;
	buv0Y = uv0Y - borderBottom * texFullHeightReciprocal;
	buv3Y = uv3Y + borderTop * texFullHeightReciprocal;
}

bool ULexUISpriteData::InsertTexture(FLexUIDynamicSpriteAtlasData* InAtlasData)
{
#if WITH_EDITOR
	int32 spaceBetweenSprites = ULGUISettings::GetAtlasTexturePadding(PackingTag);
#else
	static int32 spaceBetweenSprites = ULGUISettings::GetAtlasTexturePadding(PackingTag);
#endif
	auto SizeX = InAtlasData->AtlasTexture->GetSizeX();
	check(SizeX != 0);
	float atlasTextureSizeInv = 1.0f / SizeX;

	auto method = rbp::MaxRectsBinPack::RectBestAreaFit;
#if WITH_EDITOR
	FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
	int insertRectWidth = SpriteTexture->GetSizeX() + spaceBetweenSprites + spaceBetweenSprites;
	int insertRectHeight = SpriteTexture->GetSizeY() + spaceBetweenSprites + spaceBetweenSprites;

	auto packedRect = InAtlasData->AtlasBinPack.Insert(insertRectWidth, insertRectHeight, method);
	if (packedRect.height <= 0)//means this area cannot fit the texture
	{
		return false;
	}
	else//this area can fit the texture, copy pixels
	{
		AtlasTexture = InAtlasData->AtlasTexture;
		//remove space
		packedRect.x += spaceBetweenSprites;
		packedRect.y += spaceBetweenSprites;
		packedRect.width -= spaceBetweenSprites + spaceBetweenSprites;
		packedRect.height -= spaceBetweenSprites + spaceBetweenSprites;
		//pixels
		CopySpriteTextureToAtlas(packedRect, spaceBetweenSprites);
		//add to Sprite
		SpriteInfo.ApplyUV(packedRect.x, packedRect.y, packedRect.width, packedRect.height, atlasTextureSizeInv, atlasTextureSizeInv);
		SpriteInfo.ApplyBorderUV(atlasTextureSizeInv, atlasTextureSizeInv);
		InAtlasData->SpriteDataArray.Add(this);
		return true;
	}
}
void ULexUISpriteData::CopySpriteTextureToAtlas(rbp::Rect InPackedRect, int32 InAtlasTexturePadding)
{
	if (SpriteTexture->GetResource() != nullptr && AtlasTexture->GetResource() != nullptr)
	{
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
		FUpdateTextureRegionsData* RegionData = new FUpdateTextureRegionsData;
		RegionData->SpriteTextureResource = (FTexture2DResource*)SpriteTexture->GetResource();
		RegionData->AtlasTextureResource = (FTexture2DResource*)AtlasTexture->GetResource();
		RegionData->SrcRegionBox = srcRegionBox;
		RegionData->DstRegionBox = dstRegionBox;
		RegionData->SpaceBetweenSprites = bUseEdgePixelPadding ? InAtlasTexturePadding : 0;
		RegionData->PackedRect = InPackedRect;

		ENQUEUE_RENDER_COMMAND(FLGUISpriteCopyTextureData)(
			[RegionData](FRHICommandListImmediate& RHICmdList)
		{
			auto spriteTextureRHIRef = RegionData->SpriteTextureResource->GetTexture2DRHI();
			auto atlasTextureRHIRef = RegionData->AtlasTextureResource->GetTexture2DRHI();
			auto srcRegionPosition = RegionData->SrcRegionBox.Min;
			auto srcRegionSize = RegionData->SrcRegionBox.GetSize();
			auto dstRegionPosition = RegionData->DstRegionBox.Min;
			auto packedRect = RegionData->PackedRect;
			auto spaceBetweenSprites = RegionData->SpaceBetweenSprites;
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
			delete RegionData;
		});
	}
}

bool ULexUISpriteData::PackageSprite()
{
	CheckAndApplySpriteTextureSetting(SpriteTexture);

	auto atlasData = ULexUIDynamicSpriteAtlasManager::FindOrAdd(PackingTag);
	atlasData->EnsureAtlasTexture(PackingTag);
	AtlasTexture = atlasData->AtlasTexture;
PACK_AND_INSERT:
	if (InsertTexture(atlasData))
	{
		return true;
	}
	else//all area cannot fit the texture, then expend texture size
	{
		int32 newTextureSize = atlasData->GetWillExpendTextureSize();
		UE_LOG(LGUI, Log, TEXT("[%s].%d Insert texture:%s expend size to %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(SpriteTexture->GetPathName()), newTextureSize);
		if (newTextureSize > WARNING_ATLAS_SIZE)
		{
			auto warningMsg = FText::Format(LOCTEXT("PackageSprite_AtlasSize_Warning", "{0} Trying to insert texture:{1}, result to expend size to:{2} larger than the preferred maximun texture size:{3}!\
\nTry reduce some Sprite texture size, or use UITexture to render some large texture, or use different PackingTag to split your AtlasTexture.\
\nAlso remember to dispose unused atlas by call function DisposeAtlasByPackingTag from LGUIDynamicSpriteAtlasManager.\
")
				, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
				, FText::FromString(SpriteTexture->GetPathName())
				, newTextureSize, WARNING_ATLAS_SIZE);
			UE_LOG(LGUI, Warning, TEXT("%s"), *warningMsg.ToString());
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(warningMsg);
#endif
		}
		if ((uint32)newTextureSize > GetMax2DTextureDimension())
		{
			auto warningMsg = FText::Format(LOCTEXT("PackageSprite_AtlasSize_Error", "{0} Trying to insert texture:{1}, result too large size that not supported! Maximun texture size is:{2}.")
				, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
				, FText::FromString(SpriteTexture->GetPathName()), GetMax2DTextureDimension());
			UE_LOG(LGUI, Error, TEXT("%s"), *warningMsg.ToString());
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(warningMsg);
#endif
			return false;
		}
		atlasData->ExpendTextureSize(PackingTag);
		goto PACK_AND_INSERT;
	}
}

void ULexUISpriteData::CheckSpriteTexture()
{
	if (SpriteTexture == nullptr)
	{
		SpriteTexture = LoadObject<UTexture2D>(NULL, TEXT("/LGUI/Textures/LGUIPreset_WhiteSolid"));
	}
}

void ULexUISpriteData::ApplySpriteInfoAfterStaticPack(const rbp::Rect& InPackedRect, float InAtlasTextureSizeInv)
{
	SpriteInfo.ApplyUV(InPackedRect.x, InPackedRect.y, InPackedRect.width, InPackedRect.height, InAtlasTextureSizeInv, InAtlasTextureSizeInv);
	SpriteInfo.ApplyBorderUV(InAtlasTextureSizeInv, InAtlasTextureSizeInv);
	bIsInitialized = false;
}
#if WITH_EDITOR
void ULexUISpriteData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CheckSpriteTexture();
	if (PropertyChangedEvent.Property != nullptr)
	{
		auto propertyName = PropertyChangedEvent.Property->GetFName();
		if (propertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteTexture))
		{
			if (SpriteTexture != nullptr)
			{
				CheckAndApplySpriteTextureSetting(SpriteTexture);
#if WITH_EDITOR
				FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
				SpriteInfo.width = SpriteTexture->GetSizeX();
				SpriteInfo.height = SpriteTexture->GetSizeY();
			}
		}
		else if (
			propertyName == GET_MEMBER_NAME_CHECKED(FLexUISpriteInfo, borderLeft) ||
			propertyName == GET_MEMBER_NAME_CHECKED(FLexUISpriteInfo, borderRight) ||
			propertyName == GET_MEMBER_NAME_CHECKED(FLexUISpriteInfo, borderTop) ||
			propertyName == GET_MEMBER_NAME_CHECKED(FLexUISpriteInfo, borderBottom)
			)
		{
			//Sprite data, apply border
			if (SpriteTexture != nullptr)
			{
#if WITH_EDITOR
				FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
				SpriteInfo.width = SpriteTexture->GetSizeX();
				SpriteInfo.height = SpriteTexture->GetSizeY();
				if (bIsInitialized)
				{
					float atlasTextureSizeInv = 1.0f / GetAtlasTexture()->GetSizeX();
					SpriteInfo.ApplyBorderUV(atlasTextureSizeInv, atlasTextureSizeInv);
				}
			}
		}
		else if (propertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, PackingTag))
		{
			this->ReloadTexture();
		}
		else if (propertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteTexture))
		{
			this->ReloadTexture();
		}
		else if (propertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, bUseEdgePixelPadding))
		{
			this->ReloadTexture();
		}
		else if (propertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, packingAtlas))
		{
			this->ReloadTexture();
		}

		ULexUIManagerWorldSubsystem::RefreshAllUI();
	}
}
bool ULexUISpriteData::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULexUISpriteData, PackingTag))
		{
			return IsValid(packingAtlas);
		}
	}
	return Super::CanEditChange(InProperty);
}
void ULexUISpriteData::MarkAllSpritesNeedToReinitialize()
{
	ULexUIDynamicSpriteAtlasManager::ResetAtlasMap();
	for (TObjectIterator<ULexUISpriteData> SpriteItr; SpriteItr; ++SpriteItr)
	{
		SpriteItr->bIsInitialized = false;
	}
}
#endif

void ULexUISpriteData::CheckAndApplySpriteTextureSetting(UTexture2D* InSpriteTexture)
{
	if (
		InSpriteTexture->CompressionSettings != TextureCompressionSettings::TC_EditorIcon
		|| InSpriteTexture->LODGroup != TextureGroup::TEXTUREGROUP_UI
		|| InSpriteTexture->SRGB != true
		)
	{
		InSpriteTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
		InSpriteTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
		InSpriteTexture->SRGB = true;
		InSpriteTexture->UpdateResource();
		InSpriteTexture->MarkPackageDirty();
	}
}

void ULexUISpriteData::ReloadTexture()
{
	bIsInitialized = false;

#if WITH_EDITOR
	if (IsValid(packingAtlas))
	{
		packingAtlas->MarkNotInitialized();
	}
#endif

#if WITH_EDITOR
	FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
	AtlasTexture = SpriteTexture;
	auto SizeX = AtlasTexture->GetSizeX();
	auto SizeY = AtlasTexture->GetSizeY();
	check(SizeX != 0 && SizeY != 0);
	float atlasTextureWidthInv = 1.0f / SizeX;
	float atlasTextureHeightInv = 1.0f / SizeY;
	SpriteInfo.ApplyUV(0, 0, SizeX, SizeY, atlasTextureWidthInv, atlasTextureHeightInv);
	SpriteInfo.ApplyBorderUV(atlasTextureWidthInv, atlasTextureHeightInv);
}

void ULexUISpriteData::InitSpriteData()
{
	if (!bIsInitialized)
	{
		if (IsValid(packingAtlas))
		{
#if WITH_EDITOR
			//add it again as check if it exist in packingAtlas
			packingAtlas->AddSpriteData(this);
#endif
			if (packingAtlas->InitCheck())
			{
				AtlasTexture = packingAtlas->GetAtlasTexture();
				//no need to set spriteInfo because it is already set when do static pack
				return;
			}
			else
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d PackingAtlas:%s pack error, will fallback to use PackingTag!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(packingAtlas->GetPathName()));
			}
		}
		if (SpriteTexture == nullptr)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d SpriteData:%s SpriteTexture is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetPathName()));
			return;
		}
		if (!PackingTag.IsNone())//need to pack to atlas
		{
#if WITH_EDITOR
			FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
			if (PackageSprite())
			{
				bIsInitialized = true;
			}
			else
			{
				UE_LOG(LGUI, Warning, TEXT("[%s].%d PackageSprite fail. Will automatically clear PackingTag to make it valid."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
				PackingTag = NAME_None;
				this->MarkPackageDirty();
				bIsInitialized = false;
			}
		}
		else//no need to pack to atlas, so spriteTextire self is the atlas
		{
			AtlasTexture = SpriteTexture;
			auto SizeX = AtlasTexture->GetSizeX();
			auto SizeY = AtlasTexture->GetSizeY();
			check(SizeX != 0 && SizeY != 0);
			float atlasTextureWidthInv = 1.0f / SizeX;
			float atlasTextureHeightInv = 1.0f / SizeY;
			//spriteInfo.ApplyUV(0, 0, AtlasTexture->GetSizeX(), AtlasTexture->GetSizeY(), atlasTextureWidthInv, atlasTextureHeightInv);
			SpriteInfo.ApplyBorderUV(atlasTextureWidthInv, atlasTextureHeightInv);
			bIsInitialized = true;
		}
	}
}

UTexture2D* ULexUISpriteData::GetAtlasTexture()
{
	InitSpriteData();
	return AtlasTexture;
}
const FLexUISpriteInfo& ULexUISpriteData::GetSpriteInfo()
{
	InitSpriteData();
	return SpriteInfo;
}

bool ULexUISpriteData::IsIndividual()const
{
	return !IsValid(packingAtlas) && PackingTag.IsNone();
}

bool ULexUISpriteData::HavePackingTag()const
{
	return !PackingTag.IsNone();
}
const FName& ULexUISpriteData::GetPackingTag()const
{
	return PackingTag;
}

ULexUISpriteData* ULexUISpriteData::CreateLexUISpriteData(UObject* Outer, UTexture2D* inSpriteTexture, FVector2D inHorizontalBorder /* = FVector2D::ZeroVector */, FVector2D inVerticalBorder /* = FVector2D::ZeroVector */, FName inPackingTag /* = TEXT("Main") */)
{
	if (!IsValid(inSpriteTexture))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Input texture not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	// check size
	if (inSpriteTexture)
	{
		int32 atlasPadding = 0;
		auto lguiSetting = GetDefault<ULGUISettings>()->defaultAtlasSetting.spaceBetweenSprites;
		if (inSpriteTexture->GetSizeX() + atlasPadding * 2 > WARNING_ATLAS_SIZE || inSpriteTexture->GetSizeY() + atlasPadding * 2 > WARNING_ATLAS_SIZE)
		{
			auto warningMsg = FText::Format(LOCTEXT("CreateLGUISpriteData_Size_Warning", "{0} Target texture width or height is too large! Consider use UITexture to render this texture.")
				, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
			UE_LOG(LGUI, Warning, TEXT("%s"), *warningMsg.ToString());
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(warningMsg);
#endif
		}
		// Apply setting for Sprite creation
		//inSpriteTexture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
		CheckAndApplySpriteTextureSetting(inSpriteTexture);
	}

	ULexUISpriteData* result = NewObject<ULexUISpriteData>(IsValid(Outer) ? Outer : GetTransientPackage());
	if (inSpriteTexture)
	{
		result->SpriteTexture = inSpriteTexture;
		auto& spriteInfo = result->SpriteInfo;
		spriteInfo.width = inSpriteTexture->GetSizeX();
		spriteInfo.height = inSpriteTexture->GetSizeY();
		spriteInfo.borderLeft = (uint16)inHorizontalBorder.X;
		spriteInfo.borderRight = (uint16)inHorizontalBorder.Y;
		spriteInfo.borderTop = (uint16)inVerticalBorder.X;
		spriteInfo.borderBottom = (uint16)inVerticalBorder.Y;
	}
	return result;
}

void ULexUISpriteData::AddUISprite(TScriptInterface<class ILexUISpriteRenderInterface> InUISprite)
{
	if (IsValid(packingAtlas))
	{
		InitSpriteData();
#if WITH_EDITOR
		//packingAtlas only need to collect Sprite in editor
		packingAtlas->AddRenderSprite(InUISprite);
#endif
	}
	else if (!PackingTag.IsNone())
	{
		InitSpriteData();
		auto& spriteArray = ULexUIDynamicSpriteAtlasManager::FindOrAdd(PackingTag)->RenderSpriteArray;
		spriteArray.AddUnique(InUISprite.GetObject());
	}
}
void ULexUISpriteData::RemoveUISprite(TScriptInterface<class ILexUISpriteRenderInterface> InUISprite)
{
	if (IsValid(packingAtlas))
	{
#if WITH_EDITOR
		//packingAtlas only need to collect Sprite in editor
		packingAtlas->RemoveRenderSprite(InUISprite);
#endif
	}
	else if (!PackingTag.IsNone())
	{
		if (auto spriteData = ULexUIDynamicSpriteAtlasManager::Find(PackingTag))
		{
			spriteData->RenderSpriteArray.RemoveSingle(InUISprite.GetObject());
		}
	}
}
bool ULexUISpriteData::ReadPixel(const FVector2D& InUV, FColor& OutPixel)const
{
	if (packingAtlas != nullptr)
	{
		return packingAtlas->ReadPixel(InUV, OutPixel);
	}
	return false;
}
bool ULexUISpriteData::SupportReadPixel()const
{
	return packingAtlas != nullptr;
}

ULexUISpriteData* ULexUISpriteData::GetDefaultWhiteSolid()
{
	static auto defaultWhiteSolid = LoadObject<ULexUISpriteData>(NULL, TEXT("/LGUI/LGUIPreset_WhiteSolid"));
	if (defaultWhiteSolid == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default Sprite error! Missing some content of LGUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(LGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, 10);
#endif
		return nullptr;
	}
	return defaultWhiteSolid;
}
ULexUISpriteData* ULexUISpriteData::GetDefaultFrameRect()
{
	static auto defaultFrameRect = LoadObject<ULexUISpriteData>(NULL, TEXT("/LGUI/LGUIPreset_Rect_Sprite"));
	if (defaultFrameRect == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default sprite error! Missing some content of LexUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(LGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, 10);
#endif
		return nullptr;
	}
	return defaultFrameRect;
}


#undef LOCTEXT_NAMESPACE
