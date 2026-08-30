// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIPaths.h"
#include "Text/DreamUIWorkspaceService.h"

#include "Dom/JsonObject.h"
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
	TestEqual(TEXT("it is the DreamUI extension"),
		Recommendations[0]->AsString(), FString(TEXT("typedreammoon.dreamui-language-support")));
	return true;
}

#endif
