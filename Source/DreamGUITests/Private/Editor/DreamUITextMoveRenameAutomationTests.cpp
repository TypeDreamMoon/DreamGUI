// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"
#include "DreamWidgetBehaviourTestTypes.h"
#include "DreamWidgetBlueprintTestTypes.h"

#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"
#include "Core/DreamUIAnchorData.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamPanelLayouts.h"

#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#include "UObject/StrongObjectPtr.h"

/*
 * Moving and renaming on a `.dui`-backed hierarchy: the two structural edits the trees cannot be read
 * for, so the gestures queue them (FDreamUITextWriteBack::QueueStructuralEdit) and the flush writes them
 * first, as they were made. Each designer test reads the file off the disk after the gesture, the way the
 * next compile will.
 *
 * Underneath, without a designer: the queue itself, what an UNqueued rename leaves (nothing lost, and said),
 * and the one rule a move has that no other edit has -- a widget an `if` shows stays in its branch.
 */

namespace DreamUITextMoveRenameTestLocal
{
	using namespace DreamUITextStructureFixtures;

	/** A tree built from text, standing in for the designer's template. */
	struct FLiveTree
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		explicit FLiveTree(const FString& InText)
		{
			FDreamUIAst Ast;
			FDreamUIDiagnosticBag Diagnostics;
			Diagnostics.SourceName = TEXT("MoveRename.dui");
			Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Ast, Diagnostics));
		}

		~FLiveTree()
		{
			if (Tree.IsValid())
			{
				FDreamUITextWriteBack::ClearQueuedStructuralEdits(Tree.Get());
			}
		}

		UDreamWidget* Find(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}
	};

	FString Lines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	/** A node's widget children as `A,B,C`, so an order is compared as one value. */
	FString Order(const FDreamUINode* InParent)
	{
		return FString::Join(ChildIds(InParent), TEXT(","));
	}

}

