// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIStaticSpriteAtlasData.h"
#include "LGUI.h"
#include "Core/LexUISpriteData.h"
#include "TextureCompiler.h"
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "Core/ILexUISpriteRenderInterface.h"
#include "TextureResource.h"
#include "Core/LexUIManager.h"

#define LOCTEXT_NAMESPACE "LexUIStaticSpriteAtlasData"

#if WITH_EDITOR
void ULexUIStaticSpriteAtlasData::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIStaticSpriteAtlasData, SpriteDataArray))
	{
		PrevSpriteDataArray = SpriteDataArray;
	}
}
void ULexUIStaticSpriteAtlasData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		MarkNotInitialized();
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIStaticSpriteAtlasData, SpriteDataArray))
		{
			//not allow empty
			PrevSpriteDataArray.Remove(nullptr);
			SpriteDataArray.Remove(nullptr);
			//not allow repeated
			TSet<ULexUISpriteData*> tempSet;
			for (int i = 0; i < PrevSpriteDataArray.Num(); i++)
			{
				auto spriteItem = PrevSpriteDataArray[i];
				if (tempSet.Contains(spriteItem))
				{
					PrevSpriteDataArray.RemoveAt(i);
					i--;
				}
				else
				{
					tempSet.Add(spriteItem);
				}
			}
			tempSet.Empty();
			for (int i = 0; i < SpriteDataArray.Num(); i++)
			{
				auto spriteItem = SpriteDataArray[i];
				if (tempSet.Contains(spriteItem))
				{
					SpriteDataArray.RemoveAt(i);
					i--;
				}
				else
				{
					tempSet.Add(spriteItem);
				}
			}

			TArray<ULexUISpriteData*> AddedArray;
			TArray<ULexUISpriteData*> RemovedArray;
			for (auto Item : SpriteDataArray)
			{
				if (!PrevSpriteDataArray.Contains(Item))
				{
					AddedArray.Add(Item);
				}
			}
			for (auto Item : PrevSpriteDataArray)
			{
				if (!SpriteDataArray.Contains(Item))
				{
					RemovedArray.Add(Item);
				}
			}

			auto TransferSprite = [this](ULexUISpriteData* spriteData) {
				spriteData->Modify();
				if (IsValid(spriteData->PackingAtlas))
				{
					spriteData->PackingAtlas->RemoveSpriteData(spriteData);
				}
				spriteData->PackingAtlas = this;
				spriteData->bIsInitialized = false;
				spriteData->MarkPackageDirty();
			};
			auto KeepOldSprite = [this](ULexUISpriteData* spriteData) {
				SpriteDataArray.Remove(spriteData);
			};
			for (auto Item : AddedArray)
			{
				if (Item->PackingAtlas == nullptr)
				{
					TransferSprite(Item);
				}
				else
				{
					if (bIsYesToAll || bIsNoToAll)
					{
						if (bIsYesToAll)
						{
							TransferSprite(Item);
						}
						if (bIsNoToAll)
						{
							KeepOldSprite(Item);
						}
					}
					else
					{
						auto WarningMsg = FText::Format(LOCTEXT("TransferSpriteWarning", "Sprite: '{0}' was belongs to atlas: '{1}', do you want to transfer the Sprite to this atlas?")
							, FText::FromString(Item->GetPathName()), FText::FromString(Item->PackingAtlas->GetPathName()));
						auto Result = FMessageDialog::Open(EAppMsgType::YesNoYesAllNoAll, WarningMsg);
						switch (Result)
						{
						case EAppReturnType::No:
							KeepOldSprite(Item);
							break;
						case EAppReturnType::Yes:
							TransferSprite(Item);
							break;
						case EAppReturnType::YesAll:
							bIsYesToAll = true;
							TransferSprite(Item);
							break;
						case EAppReturnType::NoAll:
							bIsNoToAll = true;
							KeepOldSprite(Item);
							break;
						}
						auto WeakThis = TWeakObjectPtr<ULexUIStaticSpriteAtlasData>(this);
						ULexUIEditorManagerObject::AddOneShotTickFunction([=] {
							if (WeakThis.IsValid())
							{
								WeakThis->bIsYesToAll = false;
								WeakThis->bIsNoToAll = false;
							}
							}, 0);
					}
				}
			}

			for (auto Item : RemovedArray)
			{
				Item->Modify();
				Item->PackingAtlas = nullptr;
				Item->bIsInitialized = false;
				Item->MarkPackageDirty();
			}

			//If we drag sprites to the spriteArray, the PostEditChangeProperty will be called foreach of the dragged sprites which is a long time wait, so we do the pack after the iteration.
			if (!bIsAddedToDelayedCall)
			{
				bIsAddedToDelayedCall = true;
				auto WeakThis = TWeakObjectPtr<ULexUIStaticSpriteAtlasData>(this);
				ULexUIEditorManagerObject::AddOneShotTickFunction([=] {
					if (WeakThis.IsValid())
					{
						WeakThis->MarkNotInitialized();
						WeakThis->InitCheck();
						WeakThis->MarkPackageDirty();
						WeakThis->bIsAddedToDelayedCall = false;
					}
					}, 0);
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIStaticSpriteAtlasData, MaxAtlasTextureSize))
		{
			MaxAtlasTextureSize = FMath::RoundUpToPowerOfTwo(MaxAtlasTextureSize);
			MaxAtlasTextureSize = FMath::Clamp(MaxAtlasTextureSize, (uint32)256, (uint32)8192);
		}
	}
}
void ULexUIStaticSpriteAtlasData::AddSpriteData(ULexUISpriteData* InSpriteData)
{
	if (!SpriteDataArray.Contains(InSpriteData))
	{
		SpriteDataArray.Add(InSpriteData);
		MarkPackageDirty();
		MarkNotInitialized();
	}
}
void ULexUIStaticSpriteAtlasData::RemoveSpriteData(ULexUISpriteData* InSpriteData)
{
	if (SpriteDataArray.Contains(InSpriteData))
	{
		SpriteDataArray.Remove(InSpriteData);
		MarkPackageDirty();
		MarkNotInitialized();
	}
}
void ULexUIStaticSpriteAtlasData::AddRenderSprite(TScriptInterface<ILexUISpriteRenderInterface> InSprite)
{
	RenderSpriteArray.AddUnique(InSprite.GetObject());
}
void ULexUIStaticSpriteAtlasData::RemoveRenderSprite(TScriptInterface<ILexUISpriteRenderInterface> InSprite)
{
	RenderSpriteArray.Remove(InSprite.GetObject());
}
void ULexUIStaticSpriteAtlasData::CheckSprite()
{
	for (int i = this->SpriteDataArray.Num() - 1; i >= 0; i--)
	{
		auto itemSprite = this->SpriteDataArray[i];
		if (IsValid(itemSprite))
		{
			if (itemSprite->GetPackingAtlas() != this)
			{
				this->SpriteDataArray.RemoveAt(i);
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
					if (spriteData->GetPackingAtlas() != this)
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
bool ULexUIStaticSpriteAtlasData::PackAtlas()
{
	AtlasTexture = nullptr;

	if (SpriteDataArray.Num() <= 0)return false;
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		ULexUISpriteData* spriteDataItem = SpriteDataArray[i];
		if (!IsValid(spriteDataItem))
		{
			if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
			{
				bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
				auto ErrMsg = FText::Format(LOCTEXT("SpriteDataError", "{0} Packing atlas for LGUIStaticSpriteAtlasData: '{1}', but SpriteData is not valid in spriteArray at index {2}")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
					, FText::FromString(this->GetPathName()), i);
				UE_LOG(LGUI, Error, TEXT("%s"), *ErrMsg.ToString());
				FLexUIUtils::EditorNotification(ErrMsg, false, 10.0f);
			}
			return false;
		}
		if (!IsValid(spriteDataItem->GetSpriteTexture()))
		{
			if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
			{
				bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
				auto ErrMsg = FText::Format(LOCTEXT("SpriteDataTextureError", "{0} Packing atlas for LGUIStaticSpriteAtlasData: '{1}', but SpriteData's texture is not valid of spriteData: '{2}'")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
					, FText::FromString(this->GetPathName()), FText::FromString(spriteDataItem->GetPathName()));
				UE_LOG(LGUI, Error, TEXT("%s"), *ErrMsg.ToString());
				FLexUIUtils::EditorNotification(ErrMsg, false, 10.0f);
			}
			return false;
		}
		if (spriteDataItem->PackingAtlas != this)
		{
			if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
			{
				bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
				auto ErrMsg = FText::Format(LOCTEXT("SpritePackingAtlasError", "{0} Packing atlas for LexUIStaticSpriteAtlasData: '{1}', but SpriteData's packingAtlas is not this one, spriteData '{2}', at index: {3}")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
					, FText::FromString(this->GetPathName()), FText::FromString(spriteDataItem->GetPathName()), i);
				UE_LOG(LGUI, Error, TEXT("%s"), *ErrMsg.ToString());
				FLexUIUtils::EditorNotification(ErrMsg, false, 10.0f);
			}
			return false;
		}
	}

	//pack
	uint32 packSize = 16;//start from minimal size 16
	TArray<rbp::Rect> packResult;
	packResult.SetNumUninitialized(SpriteDataArray.Num());
	while (!PackAtlasTest(packSize, packResult))
	{
		packSize *= 2;
	}
	if (packSize > MaxAtlasTextureSize)
	{
		if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
		{
			bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
			auto ErrMsg = FText::Format(LOCTEXT("AtlasSizeTooLargeError", "{0} Package Sprite atlas fail! Atlas texture size {1} larger than {2}: {3}! Please remove some large size Sprite, or split to multiple atlas.")
				, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
				, packSize
				, FText::FromName(GET_MEMBER_NAME_CHECKED(ULexUIStaticSpriteAtlasData, MaxAtlasTextureSize))
				, MaxAtlasTextureSize);
			UE_LOG(LGUI, Error, TEXT("%s"), *ErrMsg.ToString());
			FLexUIUtils::EditorNotification(ErrMsg, false, 10.0f);
		}
		return false;
	}

	//create texture
	auto PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = packSize;
	PlatformData->SizeY = packSize;
	PlatformData->PixelFormat = PF_B8G8R8A8;

	int32 atlasSize = packSize;
	auto pixelBufferLength = atlasSize * atlasSize * GPixelFormats[PF_B8G8R8A8].BlockBytes;
	uint8* pixelData = new uint8[pixelBufferLength];
	FMemory::Memset(pixelData, 0, pixelBufferLength);//default is transparent black
	//copy pixels
	FColor* atlasColorBuffer = static_cast<FColor*>((void*)pixelData);
	float atlasTextureSizeInv = 1.0f / atlasSize;
	for (int spriteIndex = 0; spriteIndex < SpriteDataArray.Num(); spriteIndex++)
	{
		auto spriteDataItem = SpriteDataArray[spriteIndex];
		if (IsValid(spriteDataItem) && IsValid(spriteDataItem->GetSpriteTexture()))
		{
			auto spriteTexture = spriteDataItem->GetSpriteTexture();
			ULexUISpriteData::CheckAndApplySpriteTextureSetting(spriteTexture);
#if WITH_EDITOR
			FTextureCompilingManager::Get().FinishCompilation({ spriteTexture });
#endif
			int32 spriteWidth = spriteTexture->GetSizeX();
			int32 spriteHeight = spriteTexture->GetSizeY();
			const FColor* spriteColorBuffer = static_cast<const FColor*>(spriteTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
			rbp::Rect rect = packResult[spriteIndex];

			int destY = rect.y * atlasSize;
			int spritePixelIndex = 0;
			for (int32 texY = 0; texY < spriteHeight; texY++)
			{
				int destX = rect.x + destY;
				for (int32 texX = 0; texX < spriteWidth; texX++)
				{
					int dstPixelIndex = destX + texX;
					atlasColorBuffer[dstPixelIndex] = spriteColorBuffer[spritePixelIndex];
					spritePixelIndex++;
				}
				destY += atlasSize;
			}
			//pixel padding
			if (spriteDataItem->GetUseEdgePixelPadding() && EdgePixelPadding > 0)
			{
				//left
				destY = rect.y * atlasSize;
				for (int paddingIndex = 0; paddingIndex < EdgePixelPadding; paddingIndex++)
				{
					int destX = destY + rect.x - paddingIndex - 1;
					int dstPixelIndex = destX;
					for (int heightIndex = 0; heightIndex < spriteHeight; heightIndex++)
					{
						atlasColorBuffer[dstPixelIndex] = atlasColorBuffer[dstPixelIndex + 1];
						dstPixelIndex += atlasSize;
					}
				}
				//right
				destY = rect.y * atlasSize;
				for (int paddingIndex = 0; paddingIndex < EdgePixelPadding; paddingIndex++)
				{
					int destX = destY + rect.x + rect.width + paddingIndex;
					int dstPixelIndex = destX;
					for (int heightIndex = 0; heightIndex < spriteHeight; heightIndex++)
					{
						atlasColorBuffer[dstPixelIndex] = atlasColorBuffer[dstPixelIndex - 1];
						dstPixelIndex += atlasSize;
					}
				}
				//top, with corner
				destY = (rect.y - 1) * atlasSize;
				for (int paddingIndex = 0; paddingIndex < EdgePixelPadding; paddingIndex++)
				{
					int destX = destY + rect.x;
					int dstPixelIndex = destX - EdgePixelPadding;
					for (int widthIndex = -EdgePixelPadding; widthIndex < spriteWidth + EdgePixelPadding; widthIndex++)
					{
						atlasColorBuffer[dstPixelIndex] = atlasColorBuffer[dstPixelIndex + atlasSize];
						dstPixelIndex += 1;
					}
					destY -= atlasSize;
				}
				//bottom, with corner
				destY = (rect.y + rect.height) * atlasSize;
				for (int paddingIndex = 0; paddingIndex < EdgePixelPadding; paddingIndex++)
				{
					int destX = destY + rect.x;
					int dstPixelIndex = destX - EdgePixelPadding;
					for (int widthIndex = -EdgePixelPadding; widthIndex < spriteWidth + EdgePixelPadding; widthIndex++)
					{
						atlasColorBuffer[dstPixelIndex] = atlasColorBuffer[dstPixelIndex - atlasSize];
						dstPixelIndex += 1;
					}
					destY += atlasSize;
				}
			}

			spriteDataItem->ApplySpriteInfoAfterStaticPack(rect, atlasTextureSizeInv);
			spriteTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
		}
	}

	//store data
	TextureMipData.SetNumUninitialized(pixelBufferLength);
	FMemory::Memcpy(TextureMipData.GetData(), pixelData, pixelBufferLength);
	TextureSize = packSize;

	//generate mipmaps
	{
		int mipsAdd = 0;
		//Declaring buffers here to reduce reallocs
		//We double buffer mips, using the prior buffer to build the next buffer
		TArray<FColor> mipRGBAs1;
		TArray<FColor> mipRGBAs2;

		//Access source data
		auto priorData = reinterpret_cast<const FColor*>(pixelData);
		int mipSize = atlasSize;

		while (true)
		{
			auto* mipRGBAs = mipsAdd & 1 ? &mipRGBAs1 : &mipRGBAs2;
			auto srcWidth = mipSize;
			mipSize = mipSize >> 1;
			if (mipSize == 0)
			{
				break;
			}

			mipRGBAs->Reset();
			mipRGBAs->AddUninitialized(mipSize* mipSize);

			//Average out the values
			auto* dataOut = mipRGBAs->GetData();
			for (int y = 0; y < mipSize; y++)
			{
				auto* srcData0 = priorData + (srcWidth * y * 2);
				auto* srcData1 = srcData0 + srcWidth;
				for (int x = 0; x < mipSize; x++)
				{
					auto srcColor1 = *srcData0++;
					auto srcColor2 = *srcData0++;
					auto srcColor3 = *srcData1++;
					auto srcColor4 = *srcData1++;
					int totalR = srcColor1.R;
					int totalG = srcColor1.G;
					int totalB = srcColor1.B;
					int totalA = srcColor1.A;

					totalR += srcColor2.R;
					totalG += srcColor2.G;
					totalB += srcColor2.B;
					totalA += srcColor2.A;

					totalR += srcColor3.R;
					totalG += srcColor3.G;
					totalB += srcColor3.B;
					totalA += srcColor3.A;

					totalR += srcColor4.R;
					totalG += srcColor4.G;
					totalB += srcColor4.B;
					totalA += srcColor4.A;

					totalR >>= 2;
					totalG >>= 2;
					totalB >>= 2;
					totalA >>= 2;

					*dataOut = FColor((uint8)totalR, (uint8)totalG, (uint8)totalB, (uint8)totalA);
					dataOut++;
				}
			}

			auto mipBufferLength = mipRGBAs->Num() * GPixelFormats[PF_B8G8R8A8].BlockBytes;
			priorData = mipRGBAs->GetData();
			mipsAdd++;

			//store mip data
			auto prevLength = TextureMipData.Num();
			TextureMipData.AddUninitialized(mipBufferLength);
			FMemory::Memcpy(TextureMipData.GetData() + prevLength, mipRGBAs->GetData(), mipBufferLength);
		}
	}

	delete[] pixelData;

	return true;
}
bool ULexUIStaticSpriteAtlasData::PackAtlasTest(uint32 size, TArray<rbp::Rect>& result)
{
	rbp::MaxRectsBinPack atlasBinPack;
	atlasBinPack.Init(size, size, false);
	auto methold = rbp::MaxRectsBinPack::FreeRectChoiceHeuristic::RectBestAreaFit;
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		auto spriteDataItem = SpriteDataArray[i];
		auto calculatedEdgePixelPadding = spriteDataItem->GetUseEdgePixelPadding() ? EdgePixelPadding : 0;
		auto spriteTexture = spriteDataItem->GetSpriteTexture();
		auto space = SpaceBetweenSprites + calculatedEdgePixelPadding + calculatedEdgePixelPadding;
		//add space
#if WITH_EDITOR
		FTextureCompilingManager::Get().FinishCompilation({ spriteTexture });
#endif
		int insertRectWidth = spriteTexture->GetSizeX() + space;
		int insertRectHeight = spriteTexture->GetSizeY() + space;
		auto rect = atlasBinPack.Insert(insertRectWidth, insertRectHeight, methold);
		if (rect.width <= 0)//cannot fit, should expend size
		{
			return false;
		}
		//remove space
		rect.x += calculatedEdgePixelPadding;
		rect.y += calculatedEdgePixelPadding;
		rect.width -= space;
		rect.height -= space;

		result[i] = rect;
	}
	return true;
}
void ULexUIStaticSpriteAtlasData::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	
}
void ULexUIStaticSpriteAtlasData::WillNeverCacheCookedPlatformDataAgain()
{
	
}
void ULexUIStaticSpriteAtlasData::ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	
}
void ULexUIStaticSpriteAtlasData::MarkNotInitialized()
{
	bIsInitialized = false;
	bWarningIsAlreadyAppearedAtCurrentPackingSession = false;
}
bool ULexUIStaticSpriteAtlasData::CheckInvalidSpriteData()const
{
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		ULexUISpriteData* spriteDataItem = SpriteDataArray[i];
		if (!IsValid(spriteDataItem))
		{
			return true;
		}
		else if (!IsValid(spriteDataItem->GetSpriteTexture()))
		{
			return true;
		}
		else if (spriteDataItem->PackingAtlas != this)
		{
			return true;
		}
	}
	return false;
}
void ULexUIStaticSpriteAtlasData::CleanupInvalidSpriteData()
{
	auto PrevCount = SpriteDataArray.Num();
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		ULexUISpriteData* spriteDataItem = SpriteDataArray[i];
		if (!IsValid(spriteDataItem))
		{
			SpriteDataArray.RemoveAt(i);
			i--;
		}
		else if (!IsValid(spriteDataItem->GetSpriteTexture()))
		{
			SpriteDataArray.RemoveAt(i);
			i--;
		}
		else if (spriteDataItem->PackingAtlas != this)
		{
			SpriteDataArray.RemoveAt(i);
			i--;
		}
	}
	if (PrevCount != SpriteDataArray.Num())
	{
		this->MarkNotInitialized();
		this->InitCheck();
		this->MarkPackageDirty();
	}
}
#endif

