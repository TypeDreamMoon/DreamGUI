// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamRingMenu.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "UObject/StrongObjectPtr.h"

/*
 * The designer's write-back, and the Blueprint converter built on it, against `@tag` lines, `@slot Size`, struct items
 * written as maps and style clauses of several names. The rule is the patcher's: what comes out parses, and only what
 * was edited moved -- so whole files are compared. Opening a file that uses all four writes nothing; an edit lands on
 * the line that says it, in the spelling it was written in; and a tree written out from nothing (what converting a
 * Blueprint does) comes out in the short spellings.
 */
namespace DreamUITagsAndShorthandsWriteBackTestLocal
{
	FString Join(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	bool Parse(const FString& InText, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(InText, TEXT("Shorthands.dui"), OutAst, OutDiagnostics);
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate(
			[InCode](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.Code == InCode; });
	}

	/** Parse, then a batch of property edits against that parse. */
	bool PatchAll(FString& InOutText, const TArray<FDreamUIPropertyEdit>& InEdits, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!Parse(InOutText, Ast, ParseDiagnostics))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::None, FDreamUISourceLocation(),
				FString::Printf(TEXT("the fixture did not parse: %s"), *ParseDiagnostics.ToString()));
			return false;
		}
		return FDreamUITextPatcher::SetProperties(InOutText, Ast, InEdits, OutDiagnostics);
	}

	FDreamUIPropertyEdit Edit(const TCHAR* InNodeId, EDreamUIPatchTarget InTarget, const TCHAR* InName, const TCHAR* InValue)
	{
		FDreamUIPropertyEdit Result;
		Result.NodeId = InNodeId;
		Result.Target = InTarget;
		Result.PropertyName = InName;
		Result.NewValueText = InValue;
		return Result;
	}

	struct FBuiltTree
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		UDreamWidget* Find(const FString& InId) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&InId, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InId)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}
	};

	FBuiltTree BuildLive(const FString& InText)
	{
		FBuiltTree Built;
		Built.Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Built.Ast, Built.Diagnostics));
		return Built;
	}

	TArray<FString> EveryShorthandLines()
	{
		return {
			TEXT("style Base { RenderOpacity = 0.5 }"),                     //  1
			TEXT("style Loud : Base { Visibility = Hidden }"),              //  2
			TEXT("style Card : Base : Loud { }"),                           //  3
			TEXT("Widget Root {"),                                          //  4
			TEXT("    + VerticalBox { }"),                                  //  5
			TEXT("    Widget Trim : Card : Base {"),                        //  6
			TEXT("        @tag Trim, UI.Glow"),                             //  7
			TEXT("        @slot Size = (120, 48)"),                         //  8
			TEXT("    }"),                                                  //  9
			TEXT("    Widget Boxed {"),                                     // 10
			TEXT("        @tag Trim"),                                      // 11
			TEXT("        @slot { Size = (64, 64)  Padding = (4, 0, 4, 0) }"), // 12
			TEXT("    }"),                                                  // 13
			TEXT("    Native.RingMenu Wheel {"),                            // 14
			TEXT("        Items = [{ \"Lamp\", Tag: Lamp }, { \"Door\", Tag: Door, bEnabled: false }]"), // 15
			TEXT("    }"),                                                  // 16
			TEXT("}"),                                                      // 17
		};
	}
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIShorthandRoundTripTest,
	"DreamGUI.Text.Syntax.AFileUsingTagsSizeItemMapsAndSeveralStylesComesBackByteForByte",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIShorthandRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsWriteBackTestLocal;
	const FString Source = Join(EveryShorthandLines());
	FBuiltTree Live = BuildLive(Source);
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Live.Diagnostics.ToString()), Live.Tree.IsValid()))
	{
		return false;
	}
	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	TArray<FDreamUIPropertyEdit> Edits;
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics, &Edits)))
	{
		return false;
	}
	TestEqualSensitive(TEXT("and left the file exactly as it was"), Produced, Source);
	for (const FDreamUIPropertyEdit& Attempted : Edits)
	{
		AddError(FString::Printf(TEXT("an edit was attempted: %s.%s = %s"), *Attempted.NodeId, *Attempted.PropertyName, *Attempted.NewValueText));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIShorthandDesignerEditsTest,
	"DreamGUI.Text.Syntax.DesignerEditsOfTagsASizeAndItemsRewriteTheShorthandsThatSayThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIShorthandDesignerEditsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsWriteBackTestLocal;
	const TArray<FString> Lines = EveryShorthandLines();
	const FString Source = Join(Lines);
	FBuiltTree Live = BuildLive(Source);
	UDreamWidget* Trim = Live.Find(TEXT("Trim"));
	UDreamWidget* Boxed = Live.Find(TEXT("Boxed"));
	UDreamRingMenu* Wheel = Cast<UDreamRingMenu>(Live.Find(TEXT("Wheel")));
	if (!TestNotNull(TEXT("Trim"), Trim) || !TestNotNull(TEXT("Boxed"), Boxed) || !TestNotNull(TEXT("Wheel"), Wheel)
		|| !TestNotNull(TEXT("Trim's slot"), Trim->GetPanelSlot()) || !TestEqual(TEXT("two items"), Wheel->Items.Num(), 2))
	{
		return false;
	}
	// One gesture's worth: a tag dropped and one added, half of one Size and the whole of the other, an item's label.
	Trim->RemoveTag(TEXT("UI.Glow"));
	Trim->AddTag(TEXT("Sigil"));
	Trim->GetPanelSlot()->MinDesiredSize = FVector2D(100, 48);
	Boxed->Tags.Reset();
	Boxed->GetPanelSlot()->MinDesiredSize = FVector2D(80, 80);
	Boxed->GetPanelSlot()->MaxDesiredSize = FVector2D(80, 80);
	Wheel->Items[1].Label = FText::FromString(TEXT("Gate"));

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()),
		FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}
	TArray<FString> Expected = Lines;
	Expected[6] = TEXT("        @tag Trim, Sigil");
	Expected[7] = TEXT("        @slot MinDesiredSize = (100, 48)\n        @slot MaxDesiredSize = (120, 48)");
	Expected.RemoveAt(10); // Boxed's only tag line, taken out with its line.
	Expected[10] = TEXT("        @slot { Size = (80, 80)  Padding = (4, 0, 4, 0) }");
	Expected[13] = TEXT("        Items = [{ \"Lamp\", Tag: Lamp }, { \"Gate\", Tag: Door, bEnabled: false }]");
	TestEqualSensitive(TEXT("each edit on the line that said it, in the spelling it was written in"), Produced, Join(Expected));
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIShorthandPatcherTest,
	"DreamGUI.Text.Syntax.TheTagLinesAndASizeTakeEditsWhereTheyStandAndRefuseWhatTheyCannotSay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIShorthandPatcherTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsWriteBackTestLocal;
	const TArray<FString> Lines = {
		TEXT("Widget Root {"),                                   // 1
		TEXT("    + VerticalBox { }"),                           // 2
		TEXT("    Widget A {"),                                  // 3
		TEXT("        RenderOpacity = 0.5"),                     // 4 (a property line first: tags go after it)
		TEXT("        @tag One, Two, Three"),                    // 5
		TEXT("        @tag Four"),                               // 6
		TEXT("    }"),                                           // 7
		TEXT("    Widget Bare {"),                               // 8
		TEXT("        RenderOpacity = 0.5"),                     // 9
		TEXT("    }"),                                           // 10
		TEXT("    Widget Mixed {"),                              // 11
		TEXT("        Tags = [Old]"),                            // 12
		TEXT("        @tag New"),                                // 13
		TEXT("    }"),                                           // 14
		TEXT("    Widget Sized {"),                              // 15
		TEXT("        @slot Size = (10, 10)"),                   // 16
		TEXT("    }"),                                           // 17
		TEXT("}"),                                               // 18
	};
	const FString Source = Join(Lines);
	{
		// The middle name of a line, the last line whole, and one added: it joins the line that keeps names.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(*FString::Printf(TEXT("the edit is written (%s)"), *Diagnostics.ToString()),
			PatchAll(Text, { Edit(TEXT("A"), EDreamUIPatchTarget::Node, TEXT("Tags"), TEXT("[\"One\", \"Three\", \"Five\"]")) }, Diagnostics));
		TArray<FString> Expected = Lines;
		Expected[4] = TEXT("        @tag One, Three, Five");
		Expected.RemoveAt(5);
		TestEqualSensitive(TEXT("names off their lines, and onto the one that keeps any"), Text, Join(Expected));
	}
	{
		// A node with no tag line gets one, after its own lines.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(*FString::Printf(TEXT("the edit is written (%s)"), *Diagnostics.ToString()),
			PatchAll(Text, { Edit(TEXT("Bare"), EDreamUIPatchTarget::Node, TEXT("Tags"), TEXT("[Trim, \"UI.Glow\"]")) }, Diagnostics));
		TArray<FString> Expected = Lines;
		Expected.Insert(TEXT("        @tag Trim, UI.Glow"), 9);
		TestEqualSensitive(TEXT("a tag line of its own"), Text, Join(Expected));

		// A name no tag line can hold, on a node without one: the list spelling holds anything.
		FString Spaced = Source;
		FDreamUIDiagnosticBag SpacedDiagnostics;
		TestTrue(TEXT("a name with a space is written"), PatchAll(Spaced,
			{ Edit(TEXT("Bare"), EDreamUIPatchTarget::Node, TEXT("Tags"), TEXT("[\"two words\"]")) }, SpacedDiagnostics));
		TestTrue(TEXT("as a Tags line"), Spaced.Contains(TEXT("        Tags = [\"two words\"]")));
	}
	{
		// Refusals leave the file alone and say DUI7004: a name a tag line cannot hold where tag lines are the spelling,
		// and a node that writes its tags both ways.
		for (const FDreamUIPropertyEdit& Refused : {
			Edit(TEXT("A"), EDreamUIPatchTarget::Node, TEXT("Tags"), TEXT("[\"One\", \"two words\"]")),
			Edit(TEXT("Mixed"), EDreamUIPatchTarget::Node, TEXT("Tags"), TEXT("[\"Old\"]")) })
		{
			FString Text = Source;
			FDreamUIDiagnosticBag Diagnostics;
			TestFalse(*FString::Printf(TEXT("%s's Tags are refused"), *Refused.NodeId), PatchAll(Text, { Refused }, Diagnostics));
			TestEqual(*FString::Printf(TEXT("%s's refusal leaves the file as it was"), *Refused.NodeId), Text, Source);
			TestTrue(*FString::Printf(TEXT("%s's is DUI7004 (%s)"), *Refused.NodeId, *Diagnostics.ToString()),
				Reported(Diagnostics, EDreamUIDiagnosticCode::PatchSyntaxNotWritable));
		}
	}
	{
		// Both halves of a Size in one write: still one value, so still the shorthand; two values, the two lines.
		FString Same = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("both halves alike are written"), PatchAll(Same, {
			Edit(TEXT("Sized"), EDreamUIPatchTarget::Slot, TEXT("MinDesiredSize"), TEXT("(20, 20)")),
			Edit(TEXT("Sized"), EDreamUIPatchTarget::Slot, TEXT("MaxDesiredSize"), TEXT("(20, 20)")) }, Diagnostics));
		TArray<FString> Expected = Lines;
		Expected[15] = TEXT("        @slot Size = (20, 20)");
		TestEqualSensitive(TEXT("as the shorthand's new value"), Same, Join(Expected));

		FString Apart = Source;
		TestTrue(TEXT("both halves apart are written"), PatchAll(Apart, {
			Edit(TEXT("Sized"), EDreamUIPatchTarget::Slot, TEXT("MaxDesiredSize"), TEXT("(40, 30)")),
			Edit(TEXT("Sized"), EDreamUIPatchTarget::Slot, TEXT("MinDesiredSize"), TEXT("(20, 20)")) }, Diagnostics));
		Expected[15] = TEXT("        @slot MinDesiredSize = (20, 20)\n        @slot MaxDesiredSize = (40, 30)");
		TestEqualSensitive(TEXT("as the two lines it stood for"), Apart, Join(Expected));

		// Two new lines with one value, on a node that writes neither: the one line.
		FString Fresh = Source;
		TestTrue(TEXT("a new pair is written"), PatchAll(Fresh, {
			Edit(TEXT("Bare"), EDreamUIPatchTarget::Slot, TEXT("MinDesiredSize"), TEXT("(32, 32)")),
			Edit(TEXT("Bare"), EDreamUIPatchTarget::Slot, TEXT("MaxDesiredSize"), TEXT("(32, 32)")) }, Diagnostics));
		Expected = Lines;
		Expected.Insert(TEXT("        @slot Size = (32, 32)"), 9);
		TestEqualSensitive(TEXT("as '@slot Size'"), Fresh, Join(Expected));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIShorthandFromNothingTest,
	"DreamGUI.Text.Syntax.ATreeWrittenOutFromNothingUsesTagLinesASizeAndItemMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What the Blueprint converter does: a root's header, then the write-back's passes until the text stops changing, each
 * putting in what the tree has and the text does not. The tree here is one built from text, so what comes out can be
 * held to what went in.
 */
bool FDreamUIShorthandFromNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITagsAndShorthandsWriteBackTestLocal;
	const FString Source = Join({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    Widget Trim {"),
		TEXT("        @tag Trim, UI.Glow"),
		TEXT("        @slot Size = (120, 48)"),
		TEXT("    }"),
		TEXT("    Native.RingMenu Wheel {"),
		TEXT("        Items = [{ \"Lamp\", Tag: Lamp }, { Tag: Door, bEnabled: false }]"),
		TEXT("    }"),
		TEXT("}") });
	FBuiltTree Live = BuildLive(Source);
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Live.Diagnostics.ToString()), Live.Tree.IsValid()))
	{
		return false;
	}
	FString Text = TEXT("Widget Root {\n    + VerticalBox { }\n}\n");
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		if (!TestTrue(*FString::Printf(TEXT("pass %d computed an answer (%s)"), Pass, *Diagnostics.ToString()),
			FDreamUITextWriteBack::ProduceText(Text, Live.Tree.Get(), Updated, Diagnostics)))
		{
			return false;
		}
		if (Updated.Equals(Text, ESearchCase::CaseSensitive))
		{
			break;
		}
		Text = MoveTemp(Updated);
	}
	TestTrue(*FString::Printf(TEXT("the tags as a tag line:\n%s"), *Text), Text.Contains(TEXT("@tag Trim, UI.Glow")) && !Text.Contains(TEXT("Tags =")));
	TestTrue(TEXT("one size as '@slot Size'"), Text.Contains(TEXT("@slot Size = (120, 48)")) && !Text.Contains(TEXT("DesiredSize")));
	TestTrue(TEXT("the items as maps, the label first"),
		Text.Contains(TEXT("Items = [{ \"Lamp\", Tag: Lamp }, { Tag: Door, bEnabled: false }]")));
	return true;
}

#endif
