// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIBridgeService.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/*
 * The bridge's protocol mechanics, against an override root under Saved/ so the suite never
 * touches the live bridge a real VSCode session may be talking to. What is held here is the
 * three-rule contract -- take-then-delete, respond-always (unknown and unreadable requests
 * included), write-beside-then-rename -- and drain order. The ACTIONS' semantics (functions,
 * reveal, compile) lean on assets and open editors and are exercised end to end instead.
 */

namespace DreamUIBridgeTestLocal
{
	struct FScopedBridgeRoot
	{
		FString Root;

		FScopedBridgeRoot()
		{
			Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Bridge"),
				FGuid::NewGuid().ToString(EGuidFormats::Short)));
			IFileManager::Get().MakeDirectory(*(Root / TEXT("Requests")), /*Tree*/true);
			IFileManager::Get().MakeDirectory(*(Root / TEXT("Responses")), /*Tree*/true);
		}

		~FScopedBridgeRoot()
		{
			IFileManager::Get().DeleteDirectory(*Root, /*RequireExists*/false, /*Tree*/true);
		}

		void DropRequest(const FString& InId, const FString& InJson) const
		{
			FFileHelper::SaveStringToFile(InJson, *(Root / TEXT("Requests") / (InId + TEXT(".request.json"))),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		TSharedPtr<FJsonObject> ReadResponse(const FString& InId) const
		{
			FString Serialized;
			if (!FFileHelper::LoadFileToString(Serialized,
				*(Root / TEXT("Responses") / (InId + TEXT(".response.json")))))
			{
				return nullptr;
			}
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
			FJsonSerializer::Deserialize(Reader, Object);
			return Object;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBridgeAnswersEveryRequestTest,
	"DreamGUI.Text.TheBridgeAnswersEveryRequestItTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBridgeAnswersEveryRequestTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIBridgeTestLocal;
	FScopedBridgeRoot Scoped;

	Scoped.DropRequest(TEXT("001-ping"),
		TEXT("{\"protocol\":1,\"requestId\":\"001-ping\",\"action\":\"ping\"}"));
	Scoped.DropRequest(TEXT("002-mystery"),
		TEXT("{\"protocol\":1,\"requestId\":\"002-mystery\",\"action\":\"summonDragons\"}"));
	Scoped.DropRequest(TEXT("003-broken"), TEXT("this is not json"));

	const int32 Processed = FDreamUIBridgeService::ProcessPendingNow(Scoped.Root);
	TestEqual(TEXT("all three were processed"), Processed, 3);

	// Take-then-delete: nothing left to replay.
	TArray<FString> Remaining;
	IFileManager::Get().FindFiles(Remaining, *(Scoped.Root / TEXT("Requests") / TEXT("*")), true, false);
	TestEqual(TEXT("the requests directory drained"), Remaining.Num(), 0);

	// Answered, all three -- a silent editor and a missing feature must never look alike.
	const TSharedPtr<FJsonObject> Ping = Scoped.ReadResponse(TEXT("001-ping"));
	if (TestTrue(TEXT("ping answered"), Ping.IsValid()))
	{
		TestTrue(TEXT("ping ok"), Ping->GetBoolField(TEXT("ok")));
	}
	const TSharedPtr<FJsonObject> Mystery = Scoped.ReadResponse(TEXT("002-mystery"));
	if (TestTrue(TEXT("the unknown action answered"), Mystery.IsValid()))
	{
		TestFalse(TEXT("unknown action is not ok"), Mystery->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("and says which action it did not know"),
			Mystery->GetStringField(TEXT("message")).Contains(TEXT("summonDragons")));
	}
	const TSharedPtr<FJsonObject> Broken = Scoped.ReadResponse(TEXT("003-broken"));
	if (TestTrue(TEXT("the unreadable request answered"), Broken.IsValid()))
	{
		TestFalse(TEXT("unreadable is not ok"), Broken->GetBoolField(TEXT("ok")));
	}

	// Write-beside-then-rename: no .tmp corpses.
	TArray<FString> Temps;
	IFileManager::Get().FindFiles(Temps, *(Scoped.Root / TEXT("Responses") / TEXT("*.tmp")), true, false);
	TestEqual(TEXT("no temp files left"), Temps.Num(), 0);

	// The pass wrote a heartbeat.
	TestTrue(TEXT("status.json exists"),
		IFileManager::Get().FileExists(*(Scoped.Root / TEXT("status.json"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBridgeListsAssetsTest,
	"DreamGUI.Text.TheBridgeListsWidgetBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBridgeListsAssetsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIBridgeTestLocal;
	FScopedBridgeRoot Scoped;

	Scoped.DropRequest(TEXT("001-assets"),
		TEXT("{\"protocol\":1,\"requestId\":\"001-assets\",\"action\":\"assets\"}"));
	FDreamUIBridgeService::ProcessPendingNow(Scoped.Root);

	const TSharedPtr<FJsonObject> Response = Scoped.ReadResponse(TEXT("001-assets"));
	if (!TestTrue(TEXT("answered"), Response.IsValid()))
	{
		return false;
	}
	// The registry may legitimately hold zero widget blueprints in a bare project; the contract
	// is the field's presence and shape, not its count.
	TestTrue(TEXT("ok"), Response->GetBoolField(TEXT("ok")));
	TestTrue(TEXT("carries an assets array"), Response->HasTypedField<EJson::Array>(TEXT("assets")));
	return true;
}

#endif
