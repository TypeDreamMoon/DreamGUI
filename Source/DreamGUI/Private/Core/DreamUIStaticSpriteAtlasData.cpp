// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "DreamGUI.h"
#include "Core/DreamUISpriteData.h"
#include "TextureCompiler.h"
#include "Utils/DreamUIUtils.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "TextureResource.h"
#include "Core/DreamUIManager.h"

#define LOCTEXT_NAMESPACE "DreamUIStaticSpriteAtlasData"

#if WITH_EDITOR
void UDreamUIStaticSpriteAtlasData::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIStaticSpriteAtlasData, SpriteDataArray))
	{
		PrevSpriteDataArray = SpriteDataArray;
	}
}
void UDreamUIStaticSpriteAtlasData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		MarkNotInitialized();
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIStaticSpriteAtlasData, SpriteDataArray))
		{
			//not allow empty
			PrevSpriteDataArray.Remove(nullptr);
			SpriteDataArray.Remove(nullptr);
			//not allow repeated
			TSet<UDreamUISpriteData*> tempSet;
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

			TArray<UDreamUISpriteData*> AddedArray;
			TArray<UDreamUISpriteData*> RemovedArray;
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

			auto TransferSprite = [this](UDreamUISpriteData* spriteData) {
				spriteData->Modify();
				if (IsValid(spriteData->PackingAtlas))
				{
					spriteData->PackingAtlas->RemoveSpriteData(spriteData);
				}
				spriteData->PackingAtlas = this;
				spriteData->bIsInitialized = false;
				spriteData->MarkPackageDirty();
			};
			auto KeepOldSprite = [this](UDreamUISpriteData* spriteData) {
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
						auto WeakThis = TWeakObjectPtr<UDreamUIStaticSpriteAtlasData>(this);
						UDreamUIManagerObject::AddOneShotTickFunction([=] {
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
				auto WeakThis = TWeakObjectPtr<UDreamUIStaticSpriteAtlasData>(this);
				UDreamUIManagerObject::AddOneShotTickFunction([=] {
					if (WeakThis.IsValid())
					{
						WeakThis->MarkNotInitialized();
						WeakThis->InitCheck();
						WeakThis->bIsAddedToDelayedCall = false;
					}
					}, 0);
			}
		}
	}
}

bool UDreamUIStaticSpriteAtlasData::ContainsSpriteData(UDreamUISpriteData* InSpriteData)const
{
	return SpriteDataArray.Contains(InSpriteData);
}

void UDreamUIStaticSpriteAtlasData::AddSpriteData(UDreamUISpriteData* InSpriteData)
{
	SpriteDataArray.Add(InSpriteData);
	CheckSprite();
	MarkPackageDirty();
	MarkNotInitialized();
}
void UDreamUIStaticSpriteAtlasData::RemoveSpriteData(UDreamUISpriteData* InSpriteData)
{
	SpriteDataArray.Remove(InSpriteData);
	CheckSprite();
	MarkPackageDirty();
	MarkNotInitialized();
}
void UDreamUIStaticSpriteAtlasData::AddRenderSprite(TScriptInterface<IDreamUISpriteRenderInterface> InSprite)
{
	RenderSpriteArray.AddUnique(InSprite.GetObject());
}
void UDreamUIStaticSpriteAtlasData::RemoveRenderSprite(TScriptInterface<IDreamUISpriteRenderInterface> InSprite)
{
	RenderSpriteArray.Remove(InSprite.GetObject());
}

FString UDreamUIStaticSpriteAtlasData::GetCacheDataPath(const FString& InFileName) const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamUI"), TEXT("StaticAtlasData"), *InFileName);
}

