// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"
#include "Misc/PackageName.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

/*
 * The plugin's Niagara systems are generated from the text under DFX/ by DreamFX, which stamps each asset's package with
 * where its source was: relative to the source root, and as the full path on the machine that built it. The full path has
 * no place in a repository others check out, so it is taken off before the assets are committed:
 *
 *     UnrealEditor-Cmd.exe <host>.uproject -ExecCmds="DreamGUI.GeneratedAssets.ForgetFullPaths /DreamGUI Exit" -unattended -nullrhi
 *
 * Every package under the given content paths that carries the stamp is loaded, the stamp removed and the package saved;
 * nothing else of it changes. DreamFX still finds each asset's source by the relative stamp. A test holds the committed
 * assets to it (DreamGUI.Niagara.TheGeneratedSystemsNameNoMachinesPath).
 */
DEFINE_LOG_CATEGORY_STATIC(LogDreamGUIGeneratedAssets, Log, All);

namespace DreamGeneratedAssetPathsLocal
{
	const TCHAR* const FullPathKey = TEXT("DreamFX.SourceFullPath");

	bool ForgetFullPath(UPackage* InPackage, TArray<FString>& OutLog)
	{
		FMetaData& MetaData = InPackage->GetMetaData();
		bool bChanged = false;
		TArray<UObject*> Objects;
		GetObjectsWithPackage(InPackage, Objects, EGetObjectsFlags::None);
		for (UObject* Object : Objects)
		{
			if (MetaData.HasValue(Object, FullPathKey))
			{
				MetaData.RemoveValue(Object, FullPathKey);
				bChanged = true;
			}
		}
		if (!bChanged)
		{
			return true;
		}
		UObject* Asset = InPackage->FindAssetInPackage();
		const FString Filename = FPackageName::LongPackageNameToFilename(InPackage->GetName(), FPackageName::GetAssetPackageExtension());
		InPackage->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;
		Args.Error = GWarn;
		const FSavePackageResultStruct Result = UPackage::Save(InPackage, Asset, *Filename, Args);
		OutLog.Add(FString::Printf(TEXT("%s %s"), Result.IsSuccessful() ? TEXT("saved") : TEXT("could not save"), *Filename));
		return Result.IsSuccessful();
	}

	void Run(const TArray<FString>& InArgs)
	{
		TArray<FString> Log;
		bool bOk = true;
		for (const FString& Arg : InArgs)
		{
			if (!Arg.StartsWith(TEXT("/")))
			{
				continue;
			}
			TArray<FString> Files;
			const FString Directory = FPackageName::LongPackageNameToFilename(Arg / TEXT(""), TEXT(""));
			IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.uasset"), true, false);
			for (const FString& File : Files)
			{
				FString PackageName;
				if (!FPackageName::TryConvertFilenameToLongPackageName(File, PackageName))
				{
					continue;
				}
				if (UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None))
				{
					bOk &= ForgetFullPath(Package, Log);
				}
			}
		}
		for (const FString& Line : Log)
		{
			UE_LOG(LogDreamGUIGeneratedAssets, Display, TEXT("%s"), *Line);
		}
		UE_LOG(LogDreamGUIGeneratedAssets, Display, TEXT("DreamGUI.GeneratedAssets: ForgetFullPaths %s"), bOk ? TEXT("succeeded") : TEXT("failed"));
		if (InArgs.ContainsByPredicate([](const FString& InArg) { return InArg.Equals(TEXT("Exit"), ESearchCase::IgnoreCase); }))
		{
			RequestEngineExit(TEXT("DreamGUI.GeneratedAssets.ForgetFullPaths finished"));
		}
	}
}

static FAutoConsoleCommand GDreamGeneratedAssetsForgetFullPaths(
	TEXT("DreamGUI.GeneratedAssets.ForgetFullPaths"),
	TEXT("Remove the source's full path DreamFX stamps on a generated asset, from every package under the content paths given. Pass Exit to quit afterwards."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DreamGeneratedAssetPathsLocal::Run));
