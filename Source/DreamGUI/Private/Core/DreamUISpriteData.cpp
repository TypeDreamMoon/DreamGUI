// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUISpriteData.h"
#include "Core/DreamGUISettings.h"
#include "DreamGUI.h"
#include "Core/DreamUISettings.h"
#include "Core/Components/DreamSpriteBase.h"
#include "Core/DreamUIDynamicSpriteAtlasData.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUIManager.h"
#include "RHI.h"
#include "TextureCompiler.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DreamGUISpriteData"

bool FDreamUISpriteInfo::ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal)
{
	auto NewMinUV = FVector2f(InX * texFullWidthReciprocal, InY * texFullHeightReciprocal);
	auto NewMaxUV = FVector2f((InX + InWidth) * texFullWidthReciprocal, (InY + InHeight) * texFullHeightReciprocal);
	
	if (Width == InWidth && Height == InHeight && MinUV == NewMinUV && MaxUV == NewMaxUV)
		return false;
	
	Width = InWidth;
	Height = InHeight;
	MinUV = NewMinUV;
	MaxUV = NewMaxUV;
	return true;
}
bool FDreamUISpriteInfo::ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal, const FVector4f& uvRect)
{
	auto NewMinUV = FVector2f(InX * texFullWidthReciprocal + uvRect.X, InY * texFullHeightReciprocal + uvRect.Y);
	auto NewMaxUV = FVector2f((InX + InWidth) * texFullWidthReciprocal * uvRect.Z + uvRect.X, (InY + InHeight) * texFullHeightReciprocal * uvRect.W + uvRect.Y);
	
	if (Width == InWidth && Height == InHeight && MinUV == NewMinUV && MaxUV == NewMaxUV)
		return false;
	
	Width = InWidth;
	Height = InHeight;
	MinUV = NewMinUV;
	MaxUV = NewMaxUV;
	return true;
}
bool FDreamUISpriteInfo::HasBorder()const
{
	return Border.Left != 0 || Border.Right != 0 || Border.Top != 0 || Border.Bottom != 0;
}
bool FDreamUISpriteInfo::HasPadding()const
{
	return Padding.Left != 0 || Padding.Right != 0 || Padding.Top != 0 || Padding.Bottom != 0;
}
bool FDreamUISpriteInfo::ApplyBorderUV(float texFullWidthReciprocal, float texFullHeightReciprocal)
{
	auto NewBorderMinUV = FVector2f(MinUV.X + Border.Left * texFullWidthReciprocal, MinUV.Y + Border.Top * texFullHeightReciprocal);
	auto NewBorderMaxUV = FVector2f(MaxUV.X - Border.Right * texFullWidthReciprocal, MaxUV.Y - Border.Bottom * texFullHeightReciprocal);
	if (BorderMinUV == NewBorderMinUV && BorderMaxUV == NewBorderMaxUV)
		return false;
	BorderMinUV = NewBorderMinUV;
	BorderMaxUV = NewBorderMaxUV;
	return true;
}

UDreamUISpriteData::UDreamUISpriteData()
{
	// PackingAtlas = LoadObject<UDreamUIStaticSpriteAtlasData>(NULL, TEXT("/DreamGUI/DefaultSpriteAtlasData"));
}

bool UDreamUISpriteData::PackSprite()
{
	CheckAndApplySpriteTextureSetting(SpriteTexture);

	auto AtlasData = UDreamUIDynamicSpriteAtlasManager::FindOrAdd(PackingTag);
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
		auto WarningMsg = FText::Format(LOCTEXT("PackageSprite_AtlasSize_Warning", "{0} Can't pack sprite texture:{1}!"
"\nTry reduce sprite texture size, or maybe don't use it as sprite."
"\nAlso remember to dispose unused atlas by call function DisposeAtlasByPackingTag from {2}."
)
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
			, FText::FromString(SpriteTexture->GetPathName())
			, FText::FromString(UDreamUIDynamicSpriteAtlasManager::StaticClass()->GetName())
			);
		UE_LOG(DreamGUI, Warning, TEXT("%s"), *WarningMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(WarningMsg, false);
#endif
		return false;
	}
}

void UDreamUISpriteData::CheckSpriteTexture()
{
	if (SpriteTexture == nullptr)
	{
		SpriteTexture = FDreamUIUtils::GetDefaultWhiteTexture();
	}
}

bool UDreamUISpriteData::ApplySpriteInfoAfterStaticPack(const rbp::Rect& InPackedRect, float InAtlasTextureSizeInv)
{
	bool bBaseInfoDirty = SpriteInfo.ApplyUV(InPackedRect.x, InPackedRect.y, InPackedRect.width, InPackedRect.height, InAtlasTextureSizeInv, InAtlasTextureSizeInv);
	bool bBorderInfoDirty = SpriteInfo.ApplyBorderUV(InAtlasTextureSizeInv, InAtlasTextureSizeInv);
	bIsInitialized = false;
	return bBaseInfoDirty || bBorderInfoDirty;
}
#if WITH_EDITOR
void UDreamUISpriteData::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingAtlas))
	{
		if (IsValid(PackingAtlas))
		{
			PackingAtlas->RemoveSpriteData(this);
			PackingAtlas->MarkAtlasPackDirty();
		}
	}
}

