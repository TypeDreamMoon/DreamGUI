// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIDiagnosticsMailbox.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

/*
 * The diagnostics mailbox, held to the reader's contract.
 *
 * The FORMAT contract lives in the VSCode extension (its parseMailbox tests are the spec); what
 * these tests hold is this side's half: what gets deposited, what a clean compile writes (an empty
 * array, because absence over there means "no news" while emptiness means "all clear"), what a
 * non-text compile leaves alone, and that the write really is beside-then-rename onto the final
 * name. Everything runs against an override directory under Saved/ so the suite never touches the
 * project's real mailbox -- the one an actual VSCode session may be watching while tests run.
 */

namespace DreamUIMailboxTestLocal
{
	struct FScopedMailboxDirectory
	{
		FString Directory;

		FScopedMailboxDirectory()
		{
			Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Mailbox"),
				FGuid::NewGuid().ToString(EGuidFormats::Short)));
			IFileManager::Get().MakeDirectory(*Directory, /*Tree*/true);
			FDreamUIDiagnosticsMailbox::Reset();
		}

		~FScopedMailboxDirectory()
		{
			FDreamUIDiagnosticsMailbox::Reset();
			IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/false, /*Tree*/true);
		}
	};

	TSharedPtr<FJsonObject> ReadMailbox(const FString& InPath)
	{
		FString Serialized;
		if (!FFileHelper::LoadFileToString(Serialized, *InPath))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMailboxCarriesTheCompilersBagTest,
	"DreamGUI.Text.TheMailboxCarriesWhatTheCompilerSaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMailboxCarriesTheCompilersBagTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	FDreamUIDiagnosticBag Bag;
	Bag.SourceName = TEXT("X:/Proj/DUI/Login.dui");
	Bag.AddError(EDreamUIDiagnosticCode::BindingFunctionNotFound,
		FDreamUISourceLocation(12, 5), TEXT("no function named 'GetTitle'"));
	Bag.AddWarning(EDreamUIDiagnosticCode::ClassPathMismatch,
		FDreamUISourceLocation(1, 7), TEXT("this file already compiles into another class"));
	FDreamUIDiagnosticsMailbox::Deposit(Bag);

	const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Directory);
	TestFalse(TEXT("something was written"), Written.IsEmpty());

	const TSharedPtr<FJsonObject> Root = ReadMailbox(Written);
	if (!TestTrue(TEXT("the mailbox parses as JSON"), Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("version"), static_cast<int32>(Root->GetNumberField(TEXT("version"))), 1);

	const TSharedPtr<FJsonObject>* Entry = nullptr;
	const TSharedPtr<FJsonObject> Files = Root->GetObjectField(TEXT("files"));
	if (!TestTrue(TEXT("the file has an entry"), Files->TryGetObjectField(TEXT("X:/Proj/DUI/Login.dui"), Entry)))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Diagnostics = (*Entry)->GetArrayField(TEXT("diagnostics"));
	if (!TestEqual(TEXT("two diagnostics"), Diagnostics.Num(), 2))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> First = Diagnostics[0]->AsObject();
	TestEqual(TEXT("code"), static_cast<int32>(First->GetNumberField(TEXT("code"))), 5004);
	TestEqual(TEXT("severity"), First->GetStringField(TEXT("severity")), FString(TEXT("error")));
	TestEqual(TEXT("line"), static_cast<int32>(First->GetNumberField(TEXT("line"))), 12);
	TestEqual(TEXT("column"), static_cast<int32>(First->GetNumberField(TEXT("column"))), 5);
	TestEqual(TEXT("the message is bare -- the reader prefixes the code itself"),
		First->GetStringField(TEXT("message")), FString(TEXT("no function named 'GetTitle'")));

	const TSharedPtr<FJsonObject> Second = Diagnostics[1]->AsObject();
	TestEqual(TEXT("warning severity"), Second->GetStringField(TEXT("severity")), FString(TEXT("warning")));

	// No stray .tmp beside the real file: the write renamed onto the final name.
	TArray<FString> Leftovers;
	IFileManager::Get().FindFiles(Leftovers, *(Scoped.Directory / TEXT("*.tmp")), /*Files*/true, /*Dirs*/false);
	TestEqual(TEXT("no temp file left behind"), Leftovers.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMailboxClearsWithACleanCompileTest,
	"DreamGUI.Text.ACleanCompileWritesAnEmptyMailboxEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMailboxClearsWithACleanCompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	FDreamUIDiagnosticBag Broken;
	Broken.SourceName = TEXT("X:/Proj/DUI/Login.dui");
	Broken.AddError(EDreamUIDiagnosticCode::EmptyTree, FDreamUISourceLocation(1, 1), TEXT("nothing to build"));
	FDreamUIDiagnosticsMailbox::Deposit(Broken);
	FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Directory);

	// The author fixes the file; the next compile deposits a clean bag for the same path.
	FDreamUIDiagnosticBag Clean;
	Clean.SourceName = TEXT("X:/Proj/DUI/Login.dui");
	FDreamUIDiagnosticsMailbox::Deposit(Clean);
	const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Directory);

	const TSharedPtr<FJsonObject> Root = ReadMailbox(Written);
	if (!TestTrue(TEXT("the mailbox parses"), Root.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Entry = nullptr;
	Root->GetObjectField(TEXT("files"))->TryGetObjectField(TEXT("X:/Proj/DUI/Login.dui"), Entry);
	if (!TestTrue(TEXT("the entry is still there"), Entry != nullptr))
	{
		return false;
	}
	// Present and EMPTY -- that is what clears the squiggles on the reader's side. Dropping the
	// entry instead would read as "no news" and leave the old error standing forever.
	TestEqual(TEXT("its diagnostics are empty"), (*Entry)->GetArrayField(TEXT("diagnostics")).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMailboxIgnoresNonTextCompilesTest,
	"DreamGUI.Text.ANonTextCompileNeverReachesTheMailbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMailboxIgnoresNonTextCompilesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	// A blueprint that names no .dui compiles with an empty source name; the mailbox must not
	// grow an entry for it -- this is the pipeline's negative control, kept here too.
	FDreamUIDiagnosticBag NotText;
	FDreamUIDiagnosticsMailbox::Deposit(NotText);

	const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Directory);
	TestFalse(TEXT("a flush still writes the file"), Written.IsEmpty());
	const TSharedPtr<FJsonObject> Root = ReadMailbox(Written);
	if (!TestTrue(TEXT("the mailbox parses"), Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("no entries"), Root->GetObjectField(TEXT("files"))->Values.Num(), 0);
	return true;
}

#endif
