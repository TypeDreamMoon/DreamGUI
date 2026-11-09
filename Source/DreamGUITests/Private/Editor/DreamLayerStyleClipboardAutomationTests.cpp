// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"

#include "DreamGUIEditorModule.h"
#include "Text/DreamUITextWriteBack.h"
#include "Workflow/DreamUILayerStyleClipboard.h"
#include "Workflow/DreamUILayerStyleSnippet.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"

/*
 * Copy Layer Style and Paste Layer Style (DreamUILayerStyleClipboard): the copied look is lines of `.dui` that read back as
 * the look they were written from; a paste writes it onto every selected widget of a `.dui`-backed Blueprint -- into its own
 * `+ DreamLayerStyle` line, added where it had none, and onto its blend mode and mask when the copy carries them -- and the
 * whole paste is one undo step, the file included. The clipboard is the desktop's own, so each test puts back what it found.
 */
namespace DreamLayerStyleClipboardTestLocal
{
	using namespace DreamUITextStructureFixtures;

	struct FClipboardKeeper
	{
		FString Found;
		FClipboardKeeper() { FPlatformApplicationMisc::ClipboardPaste(Found); }
		~FClipboardKeeper() { FPlatformApplicationMisc::ClipboardCopy(*Found); }
	};

	const TArray<FString>& SourceLines()
	{
		static const TArray<FString> Lines = {
			TEXT("Widget Root {"),
			TEXT("    + CanvasPanel"),
			TEXT("    RectBlock Source {"),
			TEXT("        BlendMode = Multiply"),
			TEXT("        RenderOpacity = 0.75"),
			TEXT("        Clipping = ClipToBounds"),
			TEXT("        ClipMask.Shape = Ellipse"),
			TEXT("        + DreamLayerStyle {"),
			TEXT("            Style.DropShadows = [\"(bUseGlobalLight=False,Angle=120,Distance=6,Size=10,Opacity=0.6)\"]"),
			TEXT("            Style.BevelEmboss.bEnabled = true"),
			TEXT("            Style.BevelEmboss.Size = 6"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("    RectBlock Plain {"),
			TEXT("    }"),
			TEXT("    RectBlock Styled {"),
			TEXT("        + DreamLayerStyle { Style.OuterGlow.bEnabled = true }"),
			TEXT("    }"),
			TEXT("}")
		};
		return Lines;
	}

	const UDreamLayerStyle* FindStyle(const UDreamWidget* InWidget)
	{
		if (InWidget != nullptr)
		{
			for (const UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (const UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Behaviour))
				{
					return Style;
				}
			}
		}
		return nullptr;
	}

	int32 CountStyles(const UDreamWidget* InWidget)
	{
		int32 Count = 0;
		if (InWidget != nullptr)
		{
			for (const UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				Count += Behaviour != nullptr && Behaviour->IsA<UDreamLayerStyle>() ? 1 : 0;
			}
		}
		return Count;
	}

	/** The fields of two styles that differ, each as `Name: A | B`; empty when they are the same. */
	FString StyleDifferences(const FDreamLayerEffects& InA, const FDreamLayerEffects& InB)
	{
		TArray<FString> Out;
		for (TFieldIterator<FProperty> It(FDreamLayerEffects::StaticStruct()); It; ++It)
		{
			if (!It->Identical_InContainer(&InA, &InB, 0, PPF_None))
			{
				FString A;
				FString B;
				It->ExportTextItem_InContainer(A, &InA, nullptr, nullptr, PPF_None);
				It->ExportTextItem_InContainer(B, &InB, nullptr, nullptr, PPF_None);
				Out.Add(FString::Printf(TEXT("%s: %s | %s"), *It->GetName(), *A.Left(400), *B.Left(400)));
			}
		}
		return FString::Join(Out, TEXT("; "));
	}

	EDreamUIBlendMode BlendModeOf(const UDreamWidget* InWidget)
	{
		const UDreamVisualBatchMesh* Visual = InWidget != nullptr ? Cast<UDreamVisualBatchMesh>(InWidget->GetVisual()) : nullptr;
		return Visual != nullptr ? Visual->GetBlendMode() : EDreamUIBlendMode::Alpha;
	}