void UDreamUISpriteData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CheckSpriteTexture();
	if (PropertyChangedEvent.Property != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteTexture))
		{
			if (PackingType == EDreamUISpritePackingType::Static && PackingAtlas)
			{
				PackingAtlas->MarkAtlasPackDirty();
			}
			if (SpriteTexture != nullptr)
			{
				CheckAndApplySpriteTextureSetting(SpriteTexture);
#if WITH_EDITOR
				FTextureCompilingManager::Get().FinishCompilation({ SpriteTexture });
#endif
				SpriteInfo.Width = SpriteTexture->GetSizeX();
				SpriteInfo.Height = SpriteTexture->GetSizeY();
			}
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingTag))
		{
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, bUseEdgePixelPadding))
		{
			if (PackingType == EDreamUISpritePackingType::Static && PackingAtlas)
			{
				PackingAtlas->MarkAtlasPackDirty();
			}
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingAtlas))
		{
			if (IsValid(PackingAtlas))
			{
				if (!PackingAtlas->ContainsSpriteData(this))
				{
					PackingAtlas->AddSpriteData(this);
					PackingAtlas->MarkAtlasPackDirty();
				}
			}
			if (auto DynamicSpriteAtlasData = UDreamUIDynamicSpriteAtlasManager::Find(PackingTag))
			{
				DynamicSpriteAtlasData->CheckSprite();
			}
			this->ReloadTexture();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingType))
		{
			if (PackingType == EDreamUISpritePackingType::Static && PackingAtlas)
			{
				PackingAtlas->MarkAtlasPackDirty();
			}
			this->ReloadTexture();
		}

		UDreamUIManagerWorldSubsystem::RefreshAllUI();
	}
}

void UDreamUISpriteData::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
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
	if (PropertyPath.StartsWith(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Border).ToString()))
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

bool UDreamUISpriteData::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}

void UDreamUISpriteData::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
}

void UDreamUISpriteData::MarkAllSpritesNeedToReinitialize()
{
	UDreamUIDynamicSpriteAtlasManager::ResetAtlasMap();
	for (TObjectIterator<UDreamUISpriteData> SpriteItr; SpriteItr; ++SpriteItr)
	{
		SpriteItr->bIsInitialized = false;
	}
}
#endif

bool UDreamUISpriteData::NeedsSpriteTextureSetting(const UTexture2D* InSpriteTexture)
{
	if (!IsValid(InSpriteTexture))
	{
		return false;
	}
	return
		InSpriteTexture->CompressionSettings != TextureCompressionSettings::TC_EditorIcon
		|| InSpriteTexture->LODGroup != TextureGroup::TEXTUREGROUP_UI
		|| InSpriteTexture->SRGB != true
		;
}

void UDreamUISpriteData::CheckAndApplySpriteTextureSetting(UTexture2D* InSpriteTexture)
{
	if (NeedsSpriteTextureSetting(InSpriteTexture))
	{
		InSpriteTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
		InSpriteTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
		InSpriteTexture->SRGB = true;
		InSpriteTexture->UpdateResource();
		InSpriteTexture->MarkPackageDirty();
	}
}