// =================================================================================================
// The designer
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextReorderTest,
	"DreamGUI.Designer.Text.AReorderInsideOneParentMovesTheBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextReorderTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	FScopedDuiFile Source(TEXT("MoveReorder.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Column {"),
		TEXT("        + VerticalBox"),
		TEXT("        Widget First {"),
		TEXT("        }"),
		TEXT("        Widget Second {"),
		TEXT("        }"),
		TEXT("        Widget Third {"),
		TEXT("            AnchorData.SizeDelta = (30, 10)"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_MoveReorder"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;

	// Third to the front, then First to the back: one order the file has to follow twice.
	TestTrue(TEXT("the first reorder went through"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint,
		Scoped.FindTemplate(TEXT("Third")), Scoped.FindTemplate(TEXT("Column")), 0));
	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), Ast))) return false;
	TestEqual(TEXT("the moved node is first in the file"), Order(FindNode(Ast, TEXT("Column"))),
		FString(TEXT("Third,First,Second")));
	TestTrue(TEXT("with its value"), Source.Read().Contains(TEXT("AnchorData.SizeDelta = (30, 10)")));

	// The last place by index: -1 on a widget already in this parent leaves it where it is.
	UDreamWidget* ColumnNow = Scoped.FindTemplate(TEXT("Column"));
	if (!TestNotNull(TEXT("the column is still there"), ColumnNow)) return false;
	TestTrue(TEXT("the second reorder went through"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint,
		Scoped.FindTemplate(TEXT("First")), ColumnNow, ColumnNow->GetChildren().Num() - 1));
	FDreamUIAst After;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), After))) return false;
	TestEqual(TEXT("and now last"), Order(FindNode(After, TEXT("Column"))),
		FString(TEXT("Third,Second,First")));

	// The compiled tree follows the file, which is what makes the order the author sees the order kept.
	UDreamWidget* Column = Scoped.FindTemplate(TEXT("Column"));
	if (TestNotNull(TEXT("the column is in the compiled tree"), Column) && TestEqual(TEXT("with three children"), Column->GetChildren().Num(), 3))
	{
		TestEqual(TEXT("in the file's order"), Column->GetChildren()[0]->GetDisplayName(), FString(TEXT("Third")));
		TestEqual(TEXT("to the end"), Column->GetChildren()[2]->GetDisplayName(), FString(TEXT("First")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextMoveAcrossTest,
	"DreamGUI.Designer.Text.AMoveToAnotherParentCarriesTheBlockAndUndoesWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextMoveAcrossTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	FScopedDuiFile Source(TEXT("MoveAcross.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Left {"),
		TEXT("        + VerticalBox"),
		TEXT("        Widget Card {"),
		TEXT("            AnchorData.SizeDelta = (50, 20)"),
		TEXT("            Widget Inner {"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    Widget Right {"),
		TEXT("        + VerticalBox"),
		TEXT("        Widget Resident {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_MoveAcross"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const FString Before = Source.Read();

	{
		const FScopedTransaction Transaction(NSLOCTEXT("DreamUITextMoveRenameTests", "Move", "Move"));
		TestTrue(TEXT("the move went through"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint,
			Scoped.FindTemplate(TEXT("Card")), Scoped.FindTemplate(TEXT("Right")), 0));
	}

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), Ast))) return false;
	TestEqual(TEXT("the card is first in its new parent"), Order(FindNode(Ast, TEXT("Right"))),
		FString(TEXT("Card,Resident")));
	TestFalse(TEXT("and gone from the old one"), HoldsChild(FindNode(Ast, TEXT("Left")), TEXT("Card")));
	TestTrue(TEXT("with its child inside it"), HoldsChild(FindNode(Ast, TEXT("Card")), TEXT("Inner")));
	TestTrue(TEXT("and its value"), Source.Read().Contains(TEXT("AnchorData.SizeDelta = (50, 20)")));
	UDreamWidget* Card = Scoped.FindTemplate(TEXT("Card"));
	if (TestNotNull(TEXT("the compiled tree has the card"), Card))
	{
		TestTrue(TEXT("under its new parent"), Card->GetParent() == Scoped.FindTemplate(TEXT("Right")));
	}

	if (!TestTrue(TEXT("there is something to undo"), GEditor->UndoTransaction())) return false;
	TestEqual(TEXT("the undo puts the file back exactly"), Source.Read(), Before);
	Card = Scoped.FindTemplate(TEXT("Card"));
	if (TestNotNull(TEXT("and the card"), Card))
	{
		TestTrue(TEXT("under its old parent"), Card->GetParent() == Scoped.FindTemplate(TEXT("Left")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextRenameTest,
	"DreamGUI.Designer.Text.ARenameIsWrittenWithWasAndKeepsWhatTheNodeOwns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextRenameTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	const FString EventBehaviourPath = UDreamUIEventTestBehaviour::StaticClass()->GetPathName();
	FScopedDuiFile Source(TEXT("Rename.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel { }"),
		TEXT("    Widget Button {"),
		TEXT("        AnchorData.SizeDelta = (80, 24)"),
		FString::Printf(TEXT("        + %s {"), *EventBehaviourPath),
		TEXT("            OnPoked -> HandlePoked"),
		TEXT("        }"),
		TEXT("        Widget Label {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_Rename"), Source.FilePath, /*bInOpenDesigner*/true,
		UDreamUIEventTestUserWidget::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	TestEqual(TEXT("the rename went through"), DreamWidgetTreeEditing::RenameWidget(Scoped.Blueprint,
		Scoped.FindTemplate(TEXT("Button")), TEXT("Pusher")), FString(TEXT("Pusher")));
	FString Text = Source.Read();
	TestTrue(TEXT("written as a rename, with where it came from"), Text.Contains(TEXT("Widget Pusher (was: Button)")));
	TestTrue(TEXT("its block is all still there: its value"), Text.Contains(TEXT("AnchorData.SizeDelta = (80, 24)")));
	TestTrue(TEXT("its route"), Text.Contains(TEXT("OnPoked -> HandlePoked")));
	FDreamUIAst Renamed;
	if (TestTrue(TEXT("the file still parses"), ParseText(Text, Renamed)))
	{
		TestTrue(TEXT("and its child"), HoldsChild(FindNode(Renamed, TEXT("Pusher")), TEXT("Label")));
	}
	TestNotNull(TEXT("the compiled tree has it under the new name"), Scoped.FindTemplate(TEXT("Pusher")));
	TestNull(TEXT("and not under the old one"), Scoped.FindTemplate(TEXT("Button")));

	// The route still reaches the handler through the renamed widget, once the class is compiled in full.
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("the renamed file compiles clean"), Results.NumErrors, 0);
	const UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Scoped.Blueprint->GeneratedClass);
	if (TestNotNull(TEXT("the class is a widget class"), Generated))
	{
		TestTrue(TEXT("and its route is bound through the renamed widget"),
			Generated->GetEventBindings().ContainsByPredicate([](const FDreamWidgetEventBinding& InBinding)
			{
				return InBinding.WidgetName == FName(TEXT("Pusher"));
			}));
		TestFalse(TEXT("not through the old name"),
			Generated->GetEventBindings().ContainsByPredicate([](const FDreamWidgetEventBinding& InBinding)
			{
				return InBinding.WidgetName == FName(TEXT("Button"));
			}));
	}

	// A second rename migrates from where the node STARTED: one hop for everything still pointing at it.
	TestEqual(TEXT("the second rename went through"), DreamWidgetTreeEditing::RenameWidget(Scoped.Blueprint,
		Scoped.FindTemplate(TEXT("Pusher")), TEXT("Presser")), FString(TEXT("Presser")));
	Text = Source.Read();
	TestTrue(TEXT("the clause keeps the first name"), Text.Contains(TEXT("Widget Presser (was: Button)")));
	TestFalse(TEXT("not the one in between"), Text.Contains(TEXT("(was: Pusher)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextRenameRefusalsTest,
	"DreamGUI.Designer.Text.ARenameATimelineNamesOrAMoveOutOfAnIfIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextRenameRefusalsTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	FScopedDuiFile Source(TEXT("RenameRefusals.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Child {"),
		TEXT("    }"),
		TEXT("}"),
		TEXT("timeline Pulse {"),
		TEXT("    duration = 0.6"),
		TEXT("    Child.RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1)"),
		TEXT("}")
	})))
	{
		return false;
	}
	// Built without a designer: the check reads the file, and only the parse is asked for.
	FScopedTextBlueprint Scoped(TEXT("BP_RenameRefusals"), Source.FilePath, /*bInOpenDesigner*/false);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;

	FDreamUIStructuralEdit Rename;
	Rename.Kind = EDreamUIStructuralEditKind::RenameNode;
	Rename.NodeId = TEXT("Child");
	Rename.NewId = TEXT("Renamed");
	FText Reason;
	TestFalse(TEXT("renaming a node a timeline drives by name is refused"),
		DreamUITextAuthoring::CheckStructuralEdit(Scoped.Blueprint, Rename, /*bInVerifyBuild*/false, Reason));
	TestTrue(TEXT("and the reason names the timeline"), Reason.ToString().Contains(TEXT("Pulse")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextWrapTest,
	"DreamGUI.Designer.Text.AWidgetMadeAndFilledInOneGestureIsWrittenWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextWrapTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	// A wrap's shape: a new widget, and existing ones moved into it, in one structural batch. The moves are
	// queued before the wrapper is in the file, so they wait for the shape pass to write it.
	FScopedDuiFile Source(TEXT("MoveWrap.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Alpha {"),
		TEXT("    }"),
		TEXT("    Widget Beta {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_MoveWrap"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	{
		DreamWidgetTreeEditing::FScopedStructureChangeBatch Batch;
		UDreamWidget* Wrapper = DreamWidgetTreeEditing::CreateWidget(Scoped.Blueprint, UDreamWidget::StaticClass(),
			Scoped.FindTemplate(TEXT("Root")), 0, TEXT("Wrapper"));
		if (!TestNotNull(TEXT("the wrapper was made"), Wrapper)) return false;
		Wrapper->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
		TestTrue(TEXT("the first moved in"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint,
			Scoped.FindTemplate(TEXT("Alpha")), Wrapper, -1));
		TestTrue(TEXT("and the second"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint,
			Scoped.FindTemplate(TEXT("Beta")), Wrapper, -1));
	}

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), Ast))) return false;
	TestEqual(TEXT("the wrapper is written, holding both, in order"), Order(FindNode(Ast, TEXT("Wrapper"))),
		FString(TEXT("Alpha,Beta")));
	TestEqual(TEXT("and it is all the root holds now"), Order(&Ast.Root), FString(TEXT("Wrapper")));
	UDreamWidget* Alpha = Scoped.FindTemplate(TEXT("Alpha"));
	TestTrue(TEXT("the compiled tree agrees"), Alpha != nullptr && Alpha->GetParent() == Scoped.FindTemplate(TEXT("Wrapper")));
	return true;
}

// =================================================================================================
// Underneath
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackQueuedRenameTest,
	"DreamGUI.Text.WriteBack.AQueuedRenameKeepsTheBlockAndAnUnqueuedOneLosesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackQueuedRenameTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Card {"),
		TEXT("        AnchorData.SizeDelta = (11, 22)"),
		TEXT("        Widget Inner {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	});

	{
		const FLiveTree Live(Source);
		if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;
		Live.Find(TEXT("Card"))->SetDisplayName(TEXT("Panel"));
		FDreamUIStructuralEdit Rename;
		Rename.Kind = EDreamUIStructuralEditKind::RenameNode;
		Rename.NodeId = TEXT("Card");
		Rename.NewId = TEXT("Panel");
		FDreamUITextWriteBack::QueueStructuralEdit(Live.Tree.Get(), Rename);

		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics,
			nullptr, nullptr, &Residue));
		TestTrue(TEXT("the queued rename is written as one"), Updated.Contains(TEXT("Widget Panel (was: Card)")));
		TestTrue(TEXT("with the block it had"), Updated.Contains(TEXT("AnchorData.SizeDelta = (11, 22)")) && Updated.Contains(TEXT("Widget Inner")));
		TestTrue(TEXT("and nothing left over"), Residue.IsEmpty());
	}

	// The same rename that nobody queued: read off the trees it is Card gone and Panel arrived, and writing
	// that would delete Card's block with Inner in it. Card stays, and the residue says what did not land.
	{
		const FLiveTree Live(Source);
		if (!TestTrue(TEXT("the fixture builds again"), Live.Tree.IsValid())) return false;
		Live.Find(TEXT("Card"))->SetDisplayName(TEXT("Panel"));

		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUIStructuralResidue Residue;
		TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics,
			nullptr, nullptr, &Residue));
		TestTrue(TEXT("the old block is kept, value and child and all"),
			Updated.Contains(TEXT("Widget Card")) && Updated.Contains(TEXT("AnchorData.SizeDelta = (11, 22)")) && Updated.Contains(TEXT("Widget Inner")));
		TestFalse(TEXT("and the residue says so"), Residue.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWriteBackQueuedMoveTest,
	"DreamGUI.Text.WriteBack.AQueuedMoveIsPlacedWhereTheWidgetNowStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWriteBackQueuedMoveTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget First {"),
		TEXT("    }"),
		TEXT("    Widget Second {"),
		TEXT("    }"),
		TEXT("    Widget Third {"),
		TEXT("    }"),
		TEXT("}")
	});
	const FLiveTree Live(Source);
	if (!TestTrue(TEXT("the fixture builds"), Live.Tree.IsValid())) return false;
	UDreamWidget* Root = Live.Find(TEXT("Root"));
	UDreamWidget* Third = Live.Find(TEXT("Third"));
	if (!TestTrue(TEXT("the widgets are there"), Root != nullptr && Third != nullptr)) return false;
	TestTrue(TEXT("the live move went through"), Third->TrySetParent(Root, /*bKeepWorldPosition*/false, 1));
	FDreamUIStructuralEdit Move;
	Move.Kind = EDreamUIStructuralEditKind::MoveNode;
	Move.NodeId = TEXT("Third");
	FDreamUITextWriteBack::QueueStructuralEdit(Live.Tree.Get(), Move);

	FString Updated;
	FDreamUIDiagnosticBag Diagnostics;
	FDreamUIStructuralResidue Residue;
	TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics,
		nullptr, nullptr, &Residue));
	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the written file parses"), ParseText(Updated, Ast))) return false;
	TestEqual(TEXT("the moved node stands where the tree has it"), Order(&Ast.Root),
		FString(TEXT("First,Third,Second")));
	TestTrue(TEXT("nothing left over"), Residue.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherMoveIfTest,
	"DreamGUI.Text.Patcher.AWidgetAnIfShowsIsNotMovedOutOfItsBranchNorAnotherIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherMoveIfTest::RunTest(const FString&)
{
	using namespace DreamUITextMoveRenameTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    if HasSave() {"),
		TEXT("        Text Continue {"),
		TEXT("        }"),
		TEXT("    } else {"),
		TEXT("        Text NoSave {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    Text Footer {"),
		TEXT("    }"),
		TEXT("}")
	});
	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the fixture parses"), ParseText(Source, Ast))) return false;

	auto TryMove = [&Ast, &Source](const TCHAR* InNode, const TCHAR* InBefore, FString& OutText, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FDreamUIStructuralEdit Move;
		Move.Kind = EDreamUIStructuralEditKind::MoveNode;
		Move.NodeId = InNode;
		Move.BeforeId = InBefore;
		OutText = Source;
		return FDreamUITextPatcher::ApplyStructuralEdits(OutText, Ast, MakeArrayView(&Move, 1), OutDiagnostics);
	};

	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("a widget in a branch is not moved out of it"), TryMove(TEXT("Continue"), TEXT("Footer"), Text, Diagnostics));
		TestEqual(TEXT("and the text is untouched"), Text, Source);
		TestTrue(TEXT("with the not-writable code"), Diagnostics.Diagnostics.ContainsByPredicate([](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == EDreamUIDiagnosticCode::PatchSyntaxNotWritable;
		}));
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestFalse(TEXT("nor another widget into the middle of the if"), TryMove(TEXT("Footer"), TEXT("NoSave"), Text, Diagnostics));
		TestEqual(TEXT("and the text is untouched"), Text, Source);
	}
	{
		FString Text;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("but in front of the whole if is a place"), TryMove(TEXT("Footer"), TEXT("Continue"), Text, Diagnostics));
		FDreamUIAst Moved;
		if (TestTrue(TEXT("the written file parses"), ParseText(Text, Moved)))
		{
			TestEqual(TEXT("the footer stands first"), ChildIds(&Moved.Root).Num() > 0 ? ChildIds(&Moved.Root)[0] : FString(), FString(TEXT("Footer")));
		}
	}
	return true;
}

#endif
