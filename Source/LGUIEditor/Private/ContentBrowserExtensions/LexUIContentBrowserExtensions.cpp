// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIContentBrowserExtensions.h"
#include "Engine/EngineTypes.h"
#include "Core/LexUISpriteData.h"
#include "Engine/Texture2D.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "LGUIEditorStyle.h"
#include "Core/LexUIFontData_DistanceField.h"
#include "DataFactory/LexUIFontDataDistanceFieldFactory.h"
#include "DataFactory/LexUISpriteDataFactory.h"
#include "DataFactory/LexUIPrefabFactory.h"
#include "Engine/FontFace.h"
#include "PrefabSystem/LexUIPrefab.h"

#define LOCTEXT_NAMESPACE "LexUIContentBrowserExtensions"

//////////////////////////////////////////////////////////////////////////

FContentBrowserMenuExtender_SelectedAssets ContentBrowserExtenderDelegate;
FDelegateHandle ContentBrowserExtenderDelegateHandle;

//////////////////////////////////////////////////////////////////////////
// FCreateSpriteFromTextureExtension

#include "IAssetTools.h"
#include "AssetToolsModule.h"


//////////////////////////////////////////////////////////////////////////
// FLexUIContentBrowserExtensions_Impl

/**
 * Load the picked assets, and not one asset earlier. The menu is built on every right-click in the
 * Content Browser, so resolving the selection there means a folder of a hundred meshes is loaded
 * synchronously just to discover that none of them is ours.
 */
template<typename T>
static TArray<T*> LexUIResolveSelectedAssets(const TArray<FAssetData>& InSelectedAssets)
{
	TArray<T*> Result;
	Result.Reserve(InSelectedAssets.Num());
	for (const FAssetData& AssetData : InSelectedAssets)
	{
		if (T* Asset = Cast<T>(AssetData.GetAsset()))
		{
			Result.Add(Asset);
		}
	}
	return Result;
}

