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

		TSharedPtr<FJsonObject> ReadJson(const FString& InRelativePath) const
		{
			FString Serialized;
			if (!FFileHelper::LoadFileToString(Serialized, *(Root / InRelativePath)))
			{
				return nullptr;
			}
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
			FJsonSerializer::Deserialize(Reader, Object);
			return Object;
		}

		TSharedPtr<FJsonObject> ReadResponse(const FString& InId) const
		{
			return ReadJson(FString(TEXT("Responses")) / (InId + TEXT(".response.json")));
		}
	};

	/**
	 * The class every one of these asks about.
	 *
	 * A NATIVE class on purpose. The actions take a `classPath`, which in the field is a /Game path
	 * naming a widget Blueprint -- and a widget Blueprint is an asset a test would have to author,
	 * compile and clean up, three moving parts before the first assertion. The reflection these
	 * actions read is the same either way: UDreamUserWidget declares blueprint-visible variables
	 * and Blueprint-callable functions exactly as a generated class does. That the native spelling
	 * resolves at all is itself part of the contract -- the extension asks about base classes.
	 */
	const TCHAR* const KnownClassPath = TEXT("/Script/DreamGUI.DreamUserWidget");
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBridgeDescribesAClassTest,
	"DreamGUI.Text.TheBridgeDescribesTheClassItIsAskedAbout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBridgeDescribesAClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIBridgeTestLocal;
	FScopedBridgeRoot Scoped;

	Scoped.DropRequest(TEXT("001-functions"), FString::Printf(
		TEXT("{\"protocol\":1,\"requestId\":\"001-functions\",\"action\":\"functions\",\"classPath\":\"%s\"}"),
		KnownClassPath));
	Scoped.DropRequest(TEXT("002-variables"), FString::Printf(
		TEXT("{\"protocol\":1,\"requestId\":\"002-variables\",\"action\":\"variables\",\"classPath\":\"%s\"}"),
		KnownClassPath));
	FDreamUIBridgeService::ProcessPendingNow(Scoped.Root);

	// --- functions -------------------------------------------------------------------------
	const TSharedPtr<FJsonObject> Functions = Scoped.ReadResponse(TEXT("001-functions"));
	if (!TestTrue(TEXT("functions answered"), Functions.IsValid()))
	{
		return false;
	}
	if (TestTrue(TEXT("functions ok"), Functions->GetBoolField(TEXT("ok")))
		&& TestTrue(TEXT("carries a functions object"), Functions->HasTypedField<EJson::Object>(TEXT("functions"))))
	{
		const TSharedPtr<FJsonObject>& Lists = Functions->GetObjectField(TEXT("functions"));
		// All three lists, always. An absent list and an empty one mean different things to the
		// client -- "this editor cannot answer that" versus "there are none" -- so the shape is
		// asserted separately from the contents.
		TestTrue(TEXT("bindable is present"), Lists->HasTypedField<EJson::Array>(TEXT("bindable")));
		TestTrue(TEXT("handlers is present"), Lists->HasTypedField<EJson::Array>(TEXT("handlers")));
		if (TestTrue(TEXT("callable is present"), Lists->HasTypedField<EJson::Array>(TEXT("callable"))))
		{
			const TArray<TSharedPtr<FJsonValue>>& Callable = Lists->GetArrayField(TEXT("callable"));
			// UDreamUserWidget declares BlueprintCallable functions of its own (the FieldNotify
			// subscription pair among them), so an empty list here is a broken filter, not a
			// property of the class.
			if (TestTrue(TEXT("and the class has callable functions"), Callable.Num() > 0))
			{
				int32 Checked = 0;
				for (const TSharedPtr<FJsonValue>& Value : Callable)
				{
					const TSharedPtr<FJsonObject>* Info = nullptr;
					if (!Value->TryGetObject(Info))
					{
						AddError(TEXT("a callable entry is not an object"));
						break;
					}
					TestFalse(TEXT("every callable entry is named"),
						(*Info)->GetStringField(TEXT("name")).IsEmpty());
					TestTrue(TEXT("and says how many parameters it takes"),
						(*Info)->HasTypedField<EJson::Number>(TEXT("paramCount")));
					TestTrue(TEXT("and lists them, empty or not"),
						(*Info)->HasTypedField<EJson::Array>(TEXT("params")));
					TestTrue(TEXT("and says whether it is pure"),
						(*Info)->HasTypedField<EJson::Boolean>(TEXT("pure")));
					// paramCount is the array's length and not a second count kept by hand.
					TestEqual(TEXT("paramCount agrees with params"),
						(int32)(*Info)->GetNumberField(TEXT("paramCount")),
						(*Info)->GetArrayField(TEXT("params")).Num());
					for (const TSharedPtr<FJsonValue>& ParamValue : (*Info)->GetArrayField(TEXT("params")))
					{
						const TSharedPtr<FJsonObject>* Param = nullptr;
						if (ParamValue->TryGetObject(Param))
						{
							TestFalse(TEXT("every parameter has a type string"),
								(*Param)->GetStringField(TEXT("type")).IsEmpty());
						}
					}
					if (++Checked >= 8)
					{
						// The shape is uniform by construction; walking every function on the class
						// would trade a long test for no extra fact.
						break;
					}
				}
			}
		}
	}

	// --- variables -------------------------------------------------------------------------
	const TSharedPtr<FJsonObject> Variables = Scoped.ReadResponse(TEXT("002-variables"));
	if (!TestTrue(TEXT("variables answered"), Variables.IsValid()))
	{
		return false;
	}
	if (TestTrue(TEXT("variables ok"), Variables->GetBoolField(TEXT("ok")))
		&& TestTrue(TEXT("carries a variables array"), Variables->HasTypedField<EJson::Array>(TEXT("variables"))))
	{
		const TArray<TSharedPtr<FJsonValue>>& List = Variables->GetArrayField(TEXT("variables"));
		if (TestTrue(TEXT("the class has blueprint-visible variables"), List.Num() > 0))
		{
			const TSharedPtr<FJsonObject>* First = nullptr;
			if (List[0]->TryGetObject(First))
			{
				TestFalse(TEXT("a variable is named"), (*First)->GetStringField(TEXT("name")).IsEmpty());
				// The type string is what the client hands back as a `typePath`, so an empty one is
				// not a cosmetic gap -- it is a member lookup that can never be made.
				TestFalse(TEXT("and typed"), (*First)->GetStringField(TEXT("type")).IsEmpty());
				TestTrue(TEXT("and says whether it broadcasts"),
					(*First)->HasTypedField<EJson::Boolean>(TEXT("fieldNotify")));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBridgeReachesInsideATypeTest,
	"DreamGUI.Text.TheBridgeReachesInsideAContainerType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBridgeReachesInsideATypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIBridgeTestLocal;
	FScopedBridgeRoot Scoped;

	// FVector rather than a type of this plugin's: the point under test is the peel-and-resolve
	// walk, and a core struct cannot be renamed out from under the assertion.
	Scoped.DropRequest(TEXT("001-array"),
		TEXT("{\"protocol\":1,\"requestId\":\"001-array\",\"action\":\"members\",\"typePath\":\"TArray<FVector>\"}"));
	Scoped.DropRequest(TEXT("002-nonsense"),
		TEXT("{\"protocol\":1,\"requestId\":\"002-nonsense\",\"action\":\"members\",\"typePath\":\"FNoSuchStructExistsHere\"}"));
	FDreamUIBridgeService::ProcessPendingNow(Scoped.Root);

	const TSharedPtr<FJsonObject> Array = Scoped.ReadResponse(TEXT("001-array"));
	if (!TestTrue(TEXT("the array question answered"), Array.IsValid()))
	{
		return false;
	}
	if (TestTrue(TEXT("ok"), Array->GetBoolField(TEXT("ok"))))
	{
		// The element type comes back so the client never has to peel `TArray<...>` itself; a
		// second peeling rule on the other side is a rule that drifts.
		TestEqual(TEXT("the element type is reported"),
			Array->GetStringField(TEXT("elementType")), FString(TEXT("FVector")));

		TSet<FString> Names;
		if (TestTrue(TEXT("carries a members array"), Array->HasTypedField<EJson::Array>(TEXT("members"))))
		{
			for (const TSharedPtr<FJsonValue>& Value : Array->GetArrayField(TEXT("members")))
			{
				const TSharedPtr<FJsonObject>* Member = nullptr;
				if (Value->TryGetObject(Member))
				{
					Names.Add((*Member)->GetStringField(TEXT("name")));
					TestFalse(TEXT("every member is typed"),
						(*Member)->GetStringField(TEXT("type")).IsEmpty());
				}
			}
		}
		TestTrue(TEXT("and the members are the vector's"),
			Names.Contains(TEXT("X")) && Names.Contains(TEXT("Y")) && Names.Contains(TEXT("Z")));
	}

	const TSharedPtr<FJsonObject> Nonsense = Scoped.ReadResponse(TEXT("002-nonsense"));
	if (TestTrue(TEXT("the unresolvable type answered"), Nonsense.IsValid()))
	{
		// Refused, not answered with an empty list: "this type has no members" and "there is no
		// such type" are different facts and a client shows different things for them.
		TestFalse(TEXT("an unresolvable type is not ok"), Nonsense->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("and the message names it"),
			Nonsense->GetStringField(TEXT("message")).Contains(TEXT("FNoSuchStructExistsHere")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBridgeTellsVSCodeWhereToLookTest,
	"DreamGUI.Text.TheBridgeTellsVSCodeWhereToLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBridgeTellsVSCodeWhereToLookTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIBridgeTestLocal;
	FScopedBridgeRoot Scoped;

	const FString SourcePath = FPaths::ConvertRelativePathToFull(Scoped.Root / TEXT("Login.dui"));
	TestTrue(TEXT("the reveal was written"),
		FDreamUIBridgeService::WriteRevealToEditor(SourcePath, 12, 5, TEXT("OkButton"), Scoped.Root));

	const TSharedPtr<FJsonObject> Reveal = Scoped.ReadJson(TEXT("reveal-to-editor.json"));
	if (!TestTrue(TEXT("and is readable"), Reveal.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("stamped with the protocol"), (int32)Reveal->GetNumberField(TEXT("protocol")), 1);
	TestEqual(TEXT("naming the file"), Reveal->GetStringField(TEXT("file")), SourcePath);
	TestEqual(TEXT("the line"), (int32)Reveal->GetNumberField(TEXT("line")), 12);
	TestEqual(TEXT("the column"), (int32)Reveal->GetNumberField(TEXT("column")), 5);
	TestEqual(TEXT("and the widget"), Reveal->GetStringField(TEXT("widgetId")), FString(TEXT("OkButton")));
	TestFalse(TEXT("with a timestamp"), Reveal->GetStringField(TEXT("stampUtc")).IsEmpty());

	// 1-based on both sides: a 0 would put the cursor a line above the one that was meant, and the
	// caller that has no position at all passes zeroes rather than inventing one.
	TestTrue(TEXT("a positionless reveal still writes"),
		FDreamUIBridgeService::WriteRevealToEditor(SourcePath, 0, 0, FString(), Scoped.Root));
	const TSharedPtr<FJsonObject> Floor = Scoped.ReadJson(TEXT("reveal-to-editor.json"));
	if (TestTrue(TEXT("and is readable"), Floor.IsValid()))
	{
		TestEqual(TEXT("clamped to line 1"), (int32)Floor->GetNumberField(TEXT("line")), 1);
		TestEqual(TEXT("clamped to column 1"), (int32)Floor->GetNumberField(TEXT("column")), 1);
		// Overwritten, never queued: the newest reveal is the only one anybody wants.
		TestEqual(TEXT("and the file was replaced, not appended to"),
			Floor->GetStringField(TEXT("widgetId")), FString());
	}

	// Write-beside-then-rename, the same rule responses live under.
	TArray<FString> Temps;
	IFileManager::Get().FindFiles(Temps, *(Scoped.Root / TEXT("*.tmp")), true, false);
	TestEqual(TEXT("no temp files left"), Temps.Num(), 0);
	return true;
}

#endif
