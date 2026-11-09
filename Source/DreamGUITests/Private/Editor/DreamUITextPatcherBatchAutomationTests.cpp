// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextPatcher.h"

/*
 * Several new lines for one block in one write (FDreamUITextPatcher::SetProperties), as a pasted layer style or an applied
 * preset makes them: a block written on one line takes them all, one after the other in the order they were asked for, and
 * its brace goes down onto a line of its own once -- the lines do not each take the space before the brace, which would be
 * two edits on the same characters and refuse the whole write.
 */
namespace DreamUITextPatcherBatchTestLocal
{
	FString MakeSource(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	FDreamUIPropertyEdit Edit(const TCHAR* InNodeId, EDreamUIPatchTarget InTarget, int32 InComponentIndex, const TCHAR* InName, const TCHAR* InValue)
	{
		FDreamUIPropertyEdit Out;
		Out.NodeId = InNodeId;
		Out.Target = InTarget;
		Out.ComponentIndex = InComponentIndex;
		Out.PropertyName = InName;
		Out.NewValueText = InValue;
		return Out;
	}

	bool Parse(const FString& InText, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		OutDiagnostics.SourceName = TEXT("Batch.dui");
		return FDreamUISourceFile::Parse(InText, OutDiagnostics.SourceName, OutAst, OutDiagnostics);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherSeveralLinesIntoAOneLineBlockTest,
	"DreamGUI.Text.SeveralLinesWrittenIntoABlockOnOneLineInOneWriteAllLand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPatcherSeveralLinesIntoAOneLineBlockTest::RunTest(const FString&)
{
	using namespace DreamUITextPatcherBatchTestLocal;

	FString Text = MakeSource({
		TEXT("Widget Root {"),
		TEXT("    Text Title { Text = \"Hello\" }"),
		TEXT("    RectBlock Card { }"),
		TEXT("    RectBlock Styled {"),
		TEXT("        + DreamLayerStyle { Style.OuterGlow.bEnabled = true }"),
		TEXT("    }"),
		TEXT("}")
	});
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the fixture parses"), Parse(Text, Ast, Diagnostics))) return false;

	const TArray<FDreamUIPropertyEdit> Edits = {
		Edit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("HAlign"), TEXT("Left")),
		Edit(TEXT("Card"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Color"), TEXT("#FF0000")),
		Edit(TEXT("Title"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("24")),
		Edit(TEXT("Card"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("BlendMode"), TEXT("Multiply")),
		// A value replaced on a one-line block, beside two lines added to it.
		Edit(TEXT("Styled"), EDreamUIPatchTarget::Component, 0, TEXT("Style.OuterGlow.bEnabled"), TEXT("false")),
		Edit(TEXT("Styled"), EDreamUIPatchTarget::Component, 0, TEXT("Style.OuterGlow.Size"), TEXT("14")),
		Edit(TEXT("Styled"), EDreamUIPatchTarget::Component, 0, TEXT("Style.Scale"), TEXT("2")),
	};
	FDreamUIDiagnosticBag PatchDiagnostics;
	TestTrue(TEXT("the whole write lands"), FDreamUITextPatcher::SetProperties(Text, Ast, Edits, PatchDiagnostics));
	TestEqual(TEXT("with nothing refused"), PatchDiagnostics.Diagnostics.Num(), 0);
	TestEqual(TEXT("every line in its block, in the order asked for, each block's brace on a line of its own"), Text,
		MakeSource({
			TEXT("Widget Root {"),
			TEXT("    Text Title { Text = \"Hello\""),
			TEXT("        HAlign = Left"),
			TEXT("        FontSize = 24"),
			TEXT("    }"),
			TEXT("    RectBlock Card {"),
			TEXT("        Color = #FF0000"),
			TEXT("        BlendMode = Multiply"),
			TEXT("    }"),
			TEXT("    RectBlock Styled {"),
			TEXT("        + DreamLayerStyle { Style.OuterGlow.bEnabled = false"),
			TEXT("            Style.OuterGlow.Size = 14"),
			TEXT("            Style.Scale = 2"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")
		}));

	FDreamUIAst After;
	FDreamUIDiagnosticBag AfterDiagnostics;
	TestTrue(TEXT("and the file still parses"), Parse(Text, After, AfterDiagnostics));
	TestEqual(TEXT("with nothing to complain about"), AfterDiagnostics.Diagnostics.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPatcherBehaviourIntoAOneLineBlockTest,
	"DreamGUI.Text.ABehaviourAddedToANodeWrittenOnOneLineIsTheNodesNotItsParents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A `+` line added to a node whose block opens and closes on its header line -- empty, or with values -- goes inside the
 * block, in front of the brace, which moves down onto its own line. The end of that line is outside the block: a line
 * written there would be a behaviour of the parent, a file that still parses and builds the wrong thing.
 */
bool FDreamUIPatcherBehaviourIntoAOneLineBlockTest::RunTest(const FString&)
{
	using namespace DreamUITextPatcherBatchTestLocal;

	FString Text = MakeSource({
		TEXT("Widget Root {"),
		TEXT("    RectBlock Card { }"),
		TEXT("    Text Title { Text = \"Hello\" }"),
		TEXT("}")
	});
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the fixture parses"), Parse(Text, Ast, Diagnostics))) return false;

	TArray<FDreamUIStructuralEdit> Edits;
	for (const TCHAR* Node : { TEXT("Card"), TEXT("Title") })
	{
		FDreamUIStructuralEdit& Edit = Edits.AddDefaulted_GetRef();
		Edit.Kind = EDreamUIStructuralEditKind::InsertComponent;
		Edit.NodeId = Node;
		Edit.ComponentClassName = TEXT("DreamLayerStyle");
	}
	FDreamUIDiagnosticBag PatchDiagnostics;
	TestTrue(TEXT("both lines are written"), FDreamUITextPatcher::ApplyStructuralEdits(Text, Ast, Edits, PatchDiagnostics));
	TestEqual(TEXT("each inside its own node's block"), Text,
		MakeSource({
			TEXT("Widget Root {"),
			TEXT("    RectBlock Card {"),
			TEXT("        + DreamLayerStyle { }"),
			TEXT("    }"),
			TEXT("    Text Title { Text = \"Hello\""),
			TEXT("        + DreamLayerStyle { }"),
			TEXT("    }"),
			TEXT("}")
		}));

	FDreamUIAst After;
	FDreamUIDiagnosticBag AfterDiagnostics;
	if (TestTrue(TEXT("the file still parses"), Parse(Text, After, AfterDiagnostics)))
	{
		TestTrue(TEXT("and the root has no behaviour of its own"), After.Root.Components.IsEmpty());
		for (const FDreamUINode& Child : After.Root.Children)
		{
			TestEqual(*FString::Printf(TEXT("%s has the line"), *Child.Id), Child.Components.Num(), 1);
		}
	}
	return true;
}

#endif