void UDreamUISpriteData::ReloadTexture()
{
	bIsInitialized = false;

#if WITH_EDITOR
	if (PackingType == EDreamUISpritePackingType::Static && IsValid(PackingAtlas))
	{
		PackingAtlas->MarkAtlasPackDirty();
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

void UDreamUISpriteData::InitSpriteData()
{
	if (!bIsInitialized)
	{
#if WITH_EDITOR
		if (IsRunningCookCommandlet())
		{
			bIsInitialized = true;
			return;
		}
#endif
		if (PackingType == EDreamUISpritePackingType::Static)
		{
			if (IsValid(PackingAtlas))
			{
#if WITH_EDITOR
				if (!PackingAtlas->ContainsSpriteData(this))
				{
					PackingAtlas->AddSpriteData(this);
					PackingAtlas->MarkAtlasPackDirty();
				}
#endif
				AtlasTexture = PackingAtlas->GetAtlasTexture(AtlasTextureIndex);
				if (AtlasTexture)
				{
					bIsInitialized = true;
				}
				else
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d SpriteData:%s AtlasTexture is null! "), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetPathName()));
				}
			}
		}
		else
		{
			if (SpriteTexture == nullptr)
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d SpriteData:%s SpriteTexture is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetPathName()));
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
					UE_LOG(DreamGUI, Warning, TEXT("%s"), *WarningMsg);
#if WITH_EDITOR
					FDreamUIUtils::EditorNotification(FText::FromString(WarningMsg), false);
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
}

UTexture2D* UDreamUISpriteData::GetAtlasTexture()
{
	InitSpriteData();
	return AtlasTexture;
}
const FDreamUISpriteInfo& UDreamUISpriteData::GetSpriteInfo()
{
	InitSpriteData();
	return SpriteInfo;
}

bool UDreamUISpriteData::IsIndividual()const
{
	return !IsValid(PackingAtlas) && PackingTag.IsNone();
}

bool UDreamUISpriteData::HavePackingTag()const
{
	return !PackingTag.IsNone();
}
const FName& UDreamUISpriteData::GetPackingTag()const
{
	return PackingTag;
}

UDreamUISpriteData* UDreamUISpriteData::CreateDreamUISpriteData(UObject* Outer, UTexture2D* InSpriteTexture, FMargin InBorder, FName InPackingTag /* = TEXT("Main") */)
{
	if (!IsValid(InSpriteTexture))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Input texture not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	// check size
	int32 atlasPadding = UDreamUISettings::GetAtlasTexturePadding(InPackingTag);
	if (InSpriteTexture->GetSurfaceWidth() + atlasPadding * 2 > WARNING_ATLAS_SIZE || InSpriteTexture->GetSurfaceHeight() + atlasPadding * 2 > WARNING_ATLAS_SIZE)
	{
		auto warningMsg = FText::Format(LOCTEXT("CreateDreamUISpriteData_Size_Warning", "{0} Target texture width or height is too large! Consider use DreamImage to render this texture.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(DreamGUI, Warning, TEXT("%s"), *warningMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(warningMsg, false);
#endif
	}
	// Apply setting for Sprite creation
	CheckAndApplySpriteTextureSetting(InSpriteTexture);

	UDreamUISpriteData* result = NewObject<UDreamUISpriteData>(IsValid(Outer) ? Outer : GetTransientPackage());
	result->PackingTag = InPackingTag;
	result->SpriteTexture = InSpriteTexture;
	auto& spriteInfo = result->SpriteInfo;
	spriteInfo.Width = InSpriteTexture->GetSizeX();
	spriteInfo.Height = InSpriteTexture->GetSizeY();
	spriteInfo.Border = InBorder;
	return result;
}

void UDreamUISpriteData::AddUISprite(TScriptInterface<class IDreamUISpriteRenderInterface> InUISprite)
{
	if (PackingType == EDreamUISpritePackingType::Static)
	{
		if (IsValid(PackingAtlas))
		{
#if WITH_EDITOR
			//packingAtlas only need to collect Sprite in editor
			PackingAtlas->AddRenderSprite(InUISprite);
#endif
		}
	}
	else
	{
		if (!PackingTag.IsNone())
		{
			auto& spriteArray = UDreamUIDynamicSpriteAtlasManager::FindOrAdd(PackingTag)->RenderSpriteArray;
			spriteArray.AddUnique(InUISprite.GetObject());
		}
	}
}
void UDreamUISpriteData::RemoveUISprite(TScriptInterface<class IDreamUISpriteRenderInterface> InUISprite)
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
		if (auto spriteData = UDreamUIDynamicSpriteAtlasManager::Find(PackingTag))
		{
			spriteData->RenderSpriteArray.RemoveSingle(InUISprite.GetObject());
		}
	}
}
bool UDreamUISpriteData::ReadPixel(const FVector2D& InUV, FColor& OutPixel)const
{
	if (PackingAtlas != nullptr)
	{
		return PackingAtlas->ReadPixel(AtlasTextureIndex, InUV, OutPixel);
	}
	return false;
}
bool UDreamUISpriteData::SupportReadPixel()const
{
	return PackingAtlas != nullptr;
}

UDreamUISpriteData* UDreamUISpriteData::GetDefaultWhiteSolid()
{
	// Strong, and re-checked every call: see UDreamUIFontData_BaseObject::GetDefaultFont. A bare
	// static UObject* kept nothing alive across a level change and cached a failed load forever.
	static TStrongObjectPtr<UDreamUISpriteData> defaultWhiteSolidCache;
	if (!defaultWhiteSolidCache.IsValid())
	{
		defaultWhiteSolidCache.Reset(UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultWhiteSolidSprite, TEXT("DefaultWhiteSolidSprite")));
	}
	auto defaultWhiteSolid = defaultWhiteSolidCache.Get();
	if (defaultWhiteSolid == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default Sprite error! Missing some content of DreamGUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(DreamGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		return nullptr;
	}
	return defaultWhiteSolid;
}
UDreamUISpriteData* UDreamUISpriteData::GetDefaultFrameRect()
{
	static TStrongObjectPtr<UDreamUISpriteData> defaultFrameRectCache;
	if (!defaultFrameRectCache.IsValid())
	{
		defaultFrameRectCache.Reset(UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultFrameRectSprite, TEXT("DefaultFrameRectSprite")));
	}
	auto defaultFrameRect = defaultFrameRectCache.Get();
	if (defaultFrameRect == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default sprite error! Missing some content of DreamUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(DreamGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		return nullptr;
	}
	return defaultFrameRect;
}


#undef LOCTEXT_NAMESPACE