class FLGUIContentBrowserExtensions_Impl
{
public:
	static void CreateSpriteActionsSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("SpriteActionsSubMenuLabel", "LexUISprite"),
			LOCTEXT("SpriteActionsSubMenuToolTip", "Sprite-related actions for this texture."),
			FNewMenuDelegate::CreateStatic(&FLGUIContentBrowserExtensions_Impl::PopulateSpriteActionsMenu, SelectedAssets),
			false,
			FSlateIcon(FLGUIEditorStyle::GetStyleSetName(), "LGUIEditor.SpriteDataAction")
		);
	}
	static void CreatePrefabActionsSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("PrefabActionsSubMenuLabel", "LexUIPrefab"),
			LOCTEXT("PrefabActionsSubMenuToolTip", "Prefab-related actions for this prefab."),
			FNewMenuDelegate::CreateStatic(&FLGUIContentBrowserExtensions_Impl::PopulatePrefabActionMenu, SelectedAssets),
			false,
			FSlateIcon(FLGUIEditorStyle::GetStyleSetName(), "LGUIEditor.PrefabDataAction")
		);
	}
	static void CreateFontActionsSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("FontActionsSubMenuLabel", "LexUIFont"),
			LOCTEXT("FontActionsSubMenuToolTip", "LexUIFont-related actions for this prefab."),
			FNewMenuDelegate::CreateStatic(&FLGUIContentBrowserExtensions_Impl::PopulateFontActionMenu, SelectedAssets),
			false,
			FSlateIcon()
		);
	}

	static void PopulateSpriteActionsMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
	{
		// Create sprites
		struct LOCAL
		{
			static void CreateSpritesFromTextures(TArray<FAssetData> InTextures)
			{
				FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

				TArray<UObject*> ObjectsToSync;

				for (UTexture2D* Texture : LexUIResolveSelectedAssets<UTexture2D>(InTextures))
				{
					// Create the factory used to generate the sprite
					ULexUISpriteDataFactory* SpriteFactory = NewObject<ULexUISpriteDataFactory>();
					SpriteFactory->SpriteTexture = Texture;

					// Create the sprite
					FString Name;
					FString PackageName;

					// Get a unique name for the sprite
					const FString DefaultSuffix = TEXT("_Sprite");
					AssetToolsModule.Get().CreateUniqueAssetName(Texture->GetOutermost()->GetName(), DefaultSuffix, /*out*/ PackageName, /*out*/ Name);
					const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

					if (UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, ULexUISpriteData::StaticClass(), SpriteFactory))
					{
						ObjectsToSync.Add(NewAsset);
					}
				}

				if (ObjectsToSync.Num() > 0)
				{
					ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
				}
			}
			static void ConfigureTextureSettingsForSprites(TArray<FAssetData> InTextures)
			{
				// Change the compression settings and trigger a recompress
				for (UTexture2D* Texture : LexUIResolveSelectedAssets<UTexture2D>(InTextures))
				{
					ULexUISpriteData::CheckAndApplySpriteTextureSetting(Texture);
				}
			}
		};

		const FName LGUIStyleSetName = FLGUIEditorStyle::GetStyleSetName();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateSprite", "Create Sprite"),
			LOCTEXT("CreateSprite_Tooltip", "Create sprites from selected textures"),
			FSlateIcon(LGUIStyleSetName, "LGUIEditor.SpriteDataCreate"),
			FUIAction(FExecuteAction::CreateStatic(&LOCAL::CreateSpritesFromTextures, SelectedAssets)),
			NAME_None,
			EUserInterfaceActionType::Button);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ConfigureTextureForSprites", "Apply Sprite Texture Settings"),
			LOCTEXT("ConfigureTextureForSprites_Tooltip", "Set texture for sprite"),
			FSlateIcon(LGUIStyleSetName, "LGUIEditor.SpriteDataSetting"),
			FUIAction(FExecuteAction::CreateStatic(&LOCAL::ConfigureTextureSettingsForSprites, SelectedAssets)),
			NAME_None,
			EUserInterfaceActionType::Button);
	}

	static void PopulatePrefabActionMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
	{
		struct LOCAL
		{
			static void CreatePrefabVariant(TArray<FAssetData> InPrefabs)
			{
				FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

				TArray<UObject*> ObjectsToSync;

				for (ULexUIPrefab* Prefab : LexUIResolveSelectedAssets<ULexUIPrefab>(InPrefabs))
				{
					// Create the factory used to generate the prefab
					auto PrefabFactory = NewObject<ULexUIPrefabFactory>();
					PrefabFactory->SourcePrefab = Prefab;

					// Create the prefab
					FString Name;
					FString PackageName;

					// Get a unique name for the prefab
					const FString DefaultSuffix = TEXT("_Variant");
					AssetToolsModule.Get().CreateUniqueAssetName(Prefab->GetOutermost()->GetName(), DefaultSuffix, /*out*/ PackageName, /*out*/ Name);
					const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

					if (UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, ULexUIPrefab::StaticClass(), PrefabFactory))
					{
						ObjectsToSync.Add(NewAsset);
					}
				}

				if (ObjectsToSync.Num() > 0)
				{
					ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
				}
			}
		};

		const FName LGUIStyleSetName = FLGUIEditorStyle::GetStyleSetName();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreatePrefabVariant", "Create PrefabVariant"),
			LOCTEXT("CreatePrefabVariant_Tooltip", "Create variant prefab using this prefab."),
			FSlateIcon(LGUIStyleSetName, "LGUIEditor.PrefabDataAction"),
			FUIAction(FExecuteAction::CreateStatic(&LOCAL::CreatePrefabVariant, SelectedAssets)),
			NAME_None,
			EUserInterfaceActionType::Button);
	}
	static void PopulateFontActionMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
	{
		struct LOCAL
		{
			static void CreateLexUIFontFromUnrealFontAsset(TArray<FAssetData> InFonts)
			{
				FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

				TArray<UObject*> ObjectsToSync;

				for (UFontFace* Font : LexUIResolveSelectedAssets<UFontFace>(InFonts))
				{
					// Create the factory used to generate LexUIFont
					auto FontFactory = NewObject<ULexUIFontDataDistanceFieldFactory>();
					FontFactory->SourceFont = Font;

					// Create LexUIFont
					FString Name;
					FString PackageName;

					// Get a unique name for LexUIFont
					const FString DefaultSuffix = TEXT("_LexUIFont");
					AssetToolsModule.Get().CreateUniqueAssetName(Font->GetOutermost()->GetName(), DefaultSuffix, /*out*/ PackageName, /*out*/ Name);
					const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

					if (UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, ULexUIFontData_DistanceField::StaticClass(), FontFactory))
					{
						ObjectsToSync.Add(NewAsset);
					}
				}

				if (ObjectsToSync.Num() > 0)
				{
					ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
				}
			}
		};
		
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateLexUIFontDistanceField", "Create LexUIFont DistanceField"),
			LOCTEXT("CreateLexUIFontDistanceField_Tooltip", "Create LexUIFont DistanceField using this font-face."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LOCAL::CreateLexUIFontFromUnrealFontAsset, SelectedAssets)),
			NAME_None,
			EUserInterfaceActionType::Button);
	}

	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
	{
		TSharedRef<FExtender> Extender(new FExtender());

		// Run thru the assets to determine if any meet our criteria. The registry's recorded class
		// answers that without touching the package: all three classes are native and therefore
		// always loaded, so IsInstanceOf resolves them without loading anything either.
		TArray<FAssetData> Textures;
		TArray<FAssetData> Prefabs;
		TArray<FAssetData> Fonts;
		for (auto AssetIt = SelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			const FAssetData& Asset = *AssetIt;
			if (Asset.IsInstanceOf<UTexture2D>())
			{
				Textures.Add(Asset);
			}
			else if (Asset.IsInstanceOf<ULexUIPrefab>())
			{
				Prefabs.Add(Asset);
			}
			else if (Asset.IsInstanceOf<UFontFace>())
			{
				Fonts.Add(Asset);
			}
		}

		if (Textures.Num() > 0)
		{
			// Add the sprite actions sub-menu extender
			Extender->AddMenuExtension(
				"GetAssetActions",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateStatic(&FLGUIContentBrowserExtensions_Impl::CreateSpriteActionsSubMenu, Textures));
		}
		if (Prefabs.Num() > 0)
		{
			Extender->AddMenuExtension(
				"GetAssetActions",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateStatic(&FLGUIContentBrowserExtensions_Impl::CreatePrefabActionsSubMenu, Prefabs));
		}
		if (Fonts.Num() > 0)
		{
			Extender->AddMenuExtension(
				"GetAssetActions",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateStatic(&FLGUIContentBrowserExtensions_Impl::CreateFontActionsSubMenu, Fonts));
		}

		return Extender;
	}

	static TArray<FContentBrowserMenuExtender_SelectedAssets>& GetExtenderDelegates()
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		return ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	}
};

//////////////////////////////////////////////////////////////////////////
// FLexUIContentBrowserExtensions

void FLexUIContentBrowserExtensions::InstallHooks()
{
	ContentBrowserExtenderDelegate = FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&FLGUIContentBrowserExtensions_Impl::OnExtendContentBrowserAssetSelectionMenu);

	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = FLGUIContentBrowserExtensions_Impl::GetExtenderDelegates();
	CBMenuExtenderDelegates.Add(ContentBrowserExtenderDelegate);
	ContentBrowserExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
}

void FLexUIContentBrowserExtensions::RemoveHooks()
{
	if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = FLGUIContentBrowserExtensions_Impl::GetExtenderDelegates();
		CBMenuExtenderDelegates.RemoveAll([](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle; });
	}
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE