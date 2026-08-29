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
		// What every asset factory does after CreatePackage, and it is load-bearing rather than
		// ceremonial: UPackage::IsFullyLoaded infers "yes" for a fresh package ONLY while no file of
		// that name exists, and SavePackage refuses a package that is not fully loaded so it cannot
		// clobber content it never read. So the very first run of these tests saved fine and every
		// run after it -- with the file already on disk -- refused, which read as an order-dependent
		// flake in whichever test happened to go first.
		Package->MarkAsFullyLoaded();
	}

	FScopedOnDiskPackage::~FScopedOnDiskPackage()
	{
		if (Package != nullptr)
		{
			// A reloaded package still holds its linker, and the linker still holds the file open, so
			// deleting without this leaves the .uasset behind in Saved/ -- and the next run then reads
			// a file this run wrote.
			ResetLoaders(Package);
			Package->RemoveFromRoot();
		}
		if (IFileManager::Get().FileExists(*FileName)
			&& !IFileManager::Get().Delete(*FileName, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true))
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s].%d Could not remove the test asset '%s'; it will be left on disk."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FileName);
		}
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
		// Deliberately NOT SAVE_NoError: a save that says only "failed" is the kind of silence that
		// costs an afternoon. Let SavePackage log its own reason, and carry the code out with us.
		Args.SaveFlags = SAVE_None;
		// But NOT through GError, which is the default. GError is FOutputDeviceError: anything logged
		// to it in an unattended editor is appError, so asking SavePackage to explain itself took the
		// whole test run down with an access violation instead of failing one assertion.
		Args.Error = GWarn;
		// A test is not a slow task; the progress dialog it would push has no frame to draw in.
		Args.bSlowTask = false;
		const FSavePackageResultStruct Result = UPackage::Save(Package, InAsset, *FileName, Args);
		if (!Result.IsSuccessful())
		{
			OutError = FString::Printf(TEXT("saving '%s' failed with ESavePackageResult %d"), *FileName, (int32)Result.Result);
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
