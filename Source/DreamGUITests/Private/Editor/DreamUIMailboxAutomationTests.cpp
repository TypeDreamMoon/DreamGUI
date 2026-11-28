// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIDatabase.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIDiagnosticsMailbox.h"
#include "Text/DreamUISymbolExport.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "SQLitePreparedStatement.h"

/*
 * The diagnostics mailbox, held to the reader's contract.
 *
 * The contract is the `diagnostics` table of ui.db (Plan/DreamSource/dreamsource-layout-spec.md 11.3); what these
 * tests hold is this side's half: what gets deposited, what a clean compile writes (an empty array, because absence
 * over there means "no news" while emptiness means "all clear"), what a non-text compile leaves alone, and that every
 * flush is one revision. Everything runs against an override database under Saved/ so the suite never touches the
 * project's real one -- the one an actual VSCode session may be watching while tests run.
 */

namespace DreamUIMailboxTestLocal
{
	struct FScopedMailboxDirectory
	{
		FString Directory;
		FString Database;

		FScopedMailboxDirectory()
		{
			Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Mailbox"),
				FGuid::NewGuid().ToString(EGuidFormats::Short)));
			Database = FPaths::Combine(Directory, TEXT("ui.db"));
			IFileManager::Get().MakeDirectory(*Directory, /*Tree*/true);
			FDreamUIDiagnosticsMailbox::Reset();
		}

		~FScopedMailboxDirectory()
		{
			FDreamUIDiagnosticsMailbox::Reset();
			IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/false, /*Tree*/true);
		}
	};

	/**
	 * The diagnostics table, put back into the shape the extension builds from it -- `{ "files": { path: { "compiledAt",
	 * "diagnostics" } } }` -- so the assertions below read as they did against the json file it replaced.
	 */
	TSharedPtr<FJsonObject> ReadMailbox(const FString& InDatabasePath)
	{
		FDreamUIDatabase Database;
		if (InDatabasePath.IsEmpty() || !Database.Open(InDatabasePath))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Files = MakeShared<FJsonObject>();
		FSQLitePreparedStatement Select(Database.Get(), TEXT("SELECT path, compiled_at_utc, json FROM diagnostics;"));
		while (Select.IsValid() && Select.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			FString Path, CompiledAt, Json;
			Select.GetColumnValueByIndex(0, Path);
			Select.GetColumnValueByIndex(1, CompiledAt);
			Select.GetColumnValueByIndex(2, Json);
			TArray<TSharedPtr<FJsonValue>> Diagnostics;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Diagnostics))
			{
				return nullptr;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("compiledAt"), CompiledAt);
			Entry->SetArrayField(TEXT("diagnostics"), Diagnostics);
			Files->SetObjectField(Path, Entry);
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), Database.GetMeta(TEXT("schema")));
		Root->SetNumberField(TEXT("revision"), FCString::Atoi64(*Database.GetMeta(TEXT("diagnostics_revision"))));
		Root->SetObjectField(TEXT("files"), Files);
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

	const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Database);
	TestEqual(TEXT("the database it was given was written"), Written, Scoped.Database);

	const TSharedPtr<FJsonObject> Root = ReadMailbox(Written);
	if (!TestTrue(TEXT("the database reads back"), Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("schema"), Root->GetStringField(TEXT("schema")), FString::FromInt(FDreamUIDatabase::SchemaVersion));
	TestEqual(TEXT("the first flush is revision 1"), static_cast<int32>(Root->GetNumberField(TEXT("revision"))), 1);

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

	// Every flush is one revision: that is what the extension watches for.
	FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Database);
	const TSharedPtr<FJsonObject> Again = ReadMailbox(Scoped.Database);
	TestEqual(TEXT("a second flush is revision 2"), Again.IsValid() ? static_cast<int32>(Again->GetNumberField(TEXT("revision"))) : -1, 2);
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
	FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Database);

	// The author fixes the file; the next compile deposits a clean bag for the same path.
	FDreamUIDiagnosticBag Clean;
	Clean.SourceName = TEXT("X:/Proj/DUI/Login.dui");
	FDreamUIDiagnosticsMailbox::Deposit(Clean);
	const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Database);

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

	const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(Scoped.Database);
	TestFalse(TEXT("a flush still writes the database"), Written.IsEmpty());
	const TSharedPtr<FJsonObject> Root = ReadMailbox(Written);
	if (!TestTrue(TEXT("the mailbox parses"), Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("no entries"), Root->GetObjectField(TEXT("files"))->Values.Num(), 0);
	return true;
}