	/** The `Name = value` lines of a node, and of its `+` blocks of InComponentClass, as written. */
	TArray<FString> NodeLines(const FString& InText, const TCHAR* InNodeId)
	{
		TArray<FString> Lines;
		FDreamUIAst Ast;
		if (!ParseText(InText, Ast))
		{
			Lines.Add(TEXT("<does not parse>"));
			return Lines;
		}
		if (const FDreamUINode* Node = FindNode(Ast, InNodeId))
		{
			for (const FDreamUIProperty& Property : Node->Properties)
			{
				Lines.Add(FString::Printf(TEXT("%s = %s"), *Property.Name, *Property.Value.Raw));
			}
			for (const FDreamUIComponent& Component : Node->Components)
			{
				Lines.Add(FString::Printf(TEXT("+ %s"), *Component.ClassName));
				for (const FDreamUIProperty& Property : Component.Properties)
				{
					Lines.Add(FString::Printf(TEXT("  %s = %s"), *Property.Name, *Property.Value.Raw));
				}
			}
		}
		return Lines;
	}

	bool HasLine(const TArray<FString>& InLines, const FString& InPrefix)
	{
		return InLines.ContainsByPredicate([&InPrefix](const FString& InLine) { return InLine.StartsWith(InPrefix); });
	}

