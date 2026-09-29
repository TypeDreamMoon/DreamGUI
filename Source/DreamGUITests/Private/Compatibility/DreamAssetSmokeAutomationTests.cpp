// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DreamPackageLoadCheck.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAssetSmokeTest,
	"DreamGUI.Assets.EveryPluginAndProjectPackageLoadsWithEveryTypeFoundAndEveryBlueprintCompiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Every package under /DreamGUI -- the plugin's own content -- and under /Game -- the project's, which in
 * the test host is the old-asset fixtures -- loaded, and every Blueprint in them compiled. A class that is
 * missing, a struct that no longer loads, a Blueprint that stopped compiling cleanly: each is logged as a
 * warning or an error while the package loads, or reported by the compiler, and fails the test here, where it
 * would otherwise surface as a broken asset in somebody's project.
 */
bool FDreamAssetSmokeTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(/*bSynchronousSearch*/true);

	TArray<FName> Packages;
	for (const TCHAR* Root : { TEXT("/DreamGUI"), TEXT("/Game") })
	{
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(Root), Assets, /*bRecursive*/true);
		TSet<FName> Unique;
		for (const FAssetData& Asset : Assets)
		{
			Unique.Add(Asset.PackageName);
		}
		TArray<FName> Sorted = Unique.Array();
		Sorted.Sort(FNameLexicalLess());
		AddInfo(FString::Printf(TEXT("%s: %d packages"), Root, Sorted.Num()));
		if (FCString::Strcmp(Root, TEXT("/DreamGUI")) == 0)
		{
			TestTrue(TEXT("the plugin has content to load"), Sorted.Num() > 0);
		}
		Packages.Append(Sorted);
	}

	for (const FName Package : Packages)
	{
		const DreamPackageLoadCheck::FResult Loaded = DreamPackageLoadCheck::LoadAndCompile(Package.ToString());
		TestNotNull(*FString::Printf(TEXT("%s loads"), *Package.ToString()), Loaded.Package);
		for (const FString& Problem : Loaded.Problems)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Package.ToString(), *Problem));
		}
		// A compiler warning fails too. The plugin's own Blueprints compile without one -- the last four legacy
		// presets that named two widgets alike were renamed -- so a warning here is new, and the kind a class
		// move produces (a pin whose type went missing, a node that no longer resolves) often starts as one.
		for (const FString& Warning : Loaded.CompilerWarnings)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Package.ToString(), *Warning));
		}
	}
	return true;
}

#endif
