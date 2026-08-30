// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUITextWriteBack.h"

#include "Core/DreamUIAnchorData.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
// Only for the null host argument to Create: these cases exercise the text side against a tree of
// their own, so they never stand up a preview world.
#include "Designer/DreamWidgetPreviewHost.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDocument.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * The designer's half of the text pipeline: a value edited on the tree becoming a line of `.dui`.
 *
 * THE LOAD-BEARING TEST HERE IS NormalisingTheTextIsNotAChange, and it is worth saying why a test
 * that asserts nothing happened is the important one. The patcher edits text slices, so the obvious
 * write-back -- print every property the designer might have touched and hand them all over -- is
 * correct in the sense that the file ends up saying the right things, and useless in practice: an
 * author writes `(400,240)`, the printer spells the same value `(400, 240)`, and the line is
 * replaced. Opening a hand-written file in the designer would then produce a page of diff hunks
 * nobody made, on the first flush, before the author had touched anything. After a week of that no
 * `.dui` change is reviewable and the text stops being the source of truth in any sense that
 * matters.
 *
 * So the assertions come in pairs. Each "the edit reached the file" case is matched by a "and
 * nothing else moved" one -- the whole file compared byte for byte, comments and spacing included
 * -- because a write-back that gets the value right and the file wrong is the failure mode, not
 * the other way round.
 *
 * The fixtures build a LIVE tree from the very text under test and then poke one value into it,
 * which is what a designer gesture does to the authoring tree. That is not a shortcut: it makes the
 * "unchanged" case the default state of every test, so a comparison that is too eager shows up
 * everywhere at once rather than in the one case somebody remembered to write.
 */
namespace DreamUIWriteBackTestLocal
{
	FString Join(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	/**
	 * The reference file. Line numbers are the array's, so a test can say "line 5" and mean it.
	 *
	 * Line 5 is deliberately written `(400,240)` -- no space -- and line 8 deliberately carries a
	 * trailing comment after aligned padding. Both are things an author does and a careless
	 * write-back destroys, and both are invisible to any assertion that only looks at the value.
	 */
	TArray<FString> FixtureLines()
	{
		return {
			TEXT("// UI/WriteBack.dui"),                        // 1
			TEXT("class /Game/UI/WBP_WriteBack"),               // 2
			TEXT(""),                                           // 3
			TEXT("Widget Root {"),                              // 4
			TEXT("    AnchorData.SizeDelta = (400,240)"),       // 5
			TEXT(""),                                           // 6
			TEXT("    Text Title {"),                           // 7
			TEXT("        AnchorData.SizeDelta = (200, 40)   // 标题"), // 8
			TEXT("    }"),                                      // 9
			TEXT("}")                                           // 10
		};
	}

	FString Fixture()
	{
		return Join(FixtureLines());
	}

	/** The reference file with one line replaced, which is what most expected results are. */
	FString FixtureWith(int32 InOneBasedLine, const FString& InReplacement)
	{
		TArray<FString> Lines = FixtureLines();
		Lines[InOneBasedLine - 1] = InReplacement;
		return Join(Lines);
	}

	/** A node whose value is driven by a `<-` binding, next to one that is not. */
	FString BoundFixture()
	{
		return Join({
			TEXT("Widget Root {"),
			TEXT("    RenderOpacity <- GetFade()"),
			TEXT("    AnchorData.SizeDelta = (400,240)"),
			TEXT("}")
		});
	}

	/** A tree built from text, kept alive, with its AST and whatever the build had to say. */
	struct FBuiltTree
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		UDreamWidget* Find(const TCHAR* InNodeId) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([InNodeId, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InNodeId)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}
	};

	/**
	 * The tree the text describes, through the real builder.
	 *
	 * This stands in for the authoring tree the compiler produces. Using the same builder is the
	 * point: a hand-assembled tree could not disagree with the file in the ways that matter (a
	 * default the file leaves out, a style the file applies), and those are exactly the cases the
	 * comparison has to get right.
	 */
	FBuiltTree BuildTree(const FString& InText)
	{
		FBuiltTree Built;
		Built.Diagnostics.SourceName = TEXT("WriteBack.dui");
		if (FDreamUISourceFile::Parse(InText, Built.Diagnostics.SourceName, Built.Ast, Built.Diagnostics))
		{
			TArray<FDreamWidgetPropertyBinding> Bindings;
			Built.Tree.Reset(FDreamUITextBuilder::Build(Built.Ast, GetTransientPackage(), Built.Diagnostics, Bindings));
		}
		return Built;
	}