	void CollectTexts(TSharedRef<SWidget> InRoot, TArray<FString>& OutTexts)
	{
		if (InRoot->GetType() == TEXT("STextBlock"))
		{
			OutTexts.Add(StaticCastSharedRef<STextBlock>(InRoot)->GetText().ToString());
		}
		FChildren* Children = InRoot->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			CollectTexts(Children->GetChildAt(Index), OutTexts);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleSnippetRoundTripTest,
	"DreamGUI.Designer.LayerStyleClipboard.ACopiedLookReadsBackAsTheLookItWasCopiedFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A styled rect block's look -- its layer style, its blend mode and opacity, its clip and mask -- written as lines and read
 * back: the same style, field for field, the same blending and the same mask. Copying only some parts writes only those;
 * a widget without a layer style has no style part to copy; lines that are no look at all are not read as one.
 */
bool FDreamLayerStyleSnippetRoundTripTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleClipboardTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	TStrongObjectPtr<UDreamWidgetTree> Tree(FDreamUITextWriteBack::BuildReferenceTree(FString::Join(SourceLines(), TEXT("\n")), Ast, Diagnostics));
	if (!TestTrue(TEXT("the fixture builds"), Tree.IsValid())) return false;
	UDreamWidget* Source = nullptr;
	UDreamWidget* Plain = nullptr;
	Tree->ForEachWidget([&Source, &Plain](UDreamWidget* Widget)
	{
		Source = Widget->GetDisplayName() == TEXT("Source") ? Widget : Source;
		Plain = Widget->GetDisplayName() == TEXT("Plain") ? Widget : Plain;
	});
	const UDreamLayerStyle* SourceStyle = FindStyle(Source);
	if (!TestNotNull(TEXT("the source has its style"), SourceStyle)) return false;

	const FDreamUILayerStyleSnippet All = FDreamUILayerStyleSnippet::FromWidget(Source, EDreamUIStyleParts::All);
	TestEqual(TEXT("every part is copied"), All.GetParts(), EDreamUIStyleParts::All);
	TestTrue(TEXT("the copy starts with its comment"), All.GetText().StartsWith(FDreamUILayerStyleSnippet::HeaderLine));
	TestTrue(TEXT("it spells the blend mode as a file does"), All.GetText().Contains(TEXT("BlendMode = Multiply")));
	TestTrue(TEXT("and the mask"), All.GetText().Contains(TEXT("ClipMask.Shape = Ellipse")));
	TestTrue(TEXT("and the style as a + line"), All.GetText().Contains(TEXT("+ DreamLayerStyle {")));
	TestFalse(TEXT("and writes no value the class already has"), All.GetText().Contains(TEXT("Style.Scale")));

	FDreamUILayerStyleSnippet Read;
	FText Error;
	if (!TestTrue(TEXT("the lines read back"), FDreamUILayerStyleSnippet::Parse(All.GetText(), Read, &Error))) return false;
	TestEqual(TEXT("with the same parts"), Read.GetParts(), EDreamUIStyleParts::All);
	if (TestNotNull(TEXT("and a style"), Read.GetLayerStyle()))
	{
		TestEqual(TEXT("which is the source's, field for field"), StyleDifferences(Read.GetLayerStyle()->GetStyle(), SourceStyle->GetStyle()), FString());
	}
	EDreamUIBlendMode Mode = EDreamUIBlendMode::Alpha;
	float Opacity = 0.0f;
	TestTrue(TEXT("the blending reads back"), Read.GetBlending(Mode, Opacity));
	TestEqual(TEXT("its blend mode"), Mode, EDreamUIBlendMode::Multiply);
	TestEqual(TEXT("its opacity"), Opacity, 0.75f);
	if (TestNotNull(TEXT("the mask reads back on a widget"), Read.GetWidget()))
	{
		TestEqual(TEXT("its clip"), Read.GetWidget()->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);
		TestEqual(TEXT("its mask's shape"), Read.GetWidget()->GetClipMask().Shape, EDreamClipMaskShape::Ellipse);
	}

	const FDreamUILayerStyleSnippet StyleOnly = FDreamUILayerStyleSnippet::FromWidget(Source, EDreamUIStyleParts::Style);
	TestEqual(TEXT("copying the style alone copies the style alone"), StyleOnly.GetParts(), EDreamUIStyleParts::Style);
	TestFalse(TEXT("with no blend mode in it"), StyleOnly.GetText().Contains(TEXT("BlendMode")));

	TestTrue(TEXT("a widget without a style has no style to copy"), FDreamUILayerStyleSnippet::FromWidget(Plain, EDreamUIStyleParts::Style).IsEmpty());
	TestEqual(TEXT("but its blending is still a copy"), FDreamUILayerStyleSnippet::FromWidget(Plain, EDreamUIStyleParts::Blending).GetParts(),
		EDreamUIStyleParts::Blending);

	FDreamUILayerStyleSnippet Rejected;
	TestFalse(TEXT("a line of text is no layer style"), FDreamUILayerStyleSnippet::Parse(TEXT("Hello there"), Rejected));
	TestFalse(TEXT("nor are values that are none of its parts"), FDreamUILayerStyleSnippet::Parse(TEXT("Color = #FF0000"), Rejected));
	TestFalse(TEXT("nor a widget"), FDreamUILayerStyleSnippet::Parse(TEXT("BlendMode = Multiply\nText Child { }"), Rejected));

	// A style written from its effects alone, as a preset is applied: the same effects read back, and the preset named.
	UDreamLayerStylePreset* Preset = NewObject<UDreamLayerStylePreset>(GetTransientPackage());
	const FDreamUILayerStyleSnippet FromEffects = FDreamUILayerStyleSnippet::FromStyle(SourceStyle->GetStyle(), nullptr, EDreamUIBlendMode::Screen, 0.5f);
	TestEqual(TEXT("a style with its blending is both parts"), FromEffects.GetParts(), EDreamUIStyleParts::Style | EDreamUIStyleParts::Blending);
	if (TestNotNull(TEXT("and has a style"), FromEffects.GetLayerStyle()))
	{
		TestEqual(TEXT("with the effects it was given"), StyleDifferences(FromEffects.GetLayerStyle()->GetStyle(), SourceStyle->GetStyle()), FString());
	}
	TestTrue(TEXT("its blending is the one given"), FromEffects.GetBlending(Mode, Opacity) && Mode == EDreamUIBlendMode::Screen && Opacity == 0.5f);
	Preset->MarkAsGarbage();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStylePasteTest,
	"DreamGUI.Designer.LayerStyleClipboard.APasteWritesEverySelectedWidgetIntoTheDuiInOneUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The source's look copied with every part and pasted onto two widgets at once: one with no style, which gets a `+` line of
 * its own, and one with a style of its own, whose line is rewritten rather than joined by a second. Both compile to the
 * source's style, blend mode and mask. One undo puts the file back exactly as it was; a redo writes the paste again.
 */
bool FDreamLayerStylePasteTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleClipboardTestLocal;

	FClipboardKeeper Keeper;
	FScopedDuiFile File(TEXT("LayerStylePaste.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(SourceLines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_LayerStylePaste"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	if (!TestEqual(TEXT("and the .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	const FString Before = File.Read();

	if (!TestTrue(TEXT("the source's look is copied"),
		DreamUILayerStyleClipboard::Copy(Scoped.FindTemplate(TEXT("Source")), EDreamUIStyleParts::All))) return false;
	FDreamUILayerStyleSnippet OnClipboard;
	TestTrue(TEXT("and the clipboard reads as a layer style"), DreamUILayerStyleClipboard::ReadClipboard(OnClipboard));

	const TArray<UDreamWidget*> Targets = { Scoped.FindPreview(TEXT("Plain")), Scoped.FindPreview(TEXT("Styled")) };
	if (!TestTrue(TEXT("both targets have previews"), Targets[0] != nullptr && Targets[1] != nullptr)) return false;
	TestEqual(TEXT("the paste lands on both"), DreamUILayerStyleClipboard::Paste(*Scoped.Designer, Targets), 2);

	const FString After = File.Read();
	const TArray<FString> PlainLines = NodeLines(After, TEXT("Plain"));
	TestTrue(TEXT("the plain widget has a style line now"), HasLine(PlainLines, TEXT("+ DreamLayerStyle")));
	TestTrue(TEXT("with the source's bevel"), HasLine(PlainLines, TEXT("  Style.BevelEmboss.Size = 6")));
	TestTrue(TEXT("and the source's blend mode"), HasLine(PlainLines, TEXT("BlendMode = Multiply")));
	TestTrue(TEXT("and its mask"), HasLine(PlainLines, TEXT("ClipMask.Shape = Ellipse")));
	const TArray<FString> StyledLines = NodeLines(After, TEXT("Styled"));
	TestEqual(TEXT("the styled widget keeps one style line"),
		StyledLines.FilterByPredicate([](const FString& InLine) { return InLine.StartsWith(TEXT("+ DreamLayerStyle")); }).Num(), 1);

	const UDreamLayerStyle* SourceStyle = FindStyle(Scoped.FindTemplate(TEXT("Source")));
	for (const TCHAR* Name : { TEXT("Plain"), TEXT("Styled") })
	{
		const UDreamWidget* Template = Scoped.FindTemplate(Name);
		const UDreamLayerStyle* Style = FindStyle(Template);
		if (TestNotNull(*FString::Printf(TEXT("%s compiled with a style"), Name), Style) && SourceStyle != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s's style is the source's"), Name), StyleDifferences(Style->GetStyle(), SourceStyle->GetStyle()), FString());
		}
		TestEqual(*FString::Printf(TEXT("%s has one style"), Name), CountStyles(Template), 1);
		TestEqual(*FString::Printf(TEXT("%s blends as the source"), Name), BlendModeOf(Template), EDreamUIBlendMode::Multiply);
		TestEqual(*FString::Printf(TEXT("%s is as opaque as the source"), Name), Template != nullptr ? Template->GetRenderOpacity() : 0.0f, 0.75f);
		TestEqual(*FString::Printf(TEXT("%s clips as the source"), Name),
			Template != nullptr ? Template->GetClipMask().Shape : EDreamClipMaskShape::Rect, EDreamClipMaskShape::Ellipse);
	}
	TestTrue(TEXT("the source's own lines are untouched"), NodeLines(After, TEXT("Source")) == NodeLines(Before, TEXT("Source")));

	if (!TestTrue(TEXT("there is one thing to undo"), GEditor->UndoTransaction())) return false;
	TestEqual(TEXT("and undoing it puts the whole file back"), File.Read(), Before);
	TestNull(TEXT("the plain widget has no style again"), FindStyle(Scoped.FindTemplate(TEXT("Plain"))));
	if (!TestTrue(TEXT("there is something to redo"), GEditor->RedoTransaction())) return false;
	TestEqual(TEXT("and the redo writes the paste again"), File.Read(), After);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStylePastePartsTest,
	"DreamGUI.Designer.LayerStyleClipboard.APasteWritesOnlyThePartsTheCopyCarries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The style alone copied and pasted: the target gets the style, and keeps its blend mode, opacity and clip -- nothing the
 * copy did not carry is written, in the file or on the tree.
 */
bool FDreamLayerStylePastePartsTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleClipboardTestLocal;

	FClipboardKeeper Keeper;
	FScopedDuiFile File(TEXT("LayerStylePasteParts.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(SourceLines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_LayerStylePasteParts"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	if (!TestTrue(TEXT("the style alone is copied"),
		DreamUILayerStyleClipboard::Copy(Scoped.FindTemplate(TEXT("Source")), EDreamUIStyleParts::Style))) return false;
	TestEqual(TEXT("and pasted"), DreamUILayerStyleClipboard::Paste(*Scoped.Designer, { Scoped.FindPreview(TEXT("Plain")) }), 1);

	const TArray<FString> PlainLines = NodeLines(File.Read(), TEXT("Plain"));
	TestTrue(TEXT("the target has the style"), HasLine(PlainLines, TEXT("+ DreamLayerStyle")));
	TestFalse(TEXT("and no blend mode line"), HasLine(PlainLines, TEXT("BlendMode")));
	TestFalse(TEXT("nor an opacity line"), HasLine(PlainLines, TEXT("RenderOpacity")));
	TestFalse(TEXT("nor a clip"), HasLine(PlainLines, TEXT("Clip")));
	const UDreamWidget* Plain = Scoped.FindTemplate(TEXT("Plain"));
	TestEqual(TEXT("it blends as it did"), BlendModeOf(Plain), EDreamUIBlendMode::Alpha);
	TestEqual(TEXT("and clips as it did"), Plain != nullptr ? Plain->GetAuthoredClipping() : EDreamWidgetClipping::Disabled, EDreamWidgetClipping::Inherit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleMenuTest,
	"DreamGUI.Designer.LayerStyleClipboard.TheWidgetMenuOffersCopyAndPasteLayerStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The designer's widget menu -- the hierarchy's, which the viewport opens too -- has a Layer Style section with Copy, Paste
 * and the Copy Includes choices, through the editor module's extension point, for a selection.
 */
bool FDreamLayerStyleMenuTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleClipboardTestLocal;

	FClipboardKeeper Keeper;
	FScopedDuiFile File(TEXT("LayerStyleMenu.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(SourceLines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_LayerStyleMenu"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	UDreamWidget* Source = Scoped.FindPreview(TEXT("Source"));
	if (!TestNotNull(TEXT("the source has a preview"), Source)) return false;
	Scoped.Designer->SelectWidgets({ Source }, /*bAppendOrToggle*/false);
	DreamUILayerStyleClipboard::Copy(Scoped.FindTemplate(TEXT("Source")), EDreamUIStyleParts::All);

	FMenuBuilder Builder(/*bShouldCloseWindowAfterMenuSelection*/true, nullptr);
	const TSharedRef<FDreamWidgetBlueprintEditor> Designer = StaticCastSharedRef<FDreamWidgetBlueprintEditor>(Scoped.Designer->AsShared());
	FModuleManager::GetModuleChecked<FDreamGUIEditorModule>(TEXT("DreamGUIEditor")).ExtendWidgetContextMenu(Builder, Designer);
	TArray<FString> Texts;
	CollectTexts(Builder.MakeWidget(), Texts);
	TestTrue(TEXT("the menu has the Layer Style section"), Texts.Contains(TEXT("Layer Style")));
	TestTrue(TEXT("with Copy"), Texts.Contains(TEXT("Copy Layer Style")));
	TestTrue(TEXT("and Paste, naming what the copy carries"), Texts.Contains(TEXT("Paste Layer Style (style, blend mode, mask)")));
	TestTrue(TEXT("and what a copy includes"), Texts.Contains(TEXT("Copy Includes")));
	return true;
}

#endif
