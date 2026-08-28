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

	FARFilter Filter;
	Filter.ClassPaths.Add(UDreamUIPrefab::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*SourcePath));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Found;
	AssetRegistryModule.Get().GetAssets(Filter, Found);

	// Deterministic order, so two runs produce the same log and the same failures.
	Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

	UE_LOG(LogDreamUIPrefabToClass, Display, TEXT("%d prefab(s) under '%s' -> '%s'%s"),
		Found.Num(), *SourcePath, *DestPath, bDryRun ? TEXT(" (dry run)") : TEXT(""));

	// One throwaway world for every fidelity check; conversion must not touch a real one.
	UWorld* VerifyWorld = UWorld::CreateWorld(EWorldType::Game, false);

	int32 Converted = 0, Skipped = 0, Failed = 0, Unfaithful = 0;
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

		UDreamUIPrefab* Prefab = Cast<UDreamUIPrefab>(AssetData.GetAsset());
		if (!IsValid(Prefab))
		{
			UE_LOG(LogDreamUIPrefabToClass, Error, TEXT("FAIL  %s (did not load)"), *AssetData.AssetName.ToString());
			Failed++;
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

	UE_LOG(LogDreamUIPrefabToClass, Display, TEXT("done: %d converted, %d skipped, %d failed, %d unfaithful"),
		Converted, Skipped, Failed, Unfaithful);
	return (Failed > 0 || Unfaithful > 0) ? 1 : 0;
}