namespace DreamUIMailboxTestLocal
{
	/** The number of files the mailbox on disk holds an entry for, or -1 when it could not be read. */
	int32 CountWrittenEntries(const FString& InDatabasePath)
	{
		const FString Written = FDreamUIDiagnosticsMailbox::FlushNow(InDatabasePath);
		const TSharedPtr<FJsonObject> Root = ReadMailbox(Written);
		return Root.IsValid() ? Root->GetObjectField(TEXT("files"))->Values.Num() : -1;
	}

	FDreamUIDiagnosticBag MakeBrokenBag(const TCHAR* InSource)
	{
		FDreamUIDiagnosticBag Bag;
		Bag.SourceName = InSource;
		Bag.AddError(EDreamUIDiagnosticCode::EmptyTree, FDreamUISourceLocation(1, 1), TEXT("nothing to build"));
		return Bag;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMailboxRetiresAFileItsOwnerLeftTest,
	"DreamGUI.Text.AClassThatStopsUsingAFileRetiresItsMailboxEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMailboxRetiresAFileItsOwnerLeftTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	const FString Owner = TEXT("/Game/UI/WBP_Login.WBP_Login");
	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Login.dui")), Owner);
	TestEqual(TEXT("the file has an entry while its class is built from it"), CountWrittenEntries(Scoped.Database), 1);

	// The same Blueprint compiles again, now naming nothing: it was converted back to a plain Blueprint.
	FDreamUIDiagnosticsMailbox::Deposit(FDreamUIDiagnosticBag(), Owner);
	TestEqual(TEXT("the entry is gone once nothing is built from the file"), CountWrittenEntries(Scoped.Database), 0);

	// Pointing the same Blueprint at another file moves the entry rather than leaving two.
	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Login.dui")), Owner);
	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Title.dui")), Owner);
	TestEqual(TEXT("only the file the class names now has an entry"), CountWrittenEntries(Scoped.Database), 1);
	const TSharedPtr<FJsonObject> Root = ReadMailbox(Scoped.Database);
	if (TestTrue(TEXT("the mailbox parses"), Root.IsValid()))
	{
		TestTrue(TEXT("and it is the new one"), Root->GetObjectField(TEXT("files"))->HasField(TEXT("X:/Proj/DUI/Title.dui")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMailboxKeepsAFileAnotherClassStillUsesTest,
	"DreamGUI.Text.AMailboxEntryStaysWhileAnyClassIsStillBuiltFromTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMailboxKeepsAFileAnotherClassStillUsesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	const FString First = TEXT("/Game/UI/WBP_A.WBP_A");
	const FString Second = TEXT("/Game/UI/WBP_B.WBP_B");
	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Shared.dui")), First);
	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Shared.dui")), Second);

	FDreamUIDiagnosticsMailbox::Deposit(FDreamUIDiagnosticBag(), First);
	TestEqual(TEXT("one class leaving does not take the entry of a file another class is built from"),
		CountWrittenEntries(Scoped.Database), 1);

	FDreamUIDiagnosticsMailbox::Deposit(FDreamUIDiagnosticBag(), Second);
	TestEqual(TEXT("the last class leaving does"), CountWrittenEntries(Scoped.Database), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMailboxRemoveDropsADeletedFileTest,
	"DreamGUI.Text.ADeletedFileLeavesTheMailbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMailboxRemoveDropsADeletedFileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Login.dui")), TEXT("/Game/UI/WBP_Login.WBP_Login"));
	FDreamUIDiagnosticsMailbox::Deposit(MakeBrokenBag(TEXT("X:/Proj/DUI/Title.dui")));

	FDreamUIDiagnosticsMailbox::Remove(TEXT("X:/Proj/DUI/Login.dui"));
	TestEqual(TEXT("only the deleted file's entry is gone"), CountWrittenEntries(Scoped.Database), 1);

	// Asking about a file the mailbox holds nothing for changes nothing.
	FDreamUIDiagnosticsMailbox::Remove(TEXT("X:/Proj/DUI/Never.dui"));
	TestEqual(TEXT("an unknown file is ignored"), CountWrittenEntries(Scoped.Database), 1);
	return true;
}

namespace DreamUIMailboxTestLocal
{
	/** The symbols table put back together the way the extension does (spec 11.3), or null. */
	TSharedPtr<FJsonObject> ReadSymbols(const FString& InDatabasePath, int64& OutRevision)
	{
		FDreamUIDatabase Database;
		if (!Database.Open(InDatabasePath))
		{
			return nullptr;
		}
		OutRevision = FCString::Atoi64(*Database.GetMeta(TEXT("symbols_revision")));
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		FSQLitePreparedStatement Select(Database.Get(), TEXT("SELECT section, name, json FROM symbols;"));
		while (Select.IsValid() && Select.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			FString Section, Name, Json;
			Select.GetColumnValueByIndex(0, Section);
			Select.GetColumnValueByIndex(1, Name);
			Select.GetColumnValueByIndex(2, Json);
			// Wrapped in an array so one reader takes objects, arrays and scalars alike.
			TArray<TSharedPtr<FJsonValue>> Wrapped;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Json + TEXT("]")), Wrapped) || Wrapped.Num() != 1)
			{
				return nullptr;
			}
			if (Name.IsEmpty())
			{
				Root->SetField(Section, Wrapped[0]);
				continue;
			}
			const TSharedPtr<FJsonObject>* Existing = nullptr;
			TSharedPtr<FJsonObject> Object;
			if (Root->TryGetObjectField(Section, Existing))
			{
				Object = *Existing;
			}
			else
			{
				Object = MakeShared<FJsonObject>();
				Root->SetObjectField(Section, Object);
			}
			Object->SetField(Name, Wrapped[0]);
		}
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISymbolsRoundTripThroughTheDatabaseTest,
	"DreamGUI.Text.TheSymbolsTableHoldsWhatTheCompilerKnows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISymbolsRoundTripThroughTheDatabaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIMailboxTestLocal;
	FScopedMailboxDirectory Scoped;

