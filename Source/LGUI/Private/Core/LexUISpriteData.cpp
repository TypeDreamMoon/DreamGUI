// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUISpriteData.h"
#include "LGUI.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexSpriteBase.h"
#include "Core/LexUIDynamicSpriteAtlasData.h"
#include "Core/LexUIStaticSpriteAtlasData.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUIManager.h"
#include "RHI.h"
#include "TextureCompiler.h"
#include "RenderingThread.h"

#define LOCTEXT_NAMESPACE "LGUISpriteData"

void FLexUISpriteInfo::ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal)
{
	Width = InWidth;
	Height = InHeight;

	MinUV.X = InX * texFullWidthReciprocal;
	MaxUV.Y = (InY + InHeight) * texFullHeightReciprocal;
	MaxUV.X = (InX + InWidth) * texFullWidthReciprocal;
	MinUV.Y = InY * texFullHeightReciprocal;
}
void FLexUISpriteInfo::ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal, const FVector4f& uvRect)
{
	Width = InWidth;
	Height = InHeight;

	MinUV.X = InX * texFullWidthReciprocal + uvRect.X;
	MaxUV.Y = (InY + InHeight) * texFullHeightReciprocal * uvRect.W + uvRect.Y;
	MaxUV.X = (InX + InWidth) * texFullWidthReciprocal * uvRect.Z + uvRect.X;
	MinUV.Y = InY * texFullHeightReciprocal + uvRect.Y;
}
bool FLexUISpriteInfo::HasBorder()const
{
	return Border.Left != 0 || Border.Right != 0 || Border.Top != 0 || Border.Bottom != 0;
}
bool FLexUISpriteInfo::HasPadding()const
{
	return Padding.Left != 0 || Padding.Right != 0 || Padding.Top != 0 || Padding.Bottom != 0;
}
void FLexUISpriteInfo::ApplyBorderUV(float texFullWidthReciprocal, float texFullHeightReciprocal)
{
	BorderMinUV.X = MinUV.X + Border.Left * texFullWidthReciprocal;
	BorderMaxUV.X = MaxUV.X - Border.Right * texFullWidthReciprocal;
	BorderMaxUV.Y = MaxUV.Y - Border.Bottom * texFullHeightReciprocal;
	BorderMinUV.Y = MinUV.Y + Border.Top * texFullHeightReciprocal;
}

bool ULexUISpriteData::PackSprite()
{
	CheckAndApplySpriteTextureSetting(SpriteTexture);

	auto AtlasData = ULexUIDynamicSpriteAtlasManager::FindOrAdd(PackingTag);
	AtlasData->EnsureAtlasTexture();
#if WITH_EDITOR
	FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
	if (AtlasData->PackSprite(this))
	{
		return true;
	}
	else//all area cannot fit the texture, then show a warning
	{
		auto WarningMsg = FText::Format(LOCTEXT("PackageSprite_AtlasSize_Warning", "{0} Can't pack sprite texture:{1}!\
\nTry reduce sprite texture size, or maybe don't use it as sprite.\
\nAlso remember to dispose unused atlas by call function DisposeAtlasByPackingTag from LGUIDynamicSpriteAtlasManager.\
")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
			, FText::FromString(SpriteTexture->GetPathName())
			);
		UE_LOG(LGUI, Warning, TEXT("%s"), *WarningMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(WarningMsg, false);
#endif
		return false;
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
void ULexUISpriteData::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, PackingAtlas))
	{
		if (IsValid(PackingAtlas))
		{
			PackingAtlas->RemoveSpriteData(this);
		}
	}
}

void ULexUISpriteData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CheckSpriteTexture();
	if (PropertyChangedEvent.Property != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteTexture))
		{
			if (SpriteTexture != nullptr)
			{
				CheckAndApplySpriteTextureSetting(SpriteTexture);
#if WITH_EDITOR
				FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
				SpriteInfo.Width = SpriteTexture->GetSizeX();
				SpriteInfo.Height = SpriteTexture->GetSizeY();
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, PackingTag))
		{
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteTexture))
		{
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, bUseEdgePixelPadding))
		{
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISpriteData, PackingAtlas))
		{
			if (IsValid(PackingAtlas))
			{
				PackingAtlas->CheckSprite();
			}
			if (auto DynamicSpriteAtlasData = ULexUIDynamicSpriteAtlasManager::Find(PackingTag))
			{
				DynamicSpriteAtlasData->CheckSprite();
			}
			this->ReloadTexture();
		}

		ULexUIManagerWorldSubsystem::RefreshAllUI();
	}
}