void ULexUIStaticSpriteAtlasData::BeginDestroy()
{
#if WITH_EDITOR
	for (auto& item : SpriteDataArray)
	{
		item->bIsInitialized = false;
	}
#endif
	Super::BeginDestroy();
}

bool ULexUIStaticSpriteAtlasData::InitCheck()
{
	if (!bIsInitialized)
	{
#if WITH_EDITOR
		if (!PackAtlas())
		{
			return false;
		}
#endif
		bIsInitialized = true;

		static int TextureNameSuffix = 0;
		//create texture
		auto texture = NewObject<UTexture2D>(
			GetTransientPackage(),
			FName(*FString::Printf(TEXT("LexUIStaticSpriteAtlasData_Texture_%d"), TextureNameSuffix++)),
			EObjectFlags::RF_Transient
		);
		auto PlatformData = new FTexturePlatformData();
		PlatformData->SizeX = TextureSize;
		PlatformData->SizeY = TextureSize;
		PlatformData->PixelFormat = PF_B8G8R8A8;
		texture->SetPlatformData(PlatformData);

		//mipmaps
		{
			uint32 textureDataOffset = 0;
			int mipSize = TextureSize;
			while (true)
			{
				// Allocate next mipmap.
				auto mip = new FTexture2DMipMap;
				texture->GetPlatformData()->Mips.Add(mip);
				mip->SizeX = mipSize;
				mip->SizeY = mipSize;
				mip->BulkData.Lock(LOCK_READ_WRITE);
				auto pixelBufferLength = mipSize * mipSize * GPixelFormats[PF_B8G8R8A8].BlockBytes;
				void* mipData = mip->BulkData.Realloc(pixelBufferLength);
				FMemory::Memcpy(mipData, TextureMipData.GetData() + textureDataOffset, pixelBufferLength);
				mip->BulkData.Unlock();

				mipSize = mipSize >> 1;
				if (mipSize == 0)
				{
					break;
				}
				textureDataOffset += pixelBufferLength;
			}
		}

#if !WITH_EDITOR
		//empty it to reduce memory usage
		TextureMipData.Empty();
#endif

		texture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
		texture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
		texture->SRGB = AtlasTextureUseSRGB;
		texture->Filter = AtlasTextureFilter;
		texture->UpdateResource();

		this->AtlasTexture = texture;
#if WITH_EDITOR
		for (auto& sprite : RenderSpriteArray)
		{
			if (sprite.IsValid())
			{
				ILexUISpriteRenderInterface::Execute_ApplyAtlasTextureChange(sprite.Get());
			}
		}
#endif
	}
	return bIsInitialized;
}
UTexture2D* ULexUIStaticSpriteAtlasData::GetAtlasTexture()
{
	InitCheck();
	return AtlasTexture;
}
bool ULexUIStaticSpriteAtlasData::ReadPixel(const FVector2D& InUV, FColor& OutPixel)
{
	InitCheck();

	auto PlatformData = AtlasTexture->GetPlatformData();
	if (PlatformData && PlatformData->Mips.Num() > 0)
	{
		auto Pixels = static_cast<FColor*>(PlatformData->Mips[0].BulkData.Lock(LOCK_READ_ONLY));
		auto uvInFullSize = FIntPoint(InUV.X * TextureSize, InUV.Y * TextureSize);
		auto PixelIndex = uvInFullSize.Y * TextureSize + uvInFullSize.X;
		OutPixel = Pixels[PixelIndex];
		PlatformData->Mips[0].BulkData.Unlock();
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
