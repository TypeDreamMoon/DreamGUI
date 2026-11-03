// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Commandlets/DreamUIConvertBlueprintsCommandlet.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIBlueprintConverter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "FileHelpers.h"
#include "UObject/Package.h"

UDreamUIConvertBlueprintsCommandlet::UDreamUIConvertBlueprintsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDreamUIConvertBlueprintsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);
	const bool bDryRun = Switches.Contains(TEXT("DryRun"));
	const bool bAllowLoss = Switches.Contains(TEXT("AllowLoss"));

	TArray<FString> ObjectPaths;
	if (const FString* Assets = Values.Find(TEXT("Assets")))
	{
		Assets->ParseIntoArray(ObjectPaths, TEXT(","), /*InCullEmpty*/true);
	}
	if (const FString* Path = Values.Find(TEXT("Path")))
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.SearchAllAssets(/*bSynchronousSearch*/true);
		FARFilter Filter;
		Filter.ClassPaths.Add(UDreamWidgetBlueprint::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(**Path));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		for (const FAssetData& Asset : Found)
		{
			ObjectPaths.AddUnique(Asset.GetObjectPathString());
		}
	}
	if (ObjectPaths.IsEmpty())
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Nothing to convert: pass -Assets=/Game/UI/WBP_A,... or -Path=/Game/UI."));
		return 1;
	}

	int32 Converted = 0;
	int32 Skipped = 0;
	for (const FString& ObjectPath : ObjectPaths)
	{
		UDreamWidgetBlueprint* Blueprint = LoadObject<UDreamWidgetBlueprint>(nullptr, *ObjectPath);
		if (Blueprint == nullptr)
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("%s: not a DreamUI Widget Blueprint, or it did not load."), *ObjectPath);
			++Skipped;
			continue;
		}
		FDreamUIConversionReport Report;
		const bool bOk = bDryRun
			? FDreamUIBlueprintConverter::Preflight(Blueprint, FString(), Report) && (Report.Residue.IsEmpty() || bAllowLoss)
			: FDreamUIBlueprintConverter::Convert(Blueprint, FString(), bAllowLoss, Report);
		UE_LOG(DreamGUIEditor, Display, TEXT("%s"), *Report.ToString());
		if (!bOk)
		{
			++Skipped;
			continue;
		}
		if (!bDryRun)
		{
			TArray<UPackage*> Packages{ Blueprint->GetOutermost() };
			if (UObject* Backup = Report.BackupPath.IsEmpty() ? nullptr : FindObject<UObject>(nullptr, *Report.BackupPath))
			{
				Packages.Add(Backup->GetOutermost());
			}
			if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/false))
			{
				UE_LOG(DreamGUIEditor, Error, TEXT("%s: converted, but the packages could not be saved."), *ObjectPath);
				++Skipped;
				continue;
			}
			++Converted;
		}
	}
	UE_LOG(DreamGUIEditor, Display, TEXT("DreamUIConvertBlueprints: %d converted, %d skipped or failed, of %d%s."),
		Converted, Skipped, ObjectPaths.Num(), bDryRun ? TEXT(" (dry run)") : TEXT(""));
	return Skipped > 0 ? 1 : 0;
}