void ULexUISpriteData::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FString PropertyPath;
	auto PropNode = PropertyChangedEvent.PropertyChain.GetHead();
	while (PropNode != nullptr)
	{
		PropertyPath += PropNode->GetValue()->GetName();
		PropNode = PropNode->GetNextNode();
		if (PropNode)
		{
			PropertyPath += ".";
		}
	}
	if (PropertyPath.StartsWith(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Border).ToString()))
	{
		SpriteInfo.bIsBorderDirty = true;
		//Sprite data, apply border
		if (SpriteTexture != nullptr)
		{
#if WITH_EDITOR
			FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
			SpriteInfo.Width = SpriteTexture->GetSizeX();
			SpriteInfo.Height = SpriteTexture->GetSizeY();
			if (bIsInitialized)
			{
				float atlasTextureSizeInv = 1.0f / GetAtlasTexture()->GetSizeX();
				SpriteInfo.ApplyBorderUV(atlasTextureSizeInv, atlasTextureSizeInv);
			}
		}
	}
}

bool ULexUISpriteData::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULexUISpriteData, PackingTag))
		{
			return IsValid(PackingAtlas);
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
	if (IsValid(PackingAtlas))
	{
		PackingAtlas->RemoveSpriteData(this);
		PackingAtlas->MarkNotInitialized();
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
		if (IsValid(PackingAtlas))
		{
#if WITH_EDITOR
			//add it again as check if it exists in packingAtlas
			PackingAtlas->AddSpriteData(this);
#endif
			if (PackingAtlas->InitCheck())
			{
				AtlasTexture = PackingAtlas->GetAtlasTexture(TextureIndex);
				//no need to set spriteInfo because it is already set when do static pack
				return;
			}
			else
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d PackingAtlas:%s pack error, will fallback to use PackingTag!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(PackingAtlas->GetPathName()));
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
			if (PackSprite())
			{
				bIsInitialized = true;
			}
			else
			{
				auto WarningMsg = FString::Printf(TEXT("[%s].%d Pack Sprite fail. Will automatically clear PackingTag to make it valid."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
				UE_LOG(LGUI, Warning, TEXT("%s"), *WarningMsg);
#if WITH_EDITOR
				FLexUIUtils::EditorNotification(FText::FromString(WarningMsg), false);
#endif
				PackingTag = NAME_None;
				this->MarkPackageDirty();
				bIsInitialized = false;
			}
		}
		else//no need to pack to atlas, so spriteTexture self is the atlas
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
	return !IsValid(PackingAtlas) && PackingTag.IsNone();
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
		auto lguiSetting = GetDefault<ULexUISettings>()->DefaultAtlasSetting.SpaceBetweenSprites;
		if (inSpriteTexture->GetSizeX() + atlasPadding * 2 > WARNING_ATLAS_SIZE || inSpriteTexture->GetSizeY() + atlasPadding * 2 > WARNING_ATLAS_SIZE)
		{
			auto warningMsg = FText::Format(LOCTEXT("CreateLexUISpriteData_Size_Warning", "{0} Target texture width or height is too large! Consider use UITexture to render this texture.")
				, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
			UE_LOG(LGUI, Warning, TEXT("%s"), *warningMsg.ToString());
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(warningMsg, false);
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
		spriteInfo.Width = inSpriteTexture->GetSizeX();
		spriteInfo.Height = inSpriteTexture->GetSizeY();
		spriteInfo.Border.Left = (uint16)inHorizontalBorder.X;
		spriteInfo.Border.Right = (uint16)inHorizontalBorder.Y;
		spriteInfo.Border.Top = (uint16)inVerticalBorder.X;
		spriteInfo.Border.Bottom = (uint16)inVerticalBorder.Y;
	}
	return result;
}

void ULexUISpriteData::AddUISprite(TScriptInterface<class ILexUISpriteRenderInterface> InUISprite)
{
	if (IsValid(PackingAtlas))
	{
		InitSpriteData();
#if WITH_EDITOR
		//packingAtlas only need to collect Sprite in editor
		PackingAtlas->AddRenderSprite(InUISprite);
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
	if (IsValid(PackingAtlas))
	{
#if WITH_EDITOR
		//packingAtlas only need to collect Sprite in editor
		PackingAtlas->RemoveRenderSprite(InUISprite);
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
	if (PackingAtlas != nullptr)
	{
		return PackingAtlas->ReadPixel(TextureIndex, InUV, OutPixel);
	}
	return false;
}
bool ULexUISpriteData::SupportReadPixel()const
{
	return PackingAtlas != nullptr;
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
		FLexUIUtils::EditorNotification(errMsg, false, 10);
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
		FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
		return nullptr;
	}
	return defaultFrameRect;
}


#undef LOCTEXT_NAMESPACE
