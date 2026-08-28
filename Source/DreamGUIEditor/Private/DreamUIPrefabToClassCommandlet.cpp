// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIPrefabToClassCommandlet.h"
#include "DreamUIPrefabToClassConverter.h"
#include "DreamWidgetBlueprint.h"

#include "PrefabSystem/DreamUIPrefab.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamUIPrefabToClass, Log, All);

UDreamUIPrefabToClassCommandlet::UDreamUIPrefabToClassCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UDreamUIPrefabToClassCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString SourcePath = ParamsMap.FindRef(TEXT("Source"));
	if (SourcePath.IsEmpty())
	{
		UE_LOG(LogDreamUIPrefabToClass, Error, TEXT("-Source=<package path> is required, e.g. -Source=/DreamGUI/Prefabs"));
		return 1;
	}
	const FString DestPath = ParamsMap.Contains(TEXT("Dest")) ? ParamsMap.FindRef(TEXT("Dest")) : SourcePath;
	const FString Prefix = ParamsMap.Contains(TEXT("Prefix")) ? ParamsMap.FindRef(TEXT("Prefix")) : TEXT("BP_");
	const bool bDryRun = Switches.Contains(TEXT("DryRun"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().SearchAllAssets(/*bSynchronous*/true);

	// Filtered by PATH and not by class, deliberately. The registry's idea of an asset's class comes
	// from what was written into the package, and a rename leaves entries that no longer match the
	// class they load as -- two of the shipped controls are exactly that, invisible to a ClassPaths
	// filter while LoadObject<UDreamUIPrefab> resolves them without complaint. A migration that
	// silently converts eleven of thirteen is the worst thing this tool could do, so it looks at
	// everything in the folder and lets the load decide.
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*SourcePath));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Found;
	AssetRegistryModule.Get().GetAssets(Filter, Found);

	// Deterministic order, so two runs produce the same log and the same failures.
	Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

	UE_LOG(LogDreamUIPrefabToClass, Display, TEXT("%d asset(s) under '%s' -> '%s'%s"),
		Found.Num(), *SourcePath, *DestPath, bDryRun ? TEXT(" (dry run)") : TEXT(""));

	// One throwaway world for every fidelity check; conversion must not touch a real one.
	UWorld* VerifyWorld = UWorld::CreateWorld(EWorldType::Game, false);

	int32 Converted = 0, Skipped = 0, Failed = 0, Unfaithful = 0, NotPrefabs = 0;
	TArray<UPackage*> PackagesToSave;

	for (const FAssetData& AssetData : Found)
	{
		const FString TargetPackage = FString::Printf(TEXT("%s/%s%s"), *DestPath, *Prefix, *AssetData.AssetName.ToString());

		// Never overwrite. A partial run must be safe to repeat, and a class someone has since edited
		// by hand must not be silently replaced by a fresh conversion of a stale prefab.
		if (FPackageName::DoesPackageExist(TargetPackage))
		{
			UE_LOG(LogDreamUIPrefabToClass, Warning, TEXT("skip  %s (target '%s' already exists)"),
				*AssetData.AssetName.ToString(), *TargetPackage);
			Skipped++;
			continue;
		}

		UObject* Asset = AssetData.GetAsset();
		UDreamUIPrefab* Prefab = Cast<UDreamUIPrefab>(Asset);
		if (Prefab == nullptr)
		{
			// Not a prefab at all -- reported rather than skipped in silence, so the arithmetic at the
			// end accounts for every asset in the folder.
			UE_LOG(LogDreamUIPrefabToClass, Display, TEXT("--    %s (not a prefab: %s)"),
				*AssetData.AssetName.ToString(), Asset != nullptr ? *Asset->GetClass()->GetName() : TEXT("did not load"));
			NotPrefabs++;
			continue;
		}

		DreamUIPrefabToClass::FConversionResult Result = DreamUIPrefabToClass::ConvertPrefab(Prefab, TargetPackage);
		if (!Result.IsSuccess())
		{
			UE_LOG(LogDreamUIPrefabToClass, Error, TEXT("FAIL  %s"), *Result.ToString());
			Failed++;
			continue;
		}
		for (const FString& Warning : Result.Warnings)
		{
			UE_LOG(LogDreamUIPrefabToClass, Warning, TEXT("      %s: %s"), *AssetData.AssetName.ToString(), *Warning);
		}

		// Verified before it is kept, not after. An unfaithful conversion that got saved anyway is
		// worse than one that failed, because it looks finished.
		TArray<FString> Differences;
		const bool bFaithful = DreamUIPrefabToClass::VerifyFidelity(Prefab, Result.Blueprint->GeneratedClass, VerifyWorld, Differences);
		if (!bFaithful)
		{
			Unfaithful++;
			UE_LOG(LogDreamUIPrefabToClass, Error, TEXT("DIFF  %s (%d difference(s)), not saved:"), *AssetData.AssetName.ToString(), Differences.Num());
			for (const FString& Difference : Differences)
			{
				UE_LOG(LogDreamUIPrefabToClass, Error, TEXT("        %s"), *Difference);
			}
			continue;
		}

		UE_LOG(LogDreamUIPrefabToClass, Display, TEXT("ok    %s -> %s (%d widgets)"),
			*AssetData.AssetName.ToString(), *TargetPackage, Result.WidgetCount);
		Converted++;
		if (!bDryRun)
		{
			PackagesToSave.Add(Result.Blueprint->GetOutermost());
		}
	}

	if (!PackagesToSave.IsEmpty())
	{
		// Saved in one pass at the end rather than per asset, so a failure part-way through leaves
		// nothing half-written across the set.
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty*/false);
		if (!bSaved)
		{
			UE_LOG(LogDreamUIPrefabToClass, Error, TEXT("Converted %d, but saving failed."), Converted);
			VerifyWorld->DestroyWorld(false);
			return 1;
		}
	}

	VerifyWorld->DestroyWorld(false);

	// Every asset the folder held is accounted for in one line; a count that does not add up to the
	// number found is the signal that something was dropped.
	UE_LOG(LogDreamUIPrefabToClass, Display, TEXT("done: %d found = %d converted + %d skipped + %d failed + %d unfaithful + %d not-prefabs"),
		Found.Num(), Converted, Skipped, Failed, Unfaithful, NotPrefabs);
	return (Failed > 0 || Unfaithful > 0) ? 1 : 0;
}
