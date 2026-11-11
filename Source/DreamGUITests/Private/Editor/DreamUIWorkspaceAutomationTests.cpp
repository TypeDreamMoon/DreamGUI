// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIPaths.h"
#include "Text/DreamUIWorkspaceService.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/*
 * The workspace JSON, held to what VSCode silently requires. This file is written here and read
 * by ANOTHER program; a malformed byte errors nowhere on this side -- VSCode just refuses to
 * treat the file as a workspace, wordlessly. So the builder is pure and its output is parsed
 * back and asserted, the lesson the DreamFX workspace already paid for.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWorkspaceJsonTest,
	"DreamGUI.Text.TheWorkspaceJsonNamesEveryRootAndTheExtension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWorkspaceJsonTest::RunTest(const FString& Parameters)
{
	TArray<FDreamUISourceRoot> Roots;
	FDreamUISourceRoot& Project = Roots.AddDefaulted_GetRef();
	Project.Directory = TEXT("X:/Proj/DUI/");
	Project.RootToken = FString(); // the project's own root has no token
	FDreamUISourceRoot& Plugin = Roots.AddDefaulted_GetRef();
	Plugin.Directory = TEXT("X:/Proj/Plugins/DreamGUI/DUI/");
	Plugin.RootToken = TEXT("Plugin.DreamGUI");

	const FString Serialized = FDreamUIWorkspaceService::BuildWorkspaceJson(Roots, TEXT("X:/Proj/DUI"));

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
	if (!TestTrue(TEXT("the workspace parses as JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>& Folders = Root->GetArrayField(TEXT("folders"));
	if (!TestEqual(TEXT("two folders"), Folders.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the project root is '.'"),
		Folders[0]->AsObject()->GetStringField(TEXT("path")), FString(TEXT(".")));
	TestEqual(TEXT("the project root's name"),
		Folders[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("DreamUI Source")));
	TestEqual(TEXT("the plugin root is relative"),
		Folders[1]->AsObject()->GetStringField(TEXT("path")), FString(TEXT("../Plugins/DreamGUI/DUI")));
	TestTrue(TEXT("the plugin root is named by its token"),
		Folders[1]->AsObject()->GetStringField(TEXT("name")).Contains(TEXT("Plugin.DreamGUI")));

	const TSharedPtr<FJsonObject> Settings = Root->GetObjectField(TEXT("settings"));
	TestEqual(TEXT("*.dui associates to the language"),
		Settings->GetObjectField(TEXT("files.associations"))->GetStringField(TEXT("*.dui")),
		FString(TEXT("dui")));

	const TArray<TSharedPtr<FJsonValue>>& Recommendations =
		Root->GetObjectField(TEXT("extensions"))->GetArrayField(TEXT("recommendations"));
	if (!TestEqual(TEXT("one recommended extension"), Recommendations.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("it is the extension under its current id"),
		Recommendations[0]->AsString(), FString(TEXT("typedreammoon.dreamgui-language-support")));
	const TArray<TSharedPtr<FJsonValue>>& Unwanted =
		Root->GetObjectField(TEXT("extensions"))->GetArrayField(TEXT("unwantedRecommendations"));
	TestTrue(TEXT("and the old id is unwanted"), Unwanted.Num() == 1
		&& Unwanted[0]->AsString() == TEXT("typedreammoon.dreamui-language-support"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWorkspaceOldExtensionTest,
	"DreamGUI.Text.AnExtensionCopyUnderTheOldIdIsFoundWhateverItsPublisher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWorkspaceOldExtensionTest::RunTest(const FString& Parameters)
{
	// A stand-in for ~/.vscode/extensions: VSCode unpacks each extension into `<publisher>.<name>-<version>`.
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamGUITests") / TEXT("VSCodeExtensions"));
	IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/false, /*Tree*/true);
	for (const TCHAR* Name : { TEXT("typedreammoon.dreamui-language-support-0.7.0"), TEXT("undefined_publisher.dreamui-language-support-0.6.1"),
		TEXT("typedreammoon.dreamgui-language-support-0.11.0"), TEXT("typedreammoon.dreamshader-language-support-2.2.0") })
	{
		IFileManager::Get().MakeDirectory(*(Directory / Name), /*Tree*/true);
	}

	const TArray<FString> Found = FDreamUIWorkspaceService::FindOldExtensionCopies(Directory);
	IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/false, /*Tree*/true);

	TestEqual(TEXT("both old copies, and only those"), Found.Num(), 2);
	TestTrue(TEXT("the Marketplace publisher's"), Found.Contains(TEXT("typedreammoon.dreamui-language-support-0.7.0")));
	TestTrue(TEXT("a local build's"), Found.Contains(TEXT("undefined_publisher.dreamui-language-support-0.6.1")));
	TestTrue(TEXT("a folder that is not there has none"), FDreamUIWorkspaceService::FindOldExtensionCopies(Directory).IsEmpty());
	return true;
}

#endif
