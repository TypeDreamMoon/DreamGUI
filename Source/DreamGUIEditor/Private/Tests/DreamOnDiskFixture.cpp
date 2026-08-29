// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamOnDiskFixture.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace DreamOnDiskFixture
{
	FScopedOnDiskPackage::FScopedOnDiskPackage(const TCHAR* InAssetName)
	{
		AssetName = InAssetName;
		PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InAssetName);
		FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		Package = CreatePackage(*PackageName);
		Package->AddToRoot();
	}

	FScopedOnDiskPackage::~FScopedOnDiskPackage()
	{
		if (Package != nullptr)
		{
			Package->RemoveFromRoot();
		}
		IFileManager::Get().Delete(*FileName, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
	}

	bool FScopedOnDiskPackage::Save(UObject* InAsset, FString& OutError)
	{
		if (InAsset == nullptr)
		{
			OutError = TEXT("nothing to save");
			return false;
		}
		if (!InAsset->IsIn(Package))
		{
			OutError = FString::Printf(TEXT("'%s' is not in the package being saved"), *InAsset->GetPathName());
			return false;
		}
		// Standalone|Public is what an asset carries; without it the save writes a package whose only
		// export is unreachable and the reload hands back nothing, which reads as a serialization bug.
		InAsset->SetFlags(RF_Public | RF_Standalone);
		Package->MarkPackageDirty();

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		// A test is not a slow task; the progress dialog it would push has no frame to draw in.
		Args.bSlowTask = false;
		const FSavePackageResultStruct Result = UPackage::Save(Package, InAsset, *FileName, Args);
		if (!Result.IsSuccessful())
		{
			OutError = FString::Printf(TEXT("saving '%s' failed"), *FileName);
			return false;
		}
		if (!IFileManager::Get().FileExists(*FileName))
		{
			OutError = FString::Printf(TEXT("save reported success but '%s' is not there"), *FileName);
			return false;
		}
		return true;
	}

	UObject* FScopedOnDiskPackage::Reload(FString& OutError)
	{
		if (Package == nullptr)
		{
			OutError = TEXT("there is no package");
			return nullptr;
		}
		// Vacate the name. LoadPackage answers from memory when a package of that name is loaded, so
		// leaving this in place makes the whole fixture a no-op that still passes every assertion.
		UPackage* Vacated = Package;
		Package = nullptr;
		Vacated->RemoveFromRoot();
		Vacated->ClearFlags(RF_Standalone);
		const FString AwayName = PackageName + TEXT("_BeforeReload");
		Vacated->Rename(*AwayName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		// Not a tidiness pass: whatever survives here stays reachable by name lookups, and a test
		// comparing "the loaded one" against "the built one" wants them to be different objects.
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		UPackage* Loaded = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (Loaded == nullptr)
		{
			OutError = FString::Printf(TEXT("loading '%s' produced no package"), *PackageName);
			return nullptr;
		}
		Loaded->AddToRoot();
		Package = Loaded;

		UObject* Asset = FindObject<UObject>(Loaded, *AssetName);
		if (Asset == nullptr)
		{
			OutError = FString::Printf(TEXT("'%s' is not in the reloaded package"), *AssetName);
			return nullptr;
		}
		return Asset;
	}
}

#endif