void UDreamUIStaticSpriteAtlasData::CheckSprite()
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
			if (!IsValid(IDreamUISpriteRenderInterface::Execute_SpriteRenderGetSprite(itemSprite.Get())))
			{
				this->RenderSpriteArray.RemoveAt(i);
			}
			else
			{
				if (auto spriteData = Cast<UDreamUISpriteData>(IDreamUISpriteRenderInterface::Execute_SpriteRenderGetSprite(itemSprite.Get())))
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
bool UDreamUIStaticSpriteAtlasData::PackAtlas()
{
	AtlasTextureArray.Empty();

	if (SpriteDataArray.Num() <= 0)return false;
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		UDreamUISpriteData* SpriteData = SpriteDataArray[i];
		if (!IsValid(SpriteData))
		{
			if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
			{
				bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
				auto ErrMsg = FText::Format(LOCTEXT("SpriteDataError", "{0} Packing atlas for DreamUIStaticSpriteAtlasData: '{1}', but SpriteData is not valid in spriteArray at index {2}")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
					, FText::FromString(this->GetPathName()), i);
				UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrMsg.ToString());
				FDreamUIUtils::EditorNotification(ErrMsg, false, 10.0f);
			}
			return false;
		}
		if (!IsValid(SpriteData->GetSpriteTexture()))
		{
			if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
			{
				bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
				auto ErrMsg = FText::Format(LOCTEXT("SpriteDataTextureError", "{0} Packing atlas for DreamGUIStaticSpriteAtlasData: '{1}', but SpriteData's texture is not valid of spriteData: '{2}'")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
					, FText::FromString(this->GetPathName()), FText::FromString(SpriteData->GetPathName()));
				UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrMsg.ToString());
				FDreamUIUtils::EditorNotification(ErrMsg, false, 10.0f);
			}
			return false;
		}
		if (SpriteData->PackingAtlas != this)
		{
			if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
			{
				bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
				auto ErrMsg = FText::Format(LOCTEXT("SpritePackingAtlasError", "{0} Packing atlas for DreamUIStaticSpriteAtlasData: '{1}', but SpriteData's packingAtlas is not this one, spriteData '{2}', at index: {3}")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
					, FText::FromString(this->GetPathName()), FText::FromString(SpriteData->GetPathName()), i);
				UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrMsg.ToString());
				FDreamUIUtils::EditorNotification(ErrMsg, false, 10.0f);
			}
			return false;
		}
	}

	auto MaxAtlasTextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(MaxTextureSize);
	//pack
	struct FAtlasSpriteBinPackContainer
	{
		rbp::MaxRectsBinPack BinPack;
		TArray<rbp::Rect> PackedRects;
		TArray<UDreamUISpriteData*> SpritesBelongToAtlas;
	};
	TArray<FAtlasSpriteBinPackContainer> AtlasSpriteBinPackArray;

	for (int SpriteIndex = 0; SpriteIndex < SpriteDataArray.Num(); SpriteIndex++)
	{
		auto SpriteData = SpriteDataArray[SpriteIndex];
		auto SpriteTexture = SpriteData->GetSpriteTexture();
		if (SpriteTexture->GetPlatformData()->Mips.Num() == 0)continue;
		bool bCanPackInExistingAtlas = false;
		for (int AtlasIndex = 0; AtlasIndex < AtlasSpriteBinPackArray.Num(); AtlasIndex++)
		{
			auto& AtlasSpriteBinPack = AtlasSpriteBinPackArray[AtlasIndex];
			if (TryPackAtlas(SpriteData, AtlasSpriteBinPack.BinPack, AtlasSpriteBinPack.PackedRects, AtlasSpriteBinPack.SpritesBelongToAtlas))
			{
				bCanPackInExistingAtlas = true;
				SpriteData->AtlasTextureIndex = AtlasIndex;
				break;
			}
			else
			{
				if (AtlasSpriteBinPack.BinPack.GetBinWidth() < MaxAtlasTextureSize)//not reach max size, expand it and try again
				{
					int32 PackSize = AtlasSpriteBinPack.BinPack.GetBinWidth();
					PackSize *= 2;
					AtlasSpriteBinPack.BinPack.ExpendSize(PackSize, PackSize);
					AtlasIndex--;//try pack to this bin pack atlas again
					continue;
				}
				//already reach max size, goto next bin pack
			}
		}
		if (!bCanPackInExistingAtlas)//can't pack in existing atlas, create a new one
		{
			int32 PackSize = 32;//start from very small size
			rbp::MaxRectsBinPack RectBinPack(PackSize, PackSize, false);
			TArray<rbp::Rect> PackedRects;
			TArray<UDreamUISpriteData*> SpritesBelongToAtlas;
			while (!TryPackAtlas(SpriteData, RectBinPack, PackedRects, SpritesBelongToAtlas))
			{
				PackSize *= 2;
				if (PackSize > MaxAtlasTextureSize)
				{
					break;
				}
				RectBinPack = rbp::MaxRectsBinPack(PackSize, PackSize, false);
			}
			if (PackSize > MaxAtlasTextureSize)
			{
				if (!bWarningIsAlreadyAppearedAtCurrentPackingSession)
				{
					bWarningIsAlreadyAppearedAtCurrentPackingSession = true;
					auto ErrMsg = FText::Format(LOCTEXT("AtlasSizeTooLargeError", "{0} Package Sprite atlas fail! Atlas texture size {1} larger than {2}: {3}! Please remove some large size Sprite, or split to multiple atlas.")
						, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
						, PackSize
						, FText::FromName(GET_MEMBER_NAME_CHECKED(UDreamUIStaticSpriteAtlasData, MaxTextureSize))
						, MaxAtlasTextureSize);
					UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrMsg.ToString());
					FDreamUIUtils::EditorNotification(ErrMsg, false, 10.0f);
				}
				return false;
			}
			AtlasSpriteBinPackArray.Add({RectBinPack, PackedRects, SpritesBelongToAtlas});
		}
	}

	TextureSizeArray.Reset();
	TexturePixelData.Reset();
	for (int i = 0; i < AtlasSpriteBinPackArray.Num(); i++)
	{
		auto& AtlasSpriteBinPack = AtlasSpriteBinPackArray[i];
		//create texture
		auto PlatformData = new FTexturePlatformData();
		PlatformData->SizeX = AtlasSpriteBinPack.BinPack.GetBinWidth();
		PlatformData->SizeY = AtlasSpriteBinPack.BinPack.GetBinHeight();
		PlatformData->PixelFormat = PF_B8G8R8A8;

		int32 AtlasSize = AtlasSpriteBinPack.BinPack.GetBinWidth();
		auto PixelBufferLength = AtlasSize * AtlasSize * GPixelFormats[PF_B8G8R8A8].BlockBytes;
		TArray<uint8> PixelData;
		PixelData.SetNumUninitialized(PixelBufferLength);
		FMemory::Memset(PixelData.GetData(), 0, PixelBufferLength);//default is transparent black
		//copy pixels
		FColor* AtlasColorBuffer = static_cast<FColor*>((void*)PixelData.GetData());
		float InvAtlasTextureSize = 1.0f / AtlasSize;
		for (int SpriteIndex = 0; SpriteIndex < AtlasSpriteBinPack.SpritesBelongToAtlas.Num(); SpriteIndex++)
		{
			auto SpriteData = AtlasSpriteBinPack.SpritesBelongToAtlas[SpriteIndex];
			auto SpriteTexture = SpriteData->GetSpriteTexture();
			UDreamUISpriteData::CheckAndApplySpriteTextureSetting(SpriteTexture);
#if WITH_EDITOR
			FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
			int32 SpriteWidth = SpriteTexture->GetSizeX();
			int32 SpriteHeight = SpriteTexture->GetSizeY();
			const FColor* SpriteColorBuffer = static_cast<const FColor*>(SpriteTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
			rbp::Rect Rect = AtlasSpriteBinPack.PackedRects[SpriteIndex];

			int DestY = Rect.y * AtlasSize;
			int SpritePixelIndex = 0;
			for (int32 TexY = 0; TexY < SpriteHeight; TexY++)
			{
				int DestX = Rect.x + DestY;
				for (int32 TexX = 0; TexX < SpriteWidth; TexX++)
				{
					int DestPixelIndex = DestX + TexX;
					AtlasColorBuffer[DestPixelIndex] = SpriteColorBuffer[SpritePixelIndex];
					SpritePixelIndex++;
				}
				DestY += AtlasSize;
			}
			//pixel padding
			if (SpriteData->GetUseEdgePixelPadding() && EdgePixelPadding > 0)
			{
				//left
				DestY = Rect.y * AtlasSize;
				for (int PaddingIndex = 0; PaddingIndex < EdgePixelPadding; PaddingIndex++)
				{
					int DestX = DestY + Rect.x - PaddingIndex - 1;
					int DestPixelIndex = DestX;
					for (int HeightIndex = 0; HeightIndex < SpriteHeight; HeightIndex++)
					{
						AtlasColorBuffer[DestPixelIndex] = AtlasColorBuffer[DestPixelIndex + 1];
						DestPixelIndex += AtlasSize;
					}
				}
				//right
				DestY = Rect.y * AtlasSize;
				for (int PaddingIndex = 0; PaddingIndex < EdgePixelPadding; PaddingIndex++)
				{
					int DestX = DestY + Rect.x + Rect.width + PaddingIndex;
					int DestPixelIndex = DestX;
					for (int HeightIndex = 0; HeightIndex < SpriteHeight; HeightIndex++)
					{
						AtlasColorBuffer[DestPixelIndex] = AtlasColorBuffer[DestPixelIndex - 1];
						DestPixelIndex += AtlasSize;
					}
				}
				//top, with corner
				DestY = (Rect.y - 1) * AtlasSize;
				for (int PaddingIndex = 0; PaddingIndex < EdgePixelPadding; PaddingIndex++)
				{
					int DestX = DestY + Rect.x;
					int DestPixelIndex = DestX - EdgePixelPadding;
					for (int WidthIndex = -EdgePixelPadding; WidthIndex < SpriteWidth + EdgePixelPadding; WidthIndex++)
					{
						AtlasColorBuffer[DestPixelIndex] = AtlasColorBuffer[DestPixelIndex + AtlasSize];
						DestPixelIndex += 1;
					}
					DestY -= AtlasSize;
				}
				//bottom, with corner
				DestY = (Rect.y + Rect.height) * AtlasSize;
				for (int PaddingIndex = 0; PaddingIndex < EdgePixelPadding; PaddingIndex++)
				{
					int DestX = DestY + Rect.x;
					int DestPixelIndex = DestX - EdgePixelPadding;
					for (int WidthIndex = -EdgePixelPadding; WidthIndex < SpriteWidth + EdgePixelPadding; WidthIndex++)
					{
						AtlasColorBuffer[DestPixelIndex] = AtlasColorBuffer[DestPixelIndex - AtlasSize];
						DestPixelIndex += 1;
					}
					DestY += AtlasSize;
				}
			}

			SpriteTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
			bool bSpriteDirty = SpriteData->ApplySpriteInfoAfterStaticPack(Rect, InvAtlasTextureSize);
			if (bSpriteDirty)
			{
				SpriteData->MarkPackageDirty();
			}
		}

		//store data
		auto PrevDataLength = TexturePixelData.Num();
		TexturePixelData.AddUninitialized(PixelBufferLength);
		FMemory::Memcpy(TexturePixelData.GetData() + PrevDataLength, PixelData.GetData(), PixelBufferLength);
		TextureSizeArray.Add(AtlasSize);

		//generate mipmaps
		{
			int mipsAdd = 0;
			//Declaring buffers here to reduce reallocs
			//We double buffer mips, using the prior buffer to build the next buffer
			TArray<FColor> mipRGBAs1;
			TArray<FColor> mipRGBAs2;

			//Access source data
			auto priorData = reinterpret_cast<const FColor*>(PixelData.GetData());
			int mipSize = AtlasSize;

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
				auto PrevLength = TexturePixelData.Num();
				TexturePixelData.AddUninitialized(mipBufferLength);
				FMemory::Memcpy(TexturePixelData.GetData() + PrevLength, mipRGBAs->GetData(), mipBufferLength);
			}
		}
	}
	auto OldCacheDataPath = this->GetCacheDataPath(TexturePixelDataMD5);
	if (FPaths::FileExists(OldCacheDataPath))
	{
		IFileManager::Get().Delete(*OldCacheDataPath);
	}
	TexturePixelDataMD5 = FDreamUIUtils::GetMD5String(FDreamUIUtils::GetMD5(TexturePixelData.GetData(), TexturePixelData.Num()));
	auto NewCacheDataPath = this->GetCacheDataPath(TexturePixelDataMD5);
	FFileHelper::SaveArrayToFile(TexturePixelData, *NewCacheDataPath);
	UDreamUIManagerWorldSubsystem::RefreshAllUI();

	return true;
}

void UDreamUIStaticSpriteAtlasData::PostInitProperties()
{
	UObject::PostInitProperties();
}

bool UDreamUIStaticSpriteAtlasData::TryPackAtlas(UDreamUISpriteData* Sprite, rbp::MaxRectsBinPack& RectBinPack, TArray<rbp::Rect>& PackedRects, TArray<UDreamUISpriteData*>& PackedSprites)
{
	auto CalculatedEdgePixelPadding = Sprite->GetUseEdgePixelPadding() ? EdgePixelPadding : 0;
	auto SpriteTexture = Sprite->GetSpriteTexture();
	auto Space = SpaceBetweenSprites + CalculatedEdgePixelPadding + CalculatedEdgePixelPadding;
	//add space
#if WITH_EDITOR
	FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
	int InsertRectWidth = SpriteTexture->GetSizeX() + Space;
	int InsertRectHeight = SpriteTexture->GetSizeY() + Space;
	auto Rect = RectBinPack.Insert(InsertRectWidth, InsertRectHeight, rbp::MaxRectsBinPack::FreeRectChoiceHeuristic::RectBestAreaFit);
	if (Rect.width <= 0)//cannot fit
	{
		return false;
	}
	//remove space
	Rect.x += CalculatedEdgePixelPadding;
	Rect.y += CalculatedEdgePixelPadding;
	Rect.width -= Space;
	Rect.height -= Space;

	PackedRects.Add(Rect);
	PackedSprites.Add(Sprite);
	return true;
}

void UDreamUIStaticSpriteAtlasData::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	bool bCacheDataValid = false;
	auto CacheDataPath = this->GetCacheDataPath(TexturePixelDataMD5);
	if (FPaths::FileExists(CacheDataPath))
	{
		TArray<uint8> TextureData;
		if (FFileHelper::LoadFileToArray(TextureData, *CacheDataPath))
		{
			auto FileMD5 = FDreamUIUtils::GetMD5String(FDreamUIUtils::GetMD5(TextureData.GetData(), TextureData.Num()));
			if (FileMD5 == TexturePixelDataMD5)
			{
				TexturePixelDataForBuild = TextureData;
				bCacheDataValid = true;
			}
		}
	}
	if (!bCacheDataValid)
	{
		auto Msg = FText::Format(LOCTEXT("WrongTextureData", "Cooking DreamUIStaticSpriteAtlasData:{0}, get wrong texture data in cache! You should manny click \"Pack Atlas\" button to build atlas texture!")
			, FText::FromString(this->GetPathName()));
		FDreamUIUtils::EditorNotification(Msg, false);
	}
}
void UDreamUIStaticSpriteAtlasData::WillNeverCacheCookedPlatformDataAgain()
{
	TexturePixelDataForBuild.Empty();
}
void UDreamUIStaticSpriteAtlasData::ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	TexturePixelDataForBuild.Empty();
}
void UDreamUIStaticSpriteAtlasData::MarkNotInitialized()
{
	bIsInitialized = false;
	bWarningIsAlreadyAppearedAtCurrentPackingSession = false;
}