	/**
	 * The widget's anchor block, reached the way the pipeline reaches it.
	 *
	 * Through reflection rather than through SetSizeDelta and friends, because those are the LIVE
	 * setters: they mark layout dirty and expect a registered widget, and an authoring tree is never
	 * registered. The builder writes this memory directly for the same reason.
	 */
	FDreamUIAnchorData* AnchorDataOf(UDreamWidget* InWidget)
	{
		if (!IsValid(InWidget))
		{
			return nullptr;
		}
		FStructProperty* Property = CastField<FStructProperty>(
			FindFProperty<FProperty>(InWidget->GetClass(), UDreamWidget::GetPropertyName_AnchorData()));
		return Property != nullptr ? Property->ContainerPtrToValuePtr<FDreamUIAnchorData>(InWidget) : nullptr;
	}

	bool PokeFloat(UObject* InObject, const TCHAR* InName, float InValue)
	{
		if (!IsValid(InObject))
		{
			return false;
		}
		if (FNumericProperty* Property = CastField<FNumericProperty>(FindFProperty<FProperty>(InObject->GetClass(), InName)))
		{
			Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(InObject), InValue);
			return true;
		}
		return false;
	}

	bool HasDiagnostic(const FDreamUIDiagnosticBag& InBag, EDreamUIDiagnosticCode InCode)
	{
		return InBag.Diagnostics.ContainsByPredicate(
			[InCode](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.Code == InCode; });
	}

	/** A real `.dui` under Saved/, gone when the test leaves. Same shape the document tests use. */
	struct FScopedDuiFile
	{
		FString Path;

		FScopedDuiFile(const TCHAR* InFileName, const FString& InInitialText)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamGUITests") / InFileName);
			FFileHelper::SaveStringToFile(InInitialText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*Path, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		/** What the file actually says. The only honest answer to "did the flush land". */
		FString ReadBack() const
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				return TEXT("<the file could not be read>");
			}
			return Text;
		}
	};

	bool HasTransactionBuffer(FAutomationTestBase& InTest)
	{
		if (GEditor == nullptr || GEditor->Trans == nullptr)
		{
			InTest.AddError(TEXT("no transaction buffer; this test cannot say anything"));
			return false;
		}
		return true;
	}
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackNormalisationIsNotAChangeTest,
	"DreamGUI.Designer.NormalisingTheTextIsNotAChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackNormalisationIsNotAChangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	// The file says `(400,240)`. The printer says `(400, 240)`. Same value, different spelling, and
	// the whole design of this component is the decision that the second one is not a change.
	const FString Source = Fixture();
	FBuiltTree Live = BuildTree(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the write-back could compute an answer"),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}
	// Byte for byte, not "parses to the same thing": the point is the author's file, not its meaning.
	TestEqualSensitive(TEXT("and left the file exactly as it was"), Produced, Source);

	// The same claim through the whole stack, because the pure half returning InText unchanged is
	// only half the promise -- the other half is that no undo entry and no disk write happen either.
	FScopedDuiFile File(TEXT("WriteBackNormalisation.dui"), Source);
	FString Error;
	const TSharedPtr<FDreamUITextWriteBack> WriteBack = FDreamUITextWriteBack::Create(File.Path, nullptr, Error);
	if (!TestTrue(FString::Printf(TEXT("the write-back opened the file (%s)"), *Error), WriteBack.IsValid()))
	{
		return false;
	}

	// A marker edit, so the undo below has something real to reach. If the flush recorded an empty
	// entry, the single Ctrl+Z would pop that instead and this text would stay put -- which is
	// exactly the failure the user hits: undo appears to do nothing, they press it again, and the
	// edit before is gone.
	const FString Marker = Source + TEXT("\n// marker\n");
	GEditor->BeginTransaction(FText::FromString(TEXT("WriteBack Marker")));
	WriteBack->GetDocument()->SetContent(Marker, Error);
	GEditor->EndTransaction();
	TestEqualSensitive(TEXT("the marker edit landed"), File.ReadBack(), Marker);

	// The live tree matches the marker text as well (a trailing comment changes no value), so this
	// flush has nothing to say.
	TestTrue(TEXT("the flush reported success"), WriteBack->FlushTree(Live.Tree.Get(), Error));
	TestEqual(TEXT("and wrote nothing"), WriteBack->GetWriteCount(), 0);
	TestEqual(TEXT("having found no property to write"), WriteBack->GetLastEditCount(), 0);
	TestEqualSensitive(TEXT("so the file is untouched"), File.ReadBack(), Marker);

	GEditor->UndoTransaction();
	TestEqualSensitive(TEXT("and one undo reaches the marker edit, not an empty entry"),
		WriteBack->GetDocument()->GetContent(), Source);

	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackEditReachesTheLineTest,
	"DreamGUI.Designer.ADesignerEditReachesTheTextFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackEditReachesTheLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;

	const FString Source = Fixture();
	FBuiltTree Live = BuildTree(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;

	UDreamWidget* Root = Live.Find(TEXT("Root"));
	if (!TestNotNull(TEXT("the root is there"), (UObject*)Root)) return false;
	FDreamUIAnchorData* Anchor = AnchorDataOf(Root);
	if (!TestTrue(TEXT("with an anchor block"), Anchor != nullptr)) return false;

	// The gesture.
	Anchor->SizeDelta = FVector2D(800.0, 480.0);

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the write-back computed an answer"),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}

	// One line, and nothing else. Line 8's alignment and its trailing 标题 comment are part of the
	// assertion: a write-back that reprinted the block would pass a value check and fail this.
	TestEqualSensitive(TEXT("the one line changed and the rest of the file did not"),
		Produced, FixtureWith(5, TEXT("    AnchorData.SizeDelta = (800, 480)")));

	// Converges in one step. A second flush against the same tree must find nothing, or an idle
	// editor would write the file on every gesture end for the rest of the session.
	FString Again;
	FDreamUIDiagnosticBag AgainDiagnostics;
	TestTrue(TEXT("a second pass computed an answer"),
		FDreamUITextWriteBack::ProduceText(Produced, Live.Tree.Get(), Again, AgainDiagnostics));
	TestEqualSensitive(TEXT("and found nothing more to write"), Again, Produced);

	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackInsertsAnUnwrittenPropertyTest,
	"DreamGUI.Designer.AnUnwrittenGeometryPropertyIsInsertedIntoTheBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackInsertsAnUnwrittenPropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;

	// The commonest gesture there is: drag a widget whose file block never mentioned its position,
	// because the default was fine until now. The value has to be inserted, not merely replaced --
	// and the comparison has to reach the CLASS DEFAULT to know that the file said (0, 0) all along.
	const FString Source = Fixture();
	FBuiltTree Live = BuildTree(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;

	FDreamUIAnchorData* Anchor = AnchorDataOf(Live.Find(TEXT("Title")));
	if (!TestTrue(TEXT("Title has an anchor block"), Anchor != nullptr)) return false;
	Anchor->AnchoredPosition = FVector2D(10.0, 20.0);

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the write-back computed an answer"),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}

	TArray<FString> ProducedLines;
	Produced.ParseIntoArray(ProducedLines, TEXT("\n"), false);
	TestEqual(TEXT("exactly one line was added"), ProducedLines.Num(), FixtureLines().Num() + 1);
	TestTrue(TEXT("and it is the property that was missing"),
		ProducedLines.ContainsByPredicate([](const FString& InLine)
		{
			return InLine.TrimStartAndEnd() == TEXT("AnchorData.AnchoredPosition = (10, 20)");
		}));

	// The strongest form of "it was written correctly", and the one that does not depend on how the
	// patcher chose to indent: build the result and read the value back out of it.
	const FBuiltTree RoundTrip = BuildTree(Produced);
	if (TestTrue(TEXT("the produced file still builds"), RoundTrip.Tree.IsValid()))
	{
		const FDreamUIAnchorData* Written = AnchorDataOf(RoundTrip.Find(TEXT("Title")));
		if (TestTrue(TEXT("and still has Title"), Written != nullptr))
		{
			TestTrue(TEXT("holding the value the gesture produced"),
				Written->AnchoredPosition == FVector2D(10.0, 20.0));
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackOneFlushIsOneUndoStepTest,
	"DreamGUI.Designer.OneFlushOfManyPropertiesIsOneUndoStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackOneFlushIsOneUndoStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	const FString Source = Fixture();
	FScopedDuiFile File(TEXT("WriteBackOneUndoStep.dui"), Source);

	FString Error;
	const TSharedPtr<FDreamUITextWriteBack> WriteBack = FDreamUITextWriteBack::Create(File.Path, nullptr, Error);
	if (!TestTrue(FString::Printf(TEXT("the write-back opened the file (%s)"), *Error), WriteBack.IsValid()))
	{
		return false;
	}

	// A marker edit first, so that "one undo" can be told apart from "one undo per property". If
	// the flush below produced three entries, the single Ctrl+Z at the end would land in the middle
	// of its own batch and leave a file that is partly the gesture and partly not -- a state no
	// further undoing reproduces, and the exact reason the patcher takes a batch at all.
	const FString Marker = Source + TEXT("\n// marker\n");
	GEditor->BeginTransaction(FText::FromString(TEXT("WriteBack Marker")));
	WriteBack->GetDocument()->SetContent(Marker, Error);
	GEditor->EndTransaction();

	FBuiltTree Live = BuildTree(Marker);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;

	// Three properties over two nodes, which is what a marquee drag of two widgets looks like.
	FDreamUIAnchorData* RootAnchor = AnchorDataOf(Live.Find(TEXT("Root")));
	FDreamUIAnchorData* TitleAnchor = AnchorDataOf(Live.Find(TEXT("Title")));
	if (!TestTrue(TEXT("Root has an anchor block"), RootAnchor != nullptr)) return false;
	if (!TestTrue(TEXT("Title has an anchor block"), TitleAnchor != nullptr)) return false;
	RootAnchor->SizeDelta = FVector2D(800.0, 480.0);
	TitleAnchor->SizeDelta = FVector2D(300.0, 60.0);
	TitleAnchor->AnchoredPosition = FVector2D(10.0, 20.0);

	TestTrue(TEXT("the flush reported success"), WriteBack->FlushTree(Live.Tree.Get(), Error));
	TestEqual(TEXT("three properties were written"), WriteBack->GetLastEditCount(), 3);
	// One SetContent for the whole flush. Every location in one AST describes the text as it was
	// parsed, so the three edits have to be planned together anyway -- and planning them together
	// is what makes them one entry.
	TestEqual(TEXT("in a single write"), WriteBack->GetWriteCount(), 1);

	const FString AfterFlush = File.ReadBack();
	TestTrue(TEXT("the file changed"), !AfterFlush.Equals(Marker, ESearchCase::CaseSensitive));

	GEditor->UndoTransaction();
	TestEqualSensitive(TEXT("and one undo takes the whole gesture back off, no more and no less"),
		WriteBack->GetDocument()->GetContent(), Marker);
	TestEqualSensitive(TEXT("on disk as well as in memory"), File.ReadBack(), Marker);

	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackBoundPropertyDoesNotSpoilTheBatchTest,
	"DreamGUI.Designer.ABoundPropertyRefusalDoesNotSpoilTheBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackBoundPropertyDoesNotSpoilTheBatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;

	// Until P6 greys them out, the details panel will happily let someone drag a bound property's
	// value. The patcher refuses to overwrite the `<-` with a literal, and the point of this test is
	// that the refusal is LOCAL: the other property in the same flush still lands.
	const FString Source = BoundFixture();
	FBuiltTree Live = BuildTree(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;

	UDreamWidget* Root = Live.Find(TEXT("Root"));
	if (!TestNotNull(TEXT("the root is there"), (UObject*)Root)) return false;
	FDreamUIAnchorData* Anchor = AnchorDataOf(Root);
	if (!TestTrue(TEXT("with an anchor block"), Anchor != nullptr)) return false;

	TestTrue(TEXT("the bound property could be poked"), PokeFloat(Root, TEXT("RenderOpacity"), 0.5f));
	Anchor->SizeDelta = FVector2D(800.0, 480.0);

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the write-back still computed an answer"),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}

	TestEqualSensitive(TEXT("the writable property landed and the binding was left alone"),
		Produced,
		Join({
			TEXT("Widget Root {"),
			TEXT("    RenderOpacity <- GetFade()"),
			TEXT("    AnchorData.SizeDelta = (800, 480)"),
			TEXT("}")
		}));

	// Refused, not silently skipped. A drag that does not stick and says nothing is the failure this
	// code is expected to have until the details panel disables the property.
	TestTrue(TEXT("and the refusal was reported"),
		HasDiagnostic(Diagnostics, EDreamUIDiagnosticCode::PatchTargetNotFound));

	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackOneDocumentPerFileTest,
	"DreamGUI.Designer.OneDocumentPerFileNotPerEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackOneDocumentPerFileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;

	// Two toolkits on one `.dui` with a document each hold two Contents and two disk hashes: A's
	// undo writes the file, B's watcher sees text B never wrote, B reloads and pushes its own idea
	// back, and alternating Ctrl+Z between the windows never converges. One file, one document.
	const int32 Baseline = FDreamUIDocumentRegistry::NumTracked();

	FScopedDuiFile FileA(TEXT("WriteBackRegistryA.dui"), Fixture());
	FScopedDuiFile FileB(TEXT("WriteBackRegistryB.dui"), Fixture());

	FString Error;
	FDreamUIDocumentHandle First = FDreamUIDocumentHandle::Open(FileA.Path, Error);
	if (!TestTrue(FString::Printf(TEXT("the first handle opened (%s)"), *Error), First.IsValid()))
	{
		return false;
	}

	// The SAME file reached by a spelling nobody would call equal. A map keyed on the raw string
	// would hand this one a second document, which is the whole failure with an extra step.
	const FString Roundabout = FPaths::GetPath(FileA.Path) / TEXT("..") / FPaths::GetCleanFilename(FPaths::GetPath(FileA.Path))
		/ FPaths::GetCleanFilename(FileA.Path);
	FDreamUIDocumentHandle Second = FDreamUIDocumentHandle::Open(Roundabout, Error);
	if (!TestTrue(FString::Printf(TEXT("the second handle opened (%s)"), *Error), Second.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("both handles are the same document"), Second.Get() == First.Get());
	TestEqual(TEXT("and that is one tracked file"), FDreamUIDocumentRegistry::NumTracked(), Baseline + 1);

	FDreamUIDocumentHandle Other = FDreamUIDocumentHandle::Open(FileB.Path, Error);
	if (TestTrue(FString::Printf(TEXT("a handle on another file opened (%s)"), *Error), Other.IsValid()))
	{
		TestTrue(TEXT("a different file is a different document"), Other.Get() != First.Get());
		TestEqual(TEXT("tracked separately"), FDreamUIDocumentRegistry::NumTracked(), Baseline + 2);
	}

	// The last user leaving is what clears the entry. Anything weaker and a `.dui` edited outside
	// the editor after its window closed would come back to a document still holding the old text.
	Other.Reset();
	Second.Reset();
	TestEqual(TEXT("one handle left still holds the entry"), FDreamUIDocumentRegistry::NumTracked(), Baseline + 1);
	TestTrue(TEXT("and the document is still findable"), FDreamUIDocumentRegistry::Find(FileA.Path) != nullptr);

	First.Reset();
	TestEqual(TEXT("and the last one clears it"), FDreamUIDocumentRegistry::NumTracked(), Baseline);
	TestTrue(TEXT("so the next open reads the file again"), FDreamUIDocumentRegistry::Find(FileA.Path) == nullptr);

	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackUndoDefersItsRebuildTest,
	"DreamGUI.Designer.AnUndoDefersItsRebuildToTheNextTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackUndoDefersItsRebuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;
	if (!HasTransactionBuffer(*this)) return false;

	const FString Source = Fixture();
	FScopedDuiFile File(TEXT("WriteBackDeferredRebuild.dui"), Source);

	FString Error;
	const TSharedPtr<FDreamUITextWriteBack> WriteBack = FDreamUITextWriteBack::Create(File.Path, nullptr, Error);
	if (!TestTrue(FString::Printf(TEXT("the write-back opened the file (%s)"), *Error), WriteBack.IsValid()))
	{
		return false;
	}

	TArray<EDreamUIDocumentChangeReason> Seen;
	WriteBack->OnRebuildRequested().AddLambda(
		[&Seen](EDreamUIDocumentChangeReason InReason) { Seen.Add(InReason); });

	// An ordinary edit from somewhere else -- a text panel, a tool. Nothing is mid-restore, so the
	// rebuild is asked for straight away.
	const FString Edited = FixtureWith(5, TEXT("    AnchorData.SizeDelta = (800, 480)"));
	GEditor->BeginTransaction(FText::FromString(TEXT("WriteBack Foreign Edit")));
	WriteBack->GetDocument()->SetContent(Edited, Error);
	GEditor->EndTransaction();

	if (!TestEqual(TEXT("an ordinary edit asks for a rebuild immediately"), Seen.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("naming the reason"), (int32)Seen[0], (int32)EDreamUIDocumentChangeReason::Edited);

	// And now the case this exists for. UDreamUIDocument broadcasts from PostEditUndo, which runs
	// inside FTransaction::Apply's loop -- the widgets whose values this text describes may not have
	// been restored yet, so a tree rebuilt at that moment is overwritten by the rest of the restore
	// and ends up matching neither state.
	GEditor->UndoTransaction();
	TestEqualSensitive(TEXT("the undo landed"), WriteBack->GetDocument()->GetContent(), Source);
	TestEqual(TEXT("but no rebuild was asked for from inside it"), Seen.Num(), 1);
	TestTrue(TEXT("it is owed instead"), WriteBack->IsRebuildPending());

	// The ticker calls exactly this, and calling it here is what lets the deferral be asserted
	// rather than waited for.
	WriteBack->ProcessDeferredRebuild();
	if (TestEqual(TEXT("and arrives once Apply has finished"), Seen.Num(), 2))
	{
		TestEqual(TEXT("naming undo as the reason"), (int32)Seen[1], (int32)EDreamUIDocumentChangeReason::UndoRedo);
	}
	TestFalse(TEXT("with nothing left owed"), WriteBack->IsRebuildPending());

	// Twice in a row is not two rebuilds: one transaction can restore the document more than once,
	// and only the text after Apply finishes is worth building from.
	WriteBack->ProcessDeferredRebuild();
	TestEqual(TEXT("and a second pump does nothing"), Seen.Num(), 2);

	return true;
}


// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackRotationAndScaleTest,
	"DreamGUI.Designer.ARotationAndAScaleReachTheTextFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The user-shaped case this pipeline shipped without: rotate a widget, save, and the .dui carries
 * it. Neither property is written in the fixture, so both go through the insertion path -- and the
 * rotation goes through the transient-mirror pair, which is where three separate implementations
 * used to drop it (the compared set, the transient refusal, the raw write past the setter).
 */
bool FDreamUIWriteBackRotationAndScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWriteBackTestLocal;

	const FString Source = Fixture();
	FBuiltTree Live = BuildTree(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;

	UDreamWidget* Title = Live.Find(TEXT("Title"));
	if (!TestNotNull(TEXT("the title is there"), (UObject*)Title)) return false;

	// The gesture, exactly as the setters deliver it: rotation through the euler (what the .dui
	// spells), scale straight in.
	Title->SetRelativeRotationEuler(FRotator(0, 0, 30));
	Title->SetRelativeScale(FVector(2, 2, 1));

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the write-back computed an answer"),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}

	TestTrue(TEXT("the rotation reached the file, spelled as the euler"),
		Produced.Contains(TEXT("RelativeRotationEuler = (0, 0, 30)")));
	TestTrue(TEXT("and so did the scale"),
		Produced.Contains(TEXT("RelativeScale = (2, 2, 1)")));
	TestFalse(TEXT("and the quaternion, which has no spelling, was not smuggled in as ExportText"),
		Produced.Contains(TEXT("RelativeRotation =")));

	// Converges in one step, like every other property: a second flush against the same tree finds
	// nothing, or an idle editor rewrites the file on every gesture end for the rest of the session.
	// For the rotation pair this is the assertion that matters most -- it fails if printing reads a
	// different field than parsing writes.
	FString Again;
	FDreamUIDiagnosticBag AgainDiagnostics;
	TestTrue(TEXT("a second pass computed an answer"),
		FDreamUITextWriteBack::ProduceText(Produced, Live.Tree.Get(), Again, AgainDiagnostics));
	TestEqualSensitive(TEXT("and found nothing more to write"), Again, Produced);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