	TestEqual(TEXT("the export writes the database it was given"), FDreamUISymbolExport::ExportNow(Scoped.Database), Scoped.Database);

	int64 Revision = 0;
	const TSharedPtr<FJsonObject> Read = ReadSymbols(Scoped.Database, Revision);
	if (!TestTrue(TEXT("the symbols read back"), Read.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("the first export is revision 1"), Revision, static_cast<int64>(1));
	TestEqual(TEXT("version is a number at the top, as the json file had it"),
		static_cast<int32>(Read->GetNumberField(TEXT("version"))), 2);
	TestTrue(TEXT("tags came through as an object"), Read->HasTypedField<EJson::Object>(TEXT("tags")));
	TestTrue(TEXT("keywords came through as an array"), Read->HasTypedField<EJson::Array>(TEXT("keywords")));

	// Field order inside the reassembled object may differ from BuildSymbols', so each section is compared on its own.
	const TSharedRef<FJsonObject> Built = FDreamUISymbolExport::BuildSymbols();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Section : Built->Values)
	{
		const TSharedPtr<FJsonValue> Got = Read->TryGetField(Section.Key);
		if (!TestTrue(FString::Printf(TEXT("section '%s' is there"), *Section.Key), Got.IsValid()))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("section '%s' reads back equal"), *Section.Key),
			FJsonValue::CompareEqual(*Section.Value, *Got));
	}

	// Nothing changed, nothing written: the extension must not reload 1.8 MB for an editor start that taught it nothing.
	FDreamUISymbolExport::ExportNow(Scoped.Database);
	ReadSymbols(Scoped.Database, Revision);
	TestEqual(TEXT("an export that says the same thing leaves the revision alone"), Revision, static_cast<int64>(1));
	return true;
}

#endif