void UDreamUIStaticSpriteAtlasData::MarkAtlasPackDirty()
{
	bIsAtlasPackDirty = true;
	TexturePixelDataMD5 = "";
	MarkPackageDirty();
}

bool UDreamUIStaticSpriteAtlasData::CheckInvalidSpriteData()const
{
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		auto SpriteData = SpriteDataArray[i];
		if (!IsValid(SpriteData))
		{
			return true;
		}
		else if (!IsValid(SpriteData->GetSpriteTexture()))
		{
			return true;
		}
		else if (SpriteData->PackingAtlas != this)
		{
			return true;
		}
	}
	return false;
}
void UDreamUIStaticSpriteAtlasData::CleanupInvalidSpriteData()
{
	auto PrevCount = SpriteDataArray.Num();
	for (int i = 0; i < SpriteDataArray.Num(); i++)
	{
		auto SpriteData = SpriteDataArray[i];
		if (!IsValid(SpriteData))
		{
			SpriteDataArray.RemoveAt(i);
			i--;
		}
		else if (!IsValid(SpriteData->GetSpriteTexture()))
		{
			SpriteDataArray.RemoveAt(i);
			i--;
		}
		else if (SpriteData->PackingAtlas != this)
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

void UDreamUIStaticSpriteAtlasData::BeginDestroy()
{
#if WITH_EDITOR
	for (auto& item : SpriteDataArray)
	{
		item->bIsInitialized = false;
	}
#endif
	Super::BeginDestroy();
}

bool UDreamUIStaticSpriteAtlasData::InitCheck()
{
	if (!bIsInitialized)
	{
#if WITH_EDITOR
		if (TexturePixelDataForBuild.Num() == 0)//no need to pack in cook process
		{
			if (!bIsAtlasPackDirty)
			{
				bool bCacheDataValid = false;
				auto CacheDataPath = this->GetCacheDataPath(TexturePixelDataMD5);
				if (FPaths::FileExists(CacheDataPath))
				{
					TArray<uint8> TextureData;
					if (FFileHelper::LoadFileToArray(TextureData, *CacheDataPath))
					{
						auto FileMD5 = FDreamUIUtils::GetMD5String(FDreamUIUtils::GetMD5(TextureData.GetData(), TextureData.Num()));
						if (FileMD5 == TexturePixelDataMD5)
						{
							TexturePixelData = TextureData;
							bCacheDataValid = true;
						}
						else
						{
							IFileManager::Get().Delete(*CacheDataPath);
						}
					}
				}
				if (!bCacheDataValid)
				{
					bIsAtlasPackDirty = true;
				}
			}
			if (bIsAtlasPackDirty)
			{
				bIsAtlasPackDirty = false;
				auto bPackAtlasSuccess = PackAtlas();
				if (!bPackAtlasSuccess)
				{
					return false;
				}
			}
		}
#endif
		bIsInitialized = true;
		
		auto& DataArray =
#if WITH_EDITOR
			TexturePixelData;
#else
			TexturePixelDataForBuild;
#endif

		this->AtlasTextureArray.Empty();
		uint32 TextureDataOffset = 0;
		static int TextureNameSuffix = 0;
		for (int i = 0; i < TextureSizeArray.Num(); i++)
		{
			auto TextureSize = TextureSizeArray[i];
			//create texture
			auto NewTexture = NewObject<UTexture2D>(
				GetTransientPackage(),
				FName(*FString::Printf(TEXT("DreamUIStaticSpriteAtlasData_Texture_%d"), TextureNameSuffix++)),
				EObjectFlags::RF_Transient
			);
			auto PlatformData = new FTexturePlatformData();
			PlatformData->SizeX = TextureSize;
			PlatformData->SizeY = TextureSize;
			PlatformData->PixelFormat = PF_B8G8R8A8;
			NewTexture->SetPlatformData(PlatformData);

			//mipmaps
			{
				int mipSize = TextureSize;
				while (true)
				{
					// Allocate next mipmap.
					auto mip = new FTexture2DMipMap;
					NewTexture->GetPlatformData()->Mips.Add(mip);
					mip->SizeX = mipSize;
					mip->SizeY = mipSize;
					mip->BulkData.Lock(LOCK_READ_WRITE);
					auto pixelBufferLength = mipSize * mipSize * GPixelFormats[PF_B8G8R8A8].BlockBytes;
					void* mipData = mip->BulkData.Realloc(pixelBufferLength);
					FMemory::Memcpy(mipData, DataArray.GetData() + TextureDataOffset, pixelBufferLength);
					mip->BulkData.Unlock();

					mipSize = mipSize >> 1;
					if (mipSize == 0)
					{
						break;
					}
					TextureDataOffset += pixelBufferLength;
				}
			}

			NewTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
			NewTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
			NewTexture->SRGB = AtlasTextureUseSRGB;
			NewTexture->Filter = AtlasTextureFilter;
			NewTexture->UpdateResource();

			this->AtlasTextureArray.Add(NewTexture);
#if WITH_EDITOR
			for (auto& sprite : RenderSpriteArray)
			{
				if (sprite.IsValid())
				{
					IDreamUISpriteRenderInterface::Execute_ApplyAtlasTextureChange(sprite.Get());
				}
			}
#endif
		}
#if !WITH_EDITOR
		//empty it to reduce memory usage
		TexturePixelDataForBuild.Empty();
#endif
	}
	return bIsInitialized;
}
UTexture2D* UDreamUIStaticSpriteAtlasData::GetAtlasTexture(int32 Index)
{
	InitCheck();
	return AtlasTextureArray.IsValidIndex(Index) ? AtlasTextureArray[Index] : nullptr;
}
bool UDreamUIStaticSpriteAtlasData::ReadPixel(int InTextureIndex, const FVector2D& InUV, FColor& OutPixel)
{
	InitCheck();

	auto AtlasTexture = AtlasTextureArray[InTextureIndex];
	auto PlatformData = AtlasTexture->GetPlatformData();
	if (PlatformData && PlatformData->Mips.Num() > 0)
	{
		auto Pixels = static_cast<FColor*>(PlatformData->Mips[0].BulkData.Lock(LOCK_READ_ONLY));
		auto TextureSize = AtlasTexture->GetSizeX();
		auto uvInFullSize = FIntPoint(InUV.X * TextureSize, InUV.Y * TextureSize);
		auto PixelIndex = uvInFullSize.Y * TextureSize + uvInFullSize.X;
		OutPixel = Pixels[PixelIndex];
		PlatformData->Mips[0].BulkData.Unlock();
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
