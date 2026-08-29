// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUIDiagnostics.h"

#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * A .dui becoming a class.
 *
 * These are the only tests in the suite where the hierarchy under test arrives through the file
 * system, and that is the point of them. Everything upstream is already covered without a file: the
 * parser's tests are strings in and data out, and the builder's hand it an AST built in memory. Both
 * are true of a tree the test constructed, and DreamOnDiskFixture's class comment says what the whole
 * shape of that costs -- four of the five defects found by opening the editor on 2026-08-29 were true
 * of a tree the tests built and false of a tree that had been to disk, and 340 green tests could not
 * see one of them. The text pipeline goes through the file system by definition, so it gets the same
 * treatment: a real file, written to Saved/, read back by the compiler through the same call the
 * editor makes, and removed when the test leaves.
 *
 * Saved/ rather than Content/ because a .dui in Content is a source file the content browser will
 * show and the next cook will copy. Absolute paths in the fixtures, because ProjectSavedDir() is
 * itself relative and the resolver would then root it at Content -- the Content-relative rule is
 * asserted directly instead, where it can be read.
 */

namespace DreamUITextCompileTestLocal
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}

		~FScopedDuiFile()
		{
			// EvenReadOnly, and quiet: a leftover file is read by the NEXT run of this test, which
			// turns a failure here into a failure over there with nothing connecting them.
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		/** Write these lines as the whole file. Line N of the array is line N+1 of the diagnostics. */
		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}

		FString FilePath;
	};

	/**
	 * A widget blueprint in a real package, with the machinery a compile needs.
	 *
	 * FKismetEditorUtilities::CreateBlueprint rather than a hand-built object, and a full
	 * CompileBlueprint rather than calling the compiler's pieces: what P3 claims is that the hook is
	 * wired into Kismet at the right override, and every way of testing that from the inside passes
	 * whether it is or not.
	 */
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName, UClass* InParentClass = UDreamTextUserWidget::StaticClass())
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedBlueprint(const FScopedBlueprint&) = delete;
		FScopedBlueprint& operator=(const FScopedBlueprint&) = delete;

		/**
		 * Point the class at a .dui, the way the Class Defaults panel does.
		 *
		 * On the CDO, because DUI_File_Path is EditDefaultsOnly and a class default is what a CDO IS.
		 * CreateBlueprint has already compiled once, so there is one to write to; every compile after
		 * this copies the value onto the CDO it makes, which is what lets the path be set once here
		 * and still be read by the third compile.
		 */
		bool SetDuiFilePath(const FString& InFilePath) const
		{
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				return false;
			}
			Defaults->DUI_File_Path.FilePath = InFilePath;
			return true;
		}

		/** Add a widget of InDisplayName under InParent (or the root when null). For the control cases. */
		UDreamWidget* AddWidget(const TCHAR* InDisplayName, UDreamWidget* InParent = nullptr) const
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			UDreamWidget* Widget = Tree->ConstructWidget<UDreamWidget>();
			Widget->SetDisplayName(InDisplayName);
			Widget->SetParentBeforeRegister(InParent != nullptr ? InParent : Tree->RootWidget.Get());
			return Widget;
		}
	};

	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}

	FString JoinMessages(const FCompilerResultsLog& InResults)
	{
		FString All;
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			All += Message->ToText().ToString() + TEXT(" | ");
		}
		return All;
	}

	/**
	 * Assert that the compile said InNeedle somewhere.
	 *
	 * The dump goes out through AddInfo and never into the assertion's description, which is not
	 * tidiness: every test below declares the diagnostic it expects through AddExpectedError, that
	 * entry matches by substring, and a description repeating the same text is swallowed by it. The
	 * failure then prints nothing at all, which is worse than a bare description.
	 */
	bool TestMessagesContain(FAutomationTestBase& InTest, const TCHAR* InWhat,
		const FCompilerResultsLog& InResults, const FString& InNeedle)
	{
		const FString All = JoinMessages(InResults);
		const bool bFound = All.Contains(InNeedle);
		InTest.TestTrue(InWhat, bFound);
		if (!bFound)
		{
			InTest.AddInfo(FString::Printf(TEXT("the compile said: %s"), *All));
		}
		return bFound;
	}

	/** "DUI1002" -- the code table, never a literal, so a renumbering breaks the test at the table. */
	FString Code(EDreamUIDiagnosticCode InCode)
	{
		return FDreamUIDiagnostic::CodeToString(InCode);
	}

	UDreamWidget* FindWidget(const UDreamWidgetTree* InTree, const TCHAR* InDisplayName)
	{
		UDreamWidget* Found = nullptr;
		if (IsValid(InTree))
		{
			InTree->ForEachWidget([&Found, InDisplayName](UDreamWidget* Widget)
			{
				if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
				{
					Found = Widget;
				}
			});
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextCompilesTreeFromTextTest,
	"DreamGUI.WidgetBlueprint.ADuiBackedBlueprintCompilesItsTreeFromText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextCompilesTreeFromTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextCompileTestLocal;

	FScopedDuiFile Source(TEXT("CompilesFromText.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("class /Temp/DreamGUITests/BP_CompilesFromText"),
		TEXT(""),
		TEXT("style Card {"),
		TEXT("    RenderOpacity = 0.5"),
		TEXT("}"),
		TEXT(""),
		TEXT("Widget Root {"),
		TEXT("    Text Title : Card {"),
		TEXT("        FontSize = 24"),
		TEXT("    }"),
		TEXT(""),
		TEXT("    Widget OkBtn {"),
		TEXT("        Text OkText {"),
		TEXT("            Text = \"OK\""),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FScopedBlueprint Fixture(TEXT("BP_CompilesFromText"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	if (!TestEqual(TEXT("a well-formed .dui compiles clean"), Results.NumErrors, 0))
	{
		AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
		return false;
	}

	// The tree the ASSET now holds. This is the field the text owns -- the designer edits it, the
	// preview host mirrors into it, and FinishCompilingClass duplicates it onto the class -- so a
	// pipeline that put the hierarchy anywhere else would leave every one of those looking at the
	// hierarchy the author last dragged, whatever the file says.
	UDreamWidgetTree* Authored = Fixture.Blueprint->WidgetTree;
	if (!TestNotNull(TEXT("the Blueprint carries a hierarchy"), Authored)) return false;
	TestEqual(TEXT("with every node in the file"), Authored->CountWidgets(), 4);
	if (TestNotNull(TEXT("rooted where the file roots it"), Authored->RootWidget.Get()))
	{
		TestEqual(TEXT("under the id the file gave it"), Authored->RootWidget->GetDisplayName(), FString(TEXT("Root")));
	}
	// Outered to the Blueprint, not to a world and not to the transient package: that is what
	// SaveSubObjectsFromCleanAndSanitizeClass keeps alive through the sanitize pass and what
	// UpdateGeneratedClassWidgetTree duplicates from. A tree built somewhere else compiles once and
	// then vanishes at the next recompile, which is a hard defect to read from the symptom.
	TestEqual(TEXT("and owned by the asset"),
		(const UObject*)Authored->GetOuter(), (const UObject*)Fixture.Blueprint);

	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("compiling produced a DreamUI class"), GeneratedClass)) return false;
	UDreamWidgetTree* Archetype = GeneratedClass->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("the class carries the hierarchy"), Archetype)) return false;
	TestEqual(TEXT("all of it"), Archetype->CountWidgets(), 4);

	// One member variable per authored node, which is the entire reason the file is read at compile
	// time rather than at run time. Every id in the file, and nothing else.
	TestNotNull(TEXT("the root is a variable"), GeneratedClass->FindPropertyByName(FName(TEXT("Root"))));
	TestNotNull(TEXT("so is a child"), GeneratedClass->FindPropertyByName(FName(TEXT("Title"))));
	TestNotNull(TEXT("so is one nested two levels down"), GeneratedClass->FindPropertyByName(FName(TEXT("OkText"))));
	TestNull(TEXT("and nothing the file does not name"), GeneratedClass->FindPropertyByName(FName(TEXT("Absent"))));

	// Values, not only shape. A tree with the right nodes and every property still at its default
	// would satisfy everything above, and is exactly what a builder wired up with the wrong outer or
	// a property pass that silently gave up would produce.
	UDreamWidget* Title = FindWidget(Archetype, TEXT("Title"));
	if (TestNotNull(TEXT("the class's copy has the text node"), Title))
	{
		if (UDreamText* Visual = Cast<UDreamText>(Title->GetVisual()))
		{
			TestEqual(TEXT("carrying the size the file wrote on its visual"), Visual->GetFontSize(), 24.0f);
		}
		else
		{
			AddError(TEXT("the text node has no text visual"));
		}
		// From `: Card`, not from the node's own block. A style that resolved to nothing would leave
		// this at 1.0 and report nothing, because an unresolved style name is a parse error and this
		// one resolves.
		TestEqual(TEXT("and the opacity its style set"), Title->GetRenderOpacity(), 0.5f);
	}

	// The file is the source of truth on EVERY compile, not only the first. This is also what proves
	// DUI_File_Path survives a compile: it was written to the CDO once, above, and the CDO that reads
	// it here is the one the previous compile made.
	if (!TestTrue(TEXT("the fixture rewrote the .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        FontSize = 12"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	if (!TestEqual(TEXT("recompiling stays clean"), SecondResults.NumErrors, 0))
	{
		AddInfo(FString::Printf(TEXT("the recompile said: %s"), *JoinMessages(SecondResults)));
		return false;
	}

	UDreamWidgetGeneratedClass* SecondClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass);
	if (TestNotNull(TEXT("and produced a class"), SecondClass))
	{
		if (UDreamWidgetTree* SecondArchetype = SecondClass->GetWidgetTreeArchetype())
		{
			TestEqual(TEXT("the hierarchy is the smaller one the file now says"), SecondArchetype->CountWidgets(), 2);
			if (UDreamWidget* SmallerTitle = FindWidget(SecondArchetype, TEXT("Title")))
			{
				if (UDreamText* Visual = Cast<UDreamText>(SmallerTitle->GetVisual()))
				{
					TestEqual(TEXT("with the value the file now writes"), Visual->GetFontSize(), 12.0f);
				}
			}
		}
		// And the variable for the node that is gone goes with it. A stale variable is worse than a
		// missing one: the graph keeps compiling against a widget no instance will ever have.
		TestNull(TEXT("a node the file dropped stops being a variable"), SecondClass->FindPropertyByName(FName(TEXT("OkBtn"))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextSyntaxErrorFailsCompileTest,
	"DreamGUI.WidgetBlueprint.ADuiSyntaxErrorFailsTheCompileWithALineNumber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextSyntaxErrorFailsCompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextCompileTestLocal;

	FScopedDuiFile Source(TEXT("SyntaxError.dui"));
	// An unterminated string, because it is the mistake with the least ambiguous position: strings do
	// not span lines, so the parser reports it once, on the line it starts on, and recovery carries on
	// from the next one. A test asserting a line number wants a mistake whose line is not a judgement.
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Title = \"no closing quote"),
		TEXT("}")
	})))
	{
		return false;
	}

	FScopedBlueprint Fixture(TEXT("BP_SyntaxError"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	// Declared, because a failing compile logs through LogBlueprint and automation counts any logged
	// error as a failed test. By code rather than by wording: the number is the stable half of a
	// diagnostic, which is the whole reason the table has numbers.
	AddExpectedError(Code(EDreamUIDiagnosticCode::UnterminatedString), EAutomationExpectedErrorFlags::Contains, 0);

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("a .dui that does not parse fails the compile"), Results.NumErrors > 0);

	// The line number is the entire argument for a text pipeline over a binary one, so it is asserted
	// rather than assumed. The whole prefix, not just the digits: "(2," alone would also match a
	// column, and this is the one place the printed layout is the contract.
	TestMessagesContain(*this, TEXT("the message carries the line the mistake is on"), Results, TEXT("(2,"));
	TestMessagesContain(*this, TEXT("and the code for what it is"), Results,
		Code(EDreamUIDiagnosticCode::UnterminatedString));
	TestMessagesContain(*this, TEXT("and the file it came from"), Results, FPaths::GetCleanFilename(Source.FilePath));

	// Nothing was installed. A file that will not parse says nothing about what the class should
	// contain, and a half-built hierarchy from a half-read file is the one outcome that would make the
	// error message misleading rather than merely unwelcome.
	if (UDreamWidgetTree* Authored = Fixture.Blueprint->WidgetTree)
	{
		TestNull(TEXT("and no node from the broken file was installed"), FindWidget(Authored, TEXT("Title")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextMissingFileTest,
	"DreamGUI.WidgetBlueprint.AMissingDuiFileIsReportedAgainstThePathItLookedIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextMissingFileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextCompileTestLocal;

	// The resolution rule, on its own, without a file: a relative path is rooted at Content. Asserted
	// here rather than by writing a .dui into the project's Content directory, which would be a source
	// file the content browser shows and the next cook copies.
	const FString Expected = [] {
		FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectContentDir(), TEXT("UI/NotThere.dui")));
		FPaths::NormalizeFilename(Path);
		return Path;
	}();
	TestEqual(TEXT("a relative path is rooted at the project's Content directory"),
		UDreamTextUserWidget::ResolveDuiFilePath(TEXT("UI/NotThere.dui")), Expected);
	TestEqual(TEXT("and an empty one stays empty rather than becoming the Content directory"),
		UDreamTextUserWidget::ResolveDuiFilePath(TEXT("  ")), FString());

	FScopedBlueprint Fixture(TEXT("BP_MissingDui"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points somewhere with no file"), Fixture.SetDuiFilePath(TEXT("UI/NotThere.dui")))) return false;

	AddExpectedError(Code(EDreamUIDiagnosticCode::SourceFileUnreadable), EAutomationExpectedErrorFlags::Contains, 0);

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("a path with no file behind it fails the compile"), Results.NumErrors > 0);
	TestMessagesContain(*this, TEXT("under the compile-stage code for an unreadable source"), Results,
		Code(EDreamUIDiagnosticCode::SourceFileUnreadable));
	// Both spellings. The author wrote the relative one and will go looking for that; the file that is
	// missing is at the resolved one. A message with only one of them cannot tell a typo apart from a
	// relative path that resolved somewhere the author did not expect, which is the failure mode a
	// Content-rooted rule has.
	TestMessagesContain(*this, TEXT("naming the path as the author wrote it"), Results, TEXT("UI/NotThere.dui"));
	TestMessagesContain(*this, TEXT("and the place it actually looked"), Results, Expected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextEmptyTreeTest,
	"DreamGUI.WidgetBlueprint.ADuiThatProducesNoHierarchyIsRefusedRatherThanCompilingToNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextEmptyTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextCompileTestLocal;

	// An empty file. Refused by the parser, which is the right stage for it: a file with no root is a
	// grammar fact, and the compile stage has nothing to add beyond passing the message on.
	{
		FScopedDuiFile Source(TEXT("Empty.dui"));
		if (!TestTrue(TEXT("the fixture wrote an empty .dui"), Source.Write({ TEXT("") }))) return false;

		FScopedBlueprint Fixture(TEXT("BP_EmptyDui"));
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
		if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

		AddExpectedError(Code(EDreamUIDiagnosticCode::MalformedRoot), EAutomationExpectedErrorFlags::Contains, 0);

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestTrue(TEXT("an empty file fails the compile"), Results.NumErrors > 0);
		TestMessagesContain(*this, TEXT("saying the file has no root"), Results,
			Code(EDreamUIDiagnosticCode::MalformedRoot));
	}

	// And the other half: a file that parses perfectly and still yields nothing. Only the builder can
	// tell -- a type name is an identifier until reflection is asked about it -- so this is the case
	// the 6xxx code exists for, and the one that would otherwise be a compile that reported a cause
	// and then quietly carried the previous hierarchy forward as though nothing had happened.
	{
		FScopedDuiFile Source(TEXT("NoSuchType.dui"));
		if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
			TEXT("NotAWidgetType Root {"),
			TEXT("}")
		})))
		{
			return false;
		}

		FScopedBlueprint Fixture(TEXT("BP_NoSuchType"));
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
		if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

		AddExpectedError(Code(EDreamUIDiagnosticCode::UnknownNodeType), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(Code(EDreamUIDiagnosticCode::EmptyTree), EAutomationExpectedErrorFlags::Contains, 0);

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestTrue(TEXT("a file that builds nothing fails the compile"), Results.NumErrors > 0);
		// Two codes, and both are wanted. The builder's says the CAUSE -- this node names a type
		// nothing resolves -- and the compile stage's says the OUTCOME, that there is no hierarchy to
		// put on the class. A reader acts on them differently, which is why they are not one code.
		TestMessagesContain(*this, TEXT("the builder says which node it could not make"), Results,
			Code(EDreamUIDiagnosticCode::UnknownNodeType));
		TestMessagesContain(*this, TEXT("and the compile says there was nothing to compile"), Results,
			Code(EDreamUIDiagnosticCode::EmptyTree));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextLeavesOrdinaryBlueprintsAloneTest,
	"DreamGUI.WidgetBlueprint.ABlueprintWithNoDuiFileIsUntouchedByTheTextPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextLeavesOrdinaryBlueprintsAloneTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextCompileTestLocal;

	// The most important test in this file. The read hangs off PopulateBlueprintGeneratedVariables,
	// which every compile of every DreamUI Blueprint goes through -- so anything it does
	// unconditionally, it does to the assets that have nothing to do with text. The claim is not "it
	// mostly works": it is that a hand-authored hierarchy comes out of a compile the same object it
	// went in as.
	{
		FScopedBlueprint Fixture(TEXT("BP_NoDuiPlain"), UDreamUserWidget::StaticClass());
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
		UDreamWidget* Header = Fixture.AddWidget(TEXT("Header"));
		Fixture.AddWidget(TEXT("Caption"), Header);

		UDreamWidgetTree* Before = Fixture.Blueprint->WidgetTree;
		if (!TestNotNull(TEXT("with a hand-authored hierarchy"), Before)) return false;

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestEqual(TEXT("a hierarchy nobody wrote in text compiles clean"), Results.NumErrors, 0);
		// Object identity, not equivalence. A hook that rebuilt an identical tree would pass a
		// structural comparison and still throw away every designer reference into the old one.
		TestEqual(TEXT("and the compile did not replace its tree"),
			(const UDreamWidgetTree*)Fixture.Blueprint->WidgetTree, (const UDreamWidgetTree*)Before);
		TestEqual(TEXT("nor change what is in it"), Before->CountWidgets(), 3);
		TestEqual(TEXT("nor invent bindings for it"), Fixture.Blueprint->PropertyBindings.Num(), 0);
		if (UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass))
		{
			TestNotNull(TEXT("and the hand-authored widgets are still variables"),
				GeneratedClass->FindPropertyByName(FName(TEXT("Caption"))));
		}
	}

	// The sharper half: a class that CAN carry a .dui and does not. The hook has to key on the path
	// being set, not on the parent class being the text one -- otherwise the first thing an author
	// does after switching a Blueprint's parent, before typing a path, is watch their hierarchy vanish.
	{
		FScopedBlueprint Fixture(TEXT("BP_NoDuiTextParent"), UDreamTextUserWidget::StaticClass());
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
		Fixture.AddWidget(TEXT("Header"));

		UDreamWidgetTree* Before = Fixture.Blueprint->WidgetTree;
		if (!TestNotNull(TEXT("with a hand-authored hierarchy"), Before)) return false;

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestEqual(TEXT("a text-backed class with no path set compiles clean"), Results.NumErrors, 0);
		TestEqual(TEXT("and keeps the tree it was given"),
			(const UDreamWidgetTree*)Fixture.Blueprint->WidgetTree, (const UDreamWidgetTree*)Before);
		TestEqual(TEXT("with everything still in it"), Before->CountWidgets(), 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextBindingReachesTheClassTest,
	"DreamGUI.WidgetBlueprint.ADuiBindingIsHandedToTheBlueprintAndResolved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextBindingReachesTheClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextCompileTestLocal;

	FScopedDuiFile Source(TEXT("Binding.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- GetTitleText()"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	// The parent declares GetTitleText natively. The builder deliberately does NOT check that the
	// function exists -- at build time the class that would declare it is still being made from this
	// very tree -- so a `<-` line only becomes real if the bindings reach the Blueprint before
	// CompilePropertyBindings runs. That handover is what this test is about.
	FScopedBlueprint Fixture(TEXT("BP_TextBinding"), UDreamTextUserWidgetBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	if (!TestEqual(TEXT("a binding whose function exists compiles clean"), Results.NumErrors, 0))
	{
		AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
		return false;
	}

	if (TestEqual(TEXT("the file's binding reached the asset"), Fixture.Blueprint->PropertyBindings.Num(), 1))
	{
		const FDreamWidgetPropertyBinding& Authored = Fixture.Blueprint->PropertyBindings[0];
		// By the variable name, which is the widget's display name -- the same name the class declares
		// a member for and the same one the runtime resolves through. A binding naming anything else
		// reports success here and comes back null in the game.
		TestEqual(TEXT("naming the widget"), Authored.WidgetName, FName(TEXT("Title")));
		TestEqual(TEXT("the property"), Authored.PropertyName, FName(TEXT("Text")));
		TestEqual(TEXT("and the function"), Authored.FunctionName, FName(TEXT("GetTitleText")));
		// The visual, not the widget: Text lives on UDreamText. Getting this wrong is a binding that
		// resolves to an object without the property and fails at the next stage rather than here.
		TestEqual(TEXT("against the visual that owns it"), (uint8)Authored.Target, (uint8)EDreamWidgetBindingTarget::Visual);
	}

	// And through to the class, resolved. This is the pass that would have rejected the binding if the
	// handover had happened after it instead of before.
	if (UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass))
	{
		const TArray<FDreamWidgetPropertyBinding>& Resolved = GeneratedClass->GetPropertyBindings();
		if (TestEqual(TEXT("the class carries the resolved binding"), Resolved.Num(), 1))
		{
			TestEqual(TEXT("with the setter the compiler found for it"), Resolved[0].SetterName, FName(TEXT("SetText")));
		}
	}

	return true;
}

#endif
