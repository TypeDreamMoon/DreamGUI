// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamOnDiskFixture.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace DreamUISourceClassResolverTestLocal
{
	/** The registry deliberately refuses /Temp; give this saved asset its own real, short-lived mount. */
	struct FScopedSourceAssetMount
	{
		FScopedSourceAssetMount()
		{
			const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			RootPath = TEXT("/DreamGUIResolver_") + Suffix + TEXT("/");
			Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("ResolverAssets_") + Suffix)) + TEXT("/");
			FPaths::NormalizeDirectoryName(Directory);
			bCreatedDirectory = IFileManager::Get().MakeDirectory(*Directory, true);
			if (bCreatedDirectory) FPackageName::RegisterMountPoint(RootPath, Directory + TEXT("/"));
		}
		~FScopedSourceAssetMount()
		{
			if (bCreatedDirectory)
			{
				FPackageName::UnRegisterMountPoint(RootPath, Directory + TEXT("/"));
				IFileManager::Get().DeleteDirectory(*Directory, false, false);
			}
		}
		bool Place(DreamOnDiskFixture::FScopedOnDiskPackage& Disk) const
		{
			if (!bCreatedDirectory) return false;
			const FString NewPackageName = RootPath + Disk.AssetName;
			if (!Disk.Package->Rename(*NewPackageName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional)) return false;
			Disk.PackageName = NewPackageName;
			Disk.FileName = FPackageName::LongPackageNameToFilename(NewPackageName, FPackageName::GetAssetPackageExtension());
			return true;
		}
		FString RootPath;
		FString Directory;
		bool bCreatedDirectory = false;
	};

	struct FScopedSourceFiles
	{
		FScopedSourceFiles()
		{
			const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			const FString Directory = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests")));
			OldPath = FPaths::Combine(Directory, TEXT("ResolverOld_") + Suffix + TEXT(".dui"));
			NewPath = FPaths::Combine(Directory, TEXT("ResolverNew_") + Suffix + TEXT(".dui"));
			FPaths::NormalizeFilename(OldPath);
			FPaths::NormalizeFilename(NewPath);
		}

		~FScopedSourceFiles()
		{
			for (const FString& Path : { OldPath, NewPath })
			{
				FDreamUISourceWatcher::NoteImports(Path, TArray<FString>());
				IFileManager::Get().Delete(*Path, false, true, true);
			}
		}

		static bool Write(const FString& InPath, const FString& InText)
		{
			return IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), true)
				&& FFileHelper::SaveStringToFile(InText, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FString OldPath;
		FString NewPath;
	};
	/** Save validation runs on the next editor tick; keep its file and mount alive until that tick. */
	struct FScopedResolverDiskFixture
	{
		explicit FScopedResolverDiskFixture(const TCHAR* AssetName) : Disk(AssetName) {}
		~FScopedResolverDiskFixture()
		{
			if (UObject* Asset = RegisteredAsset.ResolveObject()) FAssetRegistryModule::AssetDeleted(Asset);
		}
		FScopedSourceFiles Sources;
		FScopedSourceAssetMount Mount;
		DreamOnDiskFixture::FScopedOnDiskPackage Disk;
		FSoftObjectPath RegisteredAsset;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamUISourceClassResolverChangedSourceAfterUnloadTest,
	"DreamGUI.Text.SourceClassResolver.AChangedSourceFileStillResolvesItsSavedBlueprintAfterUnload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISourceClassResolverChangedSourceAfterUnloadTest::RunTest(const FString&)
{
	using namespace DreamUISourceClassResolverTestLocal;
	const FString AssetName = TEXT("SourceResolverChanged_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const TSharedRef<FScopedResolverDiskFixture> Fixture = MakeShared<FScopedResolverDiskFixture>(*AssetName);
	FScopedSourceFiles& Sources = Fixture->Sources;
	// No class line: this is the supported use "...dui" as Row fallback, not a path to a class.
	const FString NewText = TEXT("Widget Root {\n    Widget NewChild { }\n}\n");
	if (!TestTrue(TEXT("the original source was written"), FScopedSourceFiles::Write(Sources.OldPath, TEXT("Widget Root { }\n")))
		|| !TestTrue(TEXT("the replacement source was written"), FScopedSourceFiles::Write(Sources.NewPath, NewText))) return false;

	DreamOnDiskFixture::FScopedOnDiskPackage& Disk = Fixture->Disk;
	if (!TestTrue(TEXT("the disk fixture is under a registry-scannable mount"), Fixture->Mount.Place(Disk))) return false;
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamTextUserWidget::StaticClass(), Disk.Package, FName(*AssetName), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("a real text widget Blueprint was created"), Blueprint)) return false;
	const FSoftObjectPath AssetPath(Blueprint);
	FAssetRegistryModule::AssetCreated(Blueprint);
	Fixture->RegisteredAsset = AssetPath;
	ON_SCOPE_EXIT
	{
		// Its queued validation must run while the saved asset's filename is still mounted.
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Fixture]() {}, 0.1f));
	};

	FString Error;
	if (!TestTrue(TEXT("Set Source File assigns and compiles the old source"),
		DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, Sources.OldPath))
		|| !TestTrue(TEXT("the original asset saved"), Disk.Save(Blueprint, Error))) { AddInfo(Error); return false; }
	TestEqual(TEXT("the first lookup indexes the old source"),
		FDreamUISourceWatcher::FindClassForSource(Sources.OldPath), static_cast<UClass*>(Blueprint->GeneratedClass));

	// Use the same operation as the designer's Set Source File command, including its compile.
	if (!TestTrue(TEXT("Set Source File assigns and compiles the replacement"),
		DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, Sources.NewPath))
		|| !TestTrue(TEXT("the changed asset saved"), Disk.Save(Blueprint, Error))) { AddInfo(Error); return false; }
	TestTrue(TEXT("the new hierarchy was compiled before unloading"), Blueprint->Status == BS_UpToDate);

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the replacement file parses without an explicit class"),
		FDreamUISourceFile::Parse(NewText, Sources.NewPath, Ast, Diagnostics, FDreamUISourceFile::MakeFileImportReader()))) return false;
	TestTrue(TEXT("the resolver must supply the missing class line"), Ast.ClassPath.IsEmpty());
	IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
	Registry.ScanFilesSynchronous({ Disk.FileName }, true);
	if (!TestTrue(TEXT("the saved asset was actually scanned from disk before unload"),
		Registry.GetAssetByObjectPath(AssetPath, true).IsValid())) return false;

	TWeakObjectPtr<UDreamWidgetBlueprint> BeforeUnload = Blueprint;
	UPackage* PackageToUnload = Disk.Package;
	PackageToUnload->RemoveFromRoot();
	Disk.Package = nullptr; // Unload may collect it; the on-disk fixture must not keep a stale pointer.
	Blueprint = nullptr;
	FText UnloadError;
	if (!TestTrue(TEXT("the editor's normal package unload succeeded"),
		UPackageTools::UnloadPackages({ PackageToUnload }, UnloadError))) { AddInfo(UnloadError.ToString()); return false; }
	if (!TestFalse(TEXT("the asset really left memory before the fallback lookup"), BeforeUnload.IsValid())) return false;
	if (!TestTrue(TEXT("the saved asset remains discoverable without loading it"),
		Registry.GetAssetByObjectPath(AssetPath, true).IsValid())) return false;

	TFunction<UClass*(const FString&)>& Resolver = FDreamUITextBuilder::SourceClassResolver();
	if (!TestTrue(TEXT("the production file-alias resolver is installed"), static_cast<bool>(Resolver))) return false;
	UClass* Resolved = Resolver(Sources.NewPath);
	TestNotNull(TEXT("the changed source resolves its saved Blueprint while that asset is unloaded"), Resolved);

	// A separate control proves the file contains the changed Source File, even if the index refused it.
	UDreamWidgetBlueprint* Loaded = Cast<UDreamWidgetBlueprint>(AssetPath.TryLoad());
	if (!TestNotNull(TEXT("the changed asset can be reopened normally from disk"), Loaded)) return false;
	Disk.Package = Loaded->GetOutermost();
	Disk.Package->AddToRoot();
	const FString SavedSource = UDreamTextUserWidget::ResolveDuiFilePath(DreamUITextAuthoring::GetAuthoredSourcePath(Loaded));
	TestTrue(TEXT("the replacement Source File survived serialization"),
		SavedSource.Equals(Sources.NewPath, ESearchCase::IgnoreCase));
	TestEqual(TEXT("the saved hierarchy contains the replacement child"), Loaded->WidgetTree->CountWidgets(), 2);
	TestEqual(TEXT("with the reopened asset loaded the same fallback resolves"),
		Resolver(Sources.NewPath), static_cast<UClass*>(Loaded->GeneratedClass));
	if (Resolved != nullptr)
	{
		TestEqual(TEXT("the unloaded lookup returned this asset's class"), Resolved, static_cast<UClass*>(Loaded->GeneratedClass));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamUISourceClassResolverStableSharedSourceTest,
	"DreamGUI.Text.SourceClassResolver.AnUnrelatedFileLookupKeepsTheSameSharedSourceClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISourceClassResolverStableSharedSourceTest::RunTest(const FString&)
{
	using namespace DreamUISourceClassResolverTestLocal;
	FScopedSourceFiles Sources;
	if (!TestTrue(TEXT("the shared source was written"), FScopedSourceFiles::Write(Sources.OldPath, TEXT("Widget SharedRoot { }\n")))
		|| !TestTrue(TEXT("the unrelated source was written"), FScopedSourceFiles::Write(Sources.NewPath, TEXT("Widget OtherRoot { }\n")))) return false;

	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString FirstName = TEXT("ResolverShared_A_") + Suffix;
	const FString LastName = TEXT("ResolverShared_Z_") + Suffix;
	const FString OtherName = TEXT("ResolverOther_") + Suffix;
	DreamOnDiskFixture::FScopedOnDiskPackage FirstDisk(*FirstName);
	DreamOnDiskFixture::FScopedOnDiskPackage LastDisk(*LastName);
	DreamOnDiskFixture::FScopedOnDiskPackage OtherDisk(*OtherName);
	TArray<UDreamWidgetBlueprint*> RegisteredAssets;
	ON_SCOPE_EXIT
	{
		for (UDreamWidgetBlueprint* Asset : RegisteredAssets) FAssetRegistryModule::AssetDeleted(Asset);
	};
	auto CreateBlueprint = [&RegisteredAssets](UPackage* Package, const FString& Name)
	{
		UDreamWidgetBlueprint* Asset = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamTextUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		if (Asset != nullptr)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			RegisteredAssets.Add(Asset);
		}
		return Asset;
	};
	UDreamWidgetBlueprint* First = CreateBlueprint(FirstDisk.Package, FirstName);
	UDreamWidgetBlueprint* Last = CreateBlueprint(LastDisk.Package, LastName);
	if (!TestNotNull(TEXT("the first shared-source Blueprint was created"), First)
		|| !TestNotNull(TEXT("the other shared-source Blueprint was created"), Last)) return false;
	if (!TestTrue(TEXT("the first Blueprint compiles from the shared source"), DreamUITextAuthoring::SetAuthoredSourcePath(First, Sources.OldPath))
		|| !TestTrue(TEXT("the other Blueprint also compiles from the shared source"), DreamUITextAuthoring::SetAuthoredSourcePath(Last, Sources.OldPath))) return false;
	TestTrue(TEXT("sharing a source file is accepted for both classes"), First->Status == BS_UpToDate && Last->Status == BS_UpToDate);
	TestTrue(TEXT("the expected class has the first asset path"), First->GetPathName() < Last->GetPathName());

	TFunction<UClass*(const FString&)>& Resolver = FDreamUITextBuilder::SourceClassResolver();
	if (!TestTrue(TEXT("the production file-alias resolver is installed"), static_cast<bool>(Resolver))) return false;
	// Sharing a source is supported; without a class line the editor warns and chooses a stable asset path.
	AddExpectedMessagePlain(TEXT("2 Blueprints are built from"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	UClass* InitiallySelected = Resolver(Sources.OldPath);
	TestEqual(TEXT("a shared source initially selects the first asset path"), InitiallySelected, static_cast<UClass*>(First->GeneratedClass));

	// Make this asset only after the first lookup, so its file cannot already have an indexed answer.
	UDreamWidgetBlueprint* Other = CreateBlueprint(OtherDisk.Package, OtherName);
	if (!TestNotNull(TEXT("an unrelated Blueprint was created afterwards"), Other)
		|| !TestTrue(TEXT("it compiles from the unrelated source"), DreamUITextAuthoring::SetAuthoredSourcePath(Other, Sources.NewPath))) return false;
	TestTrue(TEXT("the unrelated source compiles cleanly"), Other->Status == BS_UpToDate);
	TestEqual(TEXT("the new source resolves its own class"), Resolver(Sources.NewPath), static_cast<UClass*>(Other->GeneratedClass));
	TestEqual(TEXT("an unrelated lookup cannot change which shared-source class an alias means"), Resolver(Sources.OldPath), InitiallySelected);
	return true;
}

#endif
