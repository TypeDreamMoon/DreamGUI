// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUILocalizationManifest.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

/*
 * The extraction manifest: what a .dui compile leaves behind for a localization tool to read.
 *
 * The first test holds the builder to the list it hands out -- key, source, node path, type and the key a renamed
 * node used to have -- with nothing on disk. The second holds the compiler to writing it where DreamLoc looks for it,
 * through a real file under the project's DUI/ folder (only a file under a root has a place to put its manifest),
 * and to leaving the file alone when a recompile has nothing new to say.
 */

namespace DreamUILocalizationManifestTestLocal
{
	FString MakeSource(const FString& InClassPath)
	{
		return FString::Join(TArray<FString>{
			FString::Printf(TEXT("class %s"), *InClassPath),
			TEXT(""),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("        Text = \"设置\""),
			TEXT("    }"),
			TEXT("    Widget Footer {"),
			TEXT("        Text Apply (was: ApplyLabel) {"),
			TEXT("            Text = \"应用 {Count} 项更改\""),
			TEXT("        }"),
			TEXT("        Text Ok {"),
			TEXT("            Text = \"确定\" @key(\"Settings.Ok\")"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}"),
		}, TEXT("\n"));
	}

	FString ExpectedEntries()
	{
		return FString(TEXT("  \"entries\": [\n"))
			+ TEXT("    { \"key\": \"Title.Text\", \"src\": \"设置\", \"at\": \"Title\", \"kind\": \"Text\" },\n")
			+ TEXT("    { \"key\": \"Apply.Text\", \"src\": \"应用 {Count} 项更改\", \"at\": \"Footer/Apply\", \"kind\": \"Text\", \"renamedFrom\": \"ApplyLabel.Text\" },\n")
			+ TEXT("    { \"key\": \"Settings.Ok\", \"src\": \"确定\", \"at\": \"Footer/Ok\", \"kind\": \"Text\" }\n")
			+ TEXT("  ]\n}\n");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUILocalizationListTest,
	"DreamGUI.Text.TheBuilderListsEveryLocalizableTextForTranslators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUILocalizationListTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILocalizationManifestTestLocal;
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the file parses"), FDreamUISourceFile::Parse(MakeSource(TEXT("/Game/UI/WBP_Settings")), TEXT("Settings.dui"), Ast, Diagnostics)))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}

	FDreamUILocalization Localization;
	Localization.Texts.AddDefaulted(); // Build resets what it is given.
	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetTree* Tree = FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings, nullptr, nullptr, &Localization);
	if (!TestNotNull(TEXT("and builds"), Tree))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}

	TestEqual(TEXT("the namespace is the class line"), Localization.Namespace, FString(TEXT("/Game/UI/WBP_Settings")));
	if (!TestEqual(TEXT("three texts"), Localization.Texts.Num(), 3))
	{
		return false;
	}
	const FDreamUILocalizableText& Title = Localization.Texts[0];
	TestEqual(TEXT("Title key"), Title.Key, FString(TEXT("Title.Text")));
	TestEqual(TEXT("Title source"), Title.Source, FString(TEXT("设置")));
	TestEqual(TEXT("Title path leaves the root out"), Title.NodePath, FString(TEXT("Title")));
	TestEqual(TEXT("Title type"), Title.NodeType, FString(TEXT("Text")));
	TestTrue(TEXT("Title was not renamed"), Title.RenamedFrom.IsEmpty());

	const FDreamUILocalizableText& Apply = Localization.Texts[1];
	TestEqual(TEXT("Apply key"), Apply.Key, FString(TEXT("Apply.Text")));
	TestEqual(TEXT("Apply path"), Apply.NodePath, FString(TEXT("Footer/Apply")));
	TestEqual(TEXT("Apply's old key, from (was: ApplyLabel)"), Apply.RenamedFrom, FString(TEXT("ApplyLabel.Text")));

	const FDreamUILocalizableText& Ok = Localization.Texts[2];
	TestEqual(TEXT("an @key is the key"), Ok.Key, FString(TEXT("Settings.Ok")));
	TestEqual(TEXT("Ok path"), Ok.NodePath, FString(TEXT("Footer/Ok")));

	const FString Json = DreamUILocalizationManifest::ToJson(TEXT("DUI/UI/Settings.dui"), FString(), Localization);
	const FString Expected = FString(TEXT("{\n  \"version\": 1,\n  \"provider\": \"DreamGUI\",\n  \"source\": \"DUI/UI/Settings.dui\",\n"))
		+ TEXT("  \"namespace\": \"/Game/UI/WBP_Settings\",\n") + ExpectedEntries();
	TestEqual(TEXT("the manifest's text"), Json, Expected);

	const FString PluginJson = DreamUILocalizationManifest::ToJson(TEXT("DUI/Common/Dialog.dui"), TEXT("Plugin.DreamGUI"), FDreamUILocalization());
	TestTrue(TEXT("a plugin's manifest names its root"), PluginJson.Contains(TEXT("\"root\": \"Plugin.DreamGUI\",\n")));
	TestTrue(TEXT("and a file without texts has an empty list"), PluginJson.EndsWith(TEXT("\"entries\": []\n}\n")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUILocalizationManifestCompileTest,
	"DreamGUI.WidgetBlueprint.CompilingADuiWritesItsLocalizationManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUILocalizationManifestCompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILocalizationManifestTestLocal;

	// A file under the project's own DUI/ folder, made for this run and gone after it, folder included when the run
	// made the folder.
	const FString DuiDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / DreamUIPaths::SourceDirectoryName);
	const bool bMadeDuiDirectory = !IFileManager::Get().DirectoryExists(*DuiDirectory);
	// Hex digits only: the name goes into the file's `class` line, where the '-' of a short guid is not a path character.
	const FString Name = FString::Printf(TEXT("LocManifest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	FString DuiFile = DuiDirectory / TEXT("DreamGUITests") / (Name + DreamUIPaths::SourceExtension);
	FPaths::NormalizeFilename(DuiFile);
	const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), *Name);

	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*DuiFile, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
		IFileManager::Get().DeleteDirectory(*(DuiDirectory / TEXT("DreamGUITests")), /*RequireExists*/ false, /*Tree*/ false);
		if (bMadeDuiDirectory)
		{
			IFileManager::Get().DeleteDirectory(*DuiDirectory, /*RequireExists*/ false, /*Tree*/ false);
		}
	};
	if (!TestTrue(TEXT("the .dui is written"), FFileHelper::SaveStringToFile(MakeSource(PackageName), *DuiFile)))
	{
		return false;
	}

	FString ManifestPath;
	FString Source;
	FString RootToken;
	if (!TestTrue(TEXT("a file under the project's DUI/ has a manifest path"), DreamUILocalizationManifest::GetPaths(DuiFile, ManifestPath, Source, RootToken)))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*ManifestPath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
	};
	TestEqual(TEXT("named the way DLoc mirrors it"), Source, FString::Printf(TEXT("DUI/DreamGUITests/%s.dui"), *Name));
	TestTrue(TEXT("the project's root has no token"), RootToken.IsEmpty());
	const FString ExpectedManifestPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Intermediate/DLoc/extract/DreamGUI") / Source + TEXT(".json"));
	if (!TestTrue(TEXT("under the project's Intermediate/DLoc/extract/DreamGUI"), FPaths::IsSamePath(ManifestPath, ExpectedManifestPath)))
	{
		AddInfo(FString::Printf(TEXT("the manifest path is %s, expected %s"), *ManifestPath, *ExpectedManifestPath));
	}
	FString Outside, OutsideSource, OutsideToken;
	TestFalse(TEXT("a file under no root has none"), DreamUILocalizationManifest::GetPaths(
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Elsewhere.dui")), Outside, OutsideSource, OutsideToken));

	UPackage* Package = CreatePackage(*PackageName);
	Package->AddToRoot();
	ON_SCOPE_EXIT
	{
		Package->RemoveFromRoot();
	};
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamTextUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
		? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("a .dui-backed blueprint"), Defaults))
	{
		return false;
	}
	Defaults->SourceFile.FilePath = DuiFile;

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("the file compiles clean"), Results.NumErrors, 0);

	FString Json;
	if (!TestTrue(TEXT("the compile wrote the manifest"), FFileHelper::LoadFileToString(Json, *ManifestPath)))
	{
		return false;
	}
	const FString Expected = FString::Printf(TEXT("{\n  \"version\": 1,\n  \"provider\": \"DreamGUI\",\n  \"source\": \"%s\",\n  \"namespace\": \"%s\",\n"),
		*Source, *PackageName) + ExpectedEntries();
	TestEqual(TEXT("and it lists the file's texts"), Json, Expected);

	// A recompile with nothing new leaves the file as it is: no change for a watcher to see.
	const FDateTime Written = IFileManager::Get().GetTimeStamp(*ManifestPath);
	FPlatformProcess::Sleep(0.05f);
	FCompilerResultsLog Again;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Again);
	TestEqual(TEXT("an unchanged recompile does not touch the manifest"), IFileManager::Get().GetTimeStamp(*ManifestPath), Written);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
