// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"

#include "Designer/DreamWidgetDesignerTabs.h"
#include "Workflow/DreamUIPresetLibrary.h"
#include "Workflow/DreamUIStylePresets.h"
#include "Workflow/SDreamUIPresetLibrary.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamLayerStylePreset.h"
#include "Extensions/Effects/DreamGlass.h"
#include "MeshModifier/DreamMeshModifierWarp.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"

/*
 * The preset library (DreamUIPresetLibrary, SDreamUIPresetLibrary): its shelves read the project settings' text presets
 * and the preset assets; a gradient goes onto a text's face paint, a style onto a widget's layer style, effect settings onto
 * the effect -- each written into the `.dui` in one undo step; an asset is followed or copied; presets are kept the other
 * way round; the panel is a tab of the designer and applies to the selection; a dragged preset goes where it is dropped.
 *
 * The presets are project settings: each test puts back what it found, the settings file on disk included.
 */
namespace DreamPresetLibraryTestLocal
{
	using namespace DreamUITextStructureFixtures;

	struct FPresetsKeeper
	{
		TMap<FName, FString> Gradients;
		TMap<FName, FString> Styles;
		TMap<FName, FString> Effects;
		FString File;
		FString Text;
		bool bHadFile = false;

		FPresetsKeeper()
		{
			const UDreamGUISettings* Settings = GetDefault<UDreamGUISettings>();
			Gradients = Settings->GradientPresets;
			Styles = Settings->StylePresets;
			Effects = Settings->EffectPresets;
			File = Settings->GetDefaultConfigFilename();
			bHadFile = FFileHelper::LoadFileToString(Text, *File);
		}
		~FPresetsKeeper()
		{
			UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
			Settings->GradientPresets = Gradients;
			Settings->StylePresets = Styles;
			Settings->EffectPresets = Effects;
			if (bHadFile)
			{
				FFileHelper::SaveStringToFile(Text, *File);
			}
			else
			{
				// The test made the file by saving a setting: it goes again.
				IFileManager::Get().Delete(*File, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
			}
			DreamGradientPresets::OnPresetsChanged().Broadcast();
		}
	};

	const TArray<FString>& Lines()
	{
		static const TArray<FString> Text = {
			TEXT("Widget Root {"),
			TEXT("    + CanvasPanel"),
			TEXT("    Text Title { Text = \"Hello\" }"),
			TEXT("    RectBlock Card { }"),
			TEXT("    RectBlock Other { }"),
			TEXT("    Glass Pane { Frost = 0.5 }"),
			TEXT("}")
		};
		return Text;
	}

	FDreamUIPreset Text(EDreamUIPresetKind InKind, const TCHAR* InName, const FString& InText)
	{
		FDreamUIPreset Preset;
		Preset.Kind = InKind;
		Preset.Source = EDreamUIPresetSource::ProjectSettings;
		Preset.Name = InName;
		Preset.Text = InText;
		return Preset;
	}

	const FDreamUIPreset* FindPreset(const TArray<FDreamUIPreset>& InPresets, FName InName)
	{
		return InPresets.FindByPredicate([InName](const FDreamUIPreset& InPreset) { return InPreset.Name == InName; });
	}

	/** The `Name = value` lines of a node and of its `+` blocks, as written. */
	TArray<FString> NodeLines(const FString& InText, const TCHAR* InNodeId)
	{
		TArray<FString> Out;
		FDreamUIAst Ast;
		if (!ParseText(InText, Ast))
		{
			Out.Add(TEXT("<does not parse>"));
			return Out;
		}
		if (const FDreamUINode* Node = FindNode(Ast, InNodeId))
		{
			for (const FDreamUIProperty& Property : Node->Properties)
			{
				Out.Add(FString::Printf(TEXT("%s = %s"), *Property.Name, *Property.Value.Raw));
			}
			for (const FDreamUIComponent& Component : Node->Components)
			{
				Out.Add(FString::Printf(TEXT("+ %s"), *Component.ClassName));
				for (const FDreamUIProperty& Property : Component.Properties)
				{
					Out.Add(FString::Printf(TEXT("  %s = %s"), *Property.Name, *Property.Value.Raw));
				}
			}
		}
		return Out;
	}

	bool HasLine(const TArray<FString>& InLines, const FString& InPrefix)
	{
		return InLines.ContainsByPredicate([&InPrefix](const FString& InLine) { return InLine.StartsWith(InPrefix); });
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresetLibraryShelvesTest,
	"DreamGUI.Designer.PresetLibrary.TheShelvesHoldTheProjectsTextPresetsAndTheyAreKeptAndRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A gradient, a style and an effect kept as text in the project settings are each on their own shelf, read for what they
 * are; a text that does not read as its kind is not kept; a kept preset is written to the settings file; one removed is gone.
 */
bool FDreamPresetLibraryShelvesTest::RunTest(const FString&)
{
	using namespace DreamPresetLibraryTestLocal;
	FPresetsKeeper Keeper;

	FText Error;
	TestTrue(TEXT("a gradient is kept"), DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind::Gradient, TEXT("TestSunset"),
		TEXT("linear-gradient(90deg, #FF8A5B, #2B5FD9)"), &Error));
	TestTrue(TEXT("a style is kept"), DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind::Style, TEXT("TestGlow"),
		TEXT("+ DreamLayerStyle {\n    Style.OuterGlow.bEnabled = true\n    Style.OuterGlow.Size = 14\n}"), &Error));
	TestTrue(TEXT("effect settings are kept"), DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind::Effect, TEXT("TestFrosted"),
		TEXT("Glass { Refraction = 18  Frost = 0.25 }"), &Error));
	TestFalse(TEXT("a gradient that is no gradient is not"), DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind::Gradient, TEXT("TestBad"),
		TEXT("not a gradient"), &Error));
	TestFalse(TEXT("nor a style that is no style"), DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind::Style, TEXT("TestBad"),
		TEXT("Color = #FF0000"), &Error));
	TestFalse(TEXT("nor an effect of no effect"), DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind::Effect, TEXT("TestBad"),
		TEXT("NoSuchEffect { }"), &Error));
	FString OnDisk;
	FFileHelper::LoadFileToString(OnDisk, *GetDefault<UDreamGUISettings>()->GetDefaultConfigFilename());
	TestTrue(TEXT("the kept presets are in the settings file"), OnDisk.Contains(TEXT("TestGlow")) && OnDisk.Contains(TEXT("TestFrosted")));

	TArray<FDreamUIPreset> Shelf;
	DreamUIPresetLibrary::Collect(EDreamUIPresetKind::Gradient, Shelf);
	const FDreamUIPreset* Sunset = FindPreset(Shelf, TEXT("TestSunset"));
	FDreamGradient Gradient;
	TestTrue(TEXT("the gradient is on the gradient shelf, and reads"), Sunset != nullptr && Sunset->GetGradient(Gradient) && Gradient.Stops.Num() == 2);
	TestNull(TEXT("the rejected one is nowhere"), FindPreset(Shelf, TEXT("TestBad")));
	DreamUIPresetLibrary::Collect(EDreamUIPresetKind::Style, Shelf);
	const FDreamUIPreset* Glow = FindPreset(Shelf, TEXT("TestGlow"));
	FDreamUILayerStyleSnippet Snippet;
	TestTrue(TEXT("the style is on the style shelf, and reads"), Glow != nullptr && Glow->GetStyle(Snippet) && Snippet.GetLayerStyle() != nullptr
		&& Snippet.GetLayerStyle()->GetStyle().OuterGlow.bEnabled);
	DreamUIPresetLibrary::Collect(EDreamUIPresetKind::Effect, Shelf);
	const FDreamUIPreset* Frosted = FindPreset(Shelf, TEXT("TestFrosted"));
	UClass* EffectClass = nullptr;
	bool bIsComponent = true;
	TestTrue(TEXT("the effect is on the effect shelf, for glass"), Frosted != nullptr && Frosted->GetEffectClass(EffectClass, bIsComponent)
		&& EffectClass == UDreamGlass::StaticClass() && !bIsComponent);

	TestTrue(TEXT("a text preset is removed"), DreamUIPresetLibrary::RemoveTextPreset(EDreamUIPresetKind::Style, TEXT("TestGlow")));
	DreamUIPresetLibrary::Collect(EDreamUIPresetKind::Style, Shelf);
	TestNull(TEXT("and off its shelf"), FindPreset(Shelf, TEXT("TestGlow")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresetLibraryApplyTest,
	"DreamGUI.Designer.PresetLibrary.APresetIsWrittenOntoTheWidgetsItIsAppliedToInOneUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * On a `.dui`-backed Blueprint: a gradient onto a text switches its face paint on with that gradient; a style onto two rect
 * blocks gives each its layer style; glass settings onto the glass set what they name and leave its frost; a warp's settings
 * bring the warp. Each is one undo that puts the file back. What a preset cannot go onto is refused.
 */
bool FDreamPresetLibraryApplyTest::RunTest(const FString&)
{
	using namespace DreamPresetLibraryTestLocal;

	FScopedDuiFile File(TEXT("PresetLibraryApply.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_PresetLibraryApply"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	if (!TestEqual(TEXT("and the .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	FDreamWidgetBlueprintEditor& Designer = *Scoped.Designer;

	// A gradient onto a text.
	const FString Before = File.Read();
	const FDreamUIPreset Sunset = Text(EDreamUIPresetKind::Gradient, TEXT("Sunset"), TEXT("linear-gradient(90deg, #FF8A5B, #2B5FD9)"));
	TestTrue(TEXT("a text takes a gradient"), DreamUIPresetLibrary::CanApply(Scoped.FindPreview(TEXT("Title")), Sunset));
	TestEqual(TEXT("and takes it"), DreamUIPresetLibrary::Apply(Designer, { Scoped.FindPreview(TEXT("Title")) }, Sunset), 1);
	const TArray<FString> TitleLines = NodeLines(File.Read(), TEXT("Title"));
	TestTrue(TEXT("its face paint is switched on in the file"), HasLine(TitleLines, TEXT("TextStyle.FacePaint.bEnabled = true")));
	TestTrue(TEXT("with the gradient"), HasLine(TitleLines, TEXT("TextStyle.FacePaint.Gradient")));
	if (const UDreamText* Title = Scoped.FindTemplate(TEXT("Title")) ? Cast<UDreamText>(Scoped.FindTemplate(TEXT("Title"))->GetVisual()) : nullptr)
	{
		TestEqual(TEXT("and the compiled text paints two stops"), Title->GetTextStyle().FacePaint.Gradient.Stops.Num(), 2);
	}
	TestTrue(TEXT("one undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("puts the file back"), File.Read(), Before);

	// A style onto two widgets at once.
	const FDreamUIPreset Glow = Text(EDreamUIPresetKind::Style, TEXT("Glow"),
		TEXT("+ DreamLayerStyle {\n    Style.OuterGlow.bEnabled = true\n    Style.OuterGlow.Size = 14\n}"));
	TestEqual(TEXT("a style goes onto both"), DreamUIPresetLibrary::Apply(Designer,
		{ Scoped.FindPreview(TEXT("Card")), Scoped.FindPreview(TEXT("Other")) }, Glow), 2);
	for (const TCHAR* Name : { TEXT("Card"), TEXT("Other") })
	{
		const UDreamLayerStyle* Style = FindStyle(Scoped.FindTemplate(Name));
		TestTrue(*FString::Printf(TEXT("%s glows"), Name), Style != nullptr && Style->GetStyle().OuterGlow.bEnabled && Style->GetStyle().OuterGlow.Size == 14.0f);
		TestTrue(*FString::Printf(TEXT("%s's line says so"), Name), HasLine(NodeLines(File.Read(), Name), TEXT("  Style.OuterGlow.Size = 14")));
	}
	TestTrue(TEXT("one undo for both"), GEditor->UndoTransaction());
	TestEqual(TEXT("puts the file back"), File.Read(), Before);

	// Effect settings onto their effect, and nowhere else.
	const FDreamUIPreset Frosted = Text(EDreamUIPresetKind::Effect, TEXT("Frosted"), TEXT("Glass { Refraction = 18 }"));
	TestFalse(TEXT("glass settings do not go onto a rect block"), DreamUIPresetLibrary::CanApply(Scoped.FindPreview(TEXT("Card")), Frosted));
	FText Error;
	TestEqual(TEXT("and are refused there"), DreamUIPresetLibrary::Apply(Designer, { Scoped.FindPreview(TEXT("Card")) }, Frosted, true, &Error), 0);
	TestFalse(TEXT("with a reason"), Error.IsEmpty());
	TestEqual(TEXT("they go onto the glass"), DreamUIPresetLibrary::Apply(Designer, { Scoped.FindPreview(TEXT("Pane")) }, Frosted), 1);
	const TArray<FString> PaneLines = NodeLines(File.Read(), TEXT("Pane"));
	TestTrue(TEXT("setting what they name"), HasLine(PaneLines, TEXT("Refraction = 18")));
	TestTrue(TEXT("and leaving the frost it had"), HasLine(PaneLines, TEXT("Frost = 0.5")));
	TestTrue(TEXT("one undo"), GEditor->UndoTransaction());

	// A behaviour's settings bring the behaviour.
	const FDreamUIPreset Arc = Text(EDreamUIPresetKind::Effect, TEXT("Arc"), TEXT("+ DreamMeshModifierWarp { Style = Arc  Bend = 60 }"));
	TestTrue(TEXT("a warp's settings go onto a text"), DreamUIPresetLibrary::CanApply(Scoped.FindPreview(TEXT("Title")), Arc));
	TestEqual(TEXT("and are applied"), DreamUIPresetLibrary::Apply(Designer, { Scoped.FindPreview(TEXT("Title")) }, Arc), 1);
	const TArray<FString> WarpLines = NodeLines(File.Read(), TEXT("Title"));
	TestTrue(TEXT("bringing the warp's line"), HasLine(WarpLines, TEXT("+ DreamMeshModifierWarp")));
	TestTrue(TEXT("with its bend"), HasLine(WarpLines, TEXT("  Bend = 60")));
	TestTrue(TEXT("one undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("puts the file back"), File.Read(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresetLibraryAssetsTest,
	"DreamGUI.Designer.PresetLibrary.AStylePresetAssetIsMadeFollowedOrCopiedAndKeptAsText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A style preset asset made the way the style dialog's New Style makes one, blending included: applied, the widget follows
 * it (its layer style names it as its preset) and takes its blending; applied as a copy, the widget holds the effects
 * itself. A text style kept as an asset holds the same effects; an asset kept as text reads back as the same style.
 */
bool FDreamPresetLibraryAssetsTest::RunTest(const FString&)
{
	using namespace DreamPresetLibraryTestLocal;
	FPresetsKeeper Keeper;

	FDreamUIStylePresetSaveArgs Args;
	Args.Style.OuterGlow.bEnabled = true;
	Args.Style.OuterGlow.Size = 9.0f;
	FDreamLayerStyleBlending Blending;
	Blending.BlendMode = EDreamUIBlendMode::Screen;
	Blending.Opacity = 0.5f;
	Args.Blending = Blending;
	Args.Name = TEXT("LS_TestGlow");
	Args.PackagePath = TEXT("/Temp/DreamGUITests/Presets");
	FText Error;
	UDreamLayerStylePreset* Asset = DreamUIStylePresets::CreatePresetAsset(Args, &Error);
	if (!TestNotNull(*FString::Printf(TEXT("a style preset asset is made (%s)"), *Error.ToString()), Asset)) return false;
	Asset->AddToRoot();
	ON_SCOPE_EXIT { Asset->RemoveFromRoot(); Asset->MarkAsGarbage(); };
	TestTrue(TEXT("with the style"), Asset->GetStyle().OuterGlow.bEnabled && Asset->GetStyle().OuterGlow.Size == 9.0f);
	TestTrue(TEXT("and the blending"), Asset->IncludesBlending() && Asset->GetBlending().BlendMode == EDreamUIBlendMode::Screen);

	FDreamUIPreset AssetPreset;
	AssetPreset.Kind = EDreamUIPresetKind::Style;
	AssetPreset.Source = EDreamUIPresetSource::Asset;
	AssetPreset.Name = TEXT("LS_TestGlow");
	AssetPreset.Asset = FSoftObjectPath(Asset);

	FScopedDuiFile File(TEXT("PresetLibraryAssets.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_PresetLibraryAssets"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	TestEqual(TEXT("the asset is applied"), DreamUIPresetLibrary::Apply(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")) }, AssetPreset), 1);
	const UDreamLayerStyle* Following = FindStyle(Scoped.FindTemplate(TEXT("Card")));
	TestTrue(TEXT("the widget follows it"), Following != nullptr && Following->GetPreset() == Asset);
	const UDreamVisualBatchMesh* CardVisual = Scoped.FindTemplate(TEXT("Card")) ? Cast<UDreamVisualBatchMesh>(Scoped.FindTemplate(TEXT("Card"))->GetVisual()) : nullptr;
	TestTrue(TEXT("and takes its blending"), CardVisual != nullptr && CardVisual->GetBlendMode() == EDreamUIBlendMode::Screen
		&& FMath::IsNearlyEqual(Scoped.FindTemplate(TEXT("Card"))->GetRenderOpacity(), 0.5f));

	TestEqual(TEXT("applied as a copy"), DreamUIPresetLibrary::Apply(*Scoped.Designer, { Scoped.FindPreview(TEXT("Other")) }, AssetPreset,
		/*bInFollowAsset*/false), 1);
	const UDreamLayerStyle* Copied = FindStyle(Scoped.FindTemplate(TEXT("Other")));
	TestTrue(TEXT("the widget holds the effects itself"), Copied != nullptr && Copied->GetPreset() == nullptr
		&& Copied->GetStyle().OuterGlow.bEnabled && Copied->GetStyle().OuterGlow.Size == 9.0f);

	TestTrue(TEXT("the asset is kept as text"), DreamUIPresetLibrary::SaveAsText(AssetPreset, TEXT("TestGlowText"), &Error));
	TArray<FDreamUIPreset> Shelf;
	DreamUIPresetLibrary::Collect(EDreamUIPresetKind::Style, Shelf);
	const FDreamUIPreset* AsText = FindPreset(Shelf, TEXT("TestGlowText"));
	FDreamUILayerStyleSnippet Snippet;
	TestTrue(TEXT("reading back as the same style"), AsText != nullptr && AsText->GetStyle(Snippet) && Snippet.GetLayerStyle() != nullptr
		&& Snippet.GetLayerStyle()->GetStyle().OuterGlow.Size == 9.0f);
	EDreamUIBlendMode Mode = EDreamUIBlendMode::Alpha;
	float Opacity = 1.0f;
	TestTrue(TEXT("its blending with it"), Snippet.GetBlending(Mode, Opacity) && Mode == EDreamUIBlendMode::Screen);

	if (AsText != nullptr)
	{
		UDreamLayerStylePreset* Back = Cast<UDreamLayerStylePreset>(DreamUIPresetLibrary::SaveAsAsset(*AsText, TEXT("/Temp/DreamGUITests/Presets"), &Error));
		TestTrue(TEXT("and the text kept as an asset again holds the effects"), Back != nullptr && Back->GetStyle().OuterGlow.Size == 9.0f
			&& Back->IncludesBlending());
		if (Back != nullptr)
		{
			Back->MarkAsGarbage();
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresetLibraryPanelTest,
	"DreamGUI.Designer.PresetLibrary.ThePanelIsATabOfTheDesignerAndAppliesToTheSelectionOrWhereADragIsDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The designer opens the Presets tab. The panel shows a shelf's presets, narrowed by its search, and applies one to the
 * designer's selection. A preset dragged and dropped onto a widget goes onto that widget, or onto the whole selection when
 * the widget is one of it; one that cannot go there is not taken.
 */
bool FDreamPresetLibraryPanelTest::RunTest(const FString&)
{
	using namespace DreamPresetLibraryTestLocal;
	FPresetsKeeper Keeper;
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	Settings->StylePresets.Add(TEXT("TestPanelGlow"), TEXT("+ DreamLayerStyle { Style.OuterGlow.bEnabled = true }"));
	Settings->StylePresets.Add(TEXT("TestPanelShadow"), TEXT("+ DreamLayerStyle { Style.DropShadows = [\"(Distance=4,Size=6)\"] }"));

	FScopedDuiFile File(TEXT("PresetLibraryPanel.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_PresetLibraryPanel"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const TSharedRef<FDreamWidgetBlueprintEditor> Designer = StaticCastSharedRef<FDreamWidgetBlueprintEditor>(Scoped.Designer->AsShared());

	const TSharedPtr<SDockTab> Tab = Designer->GetTabManager()->TryInvokeTab(FDreamWidgetDesignerTabs::PresetsID);
	TestTrue(TEXT("the designer opens the Presets tab"), Tab.IsValid());

	const TSharedRef<SDreamUIPresetLibrary> Panel = SNew(SDreamUIPresetLibrary, Designer);
	Panel->SetKind(EDreamUIPresetKind::Style);
	auto Shows = [&Panel](FName InName)
	{
		return Panel->GetVisiblePresets().ContainsByPredicate([InName](const TSharedPtr<FDreamUIPreset>& InPreset) { return InPreset->Name == InName; });
	};
	TestTrue(TEXT("the style shelf shows the project's styles"), Shows(TEXT("TestPanelGlow")) && Shows(TEXT("TestPanelShadow")));
	Panel->SetSearchText(FText::FromString(TEXT("Shadow")));
	TestTrue(TEXT("the search narrows it"), !Shows(TEXT("TestPanelGlow")) && Shows(TEXT("TestPanelShadow")));
	Panel->SetSearchText(FText::GetEmpty());

	TestEqual(TEXT("with nothing selected nothing takes a preset"), Panel->ApplyToSelection(Text(EDreamUIPresetKind::Style, TEXT("TestPanelGlow"),
		Settings->StylePresets[TEXT("TestPanelGlow")])), 0);
	Designer->SelectWidgets({ Scoped.FindPreview(TEXT("Card")) }, /*bAppendOrToggle*/false);
	TestEqual(TEXT("the selection takes it"), Panel->ApplyToSelection(Text(EDreamUIPresetKind::Style, TEXT("TestPanelGlow"),
		Settings->StylePresets[TEXT("TestPanelGlow")])), 1);
	TestTrue(TEXT("and has the style"), FindStyle(Scoped.FindTemplate(TEXT("Card"))) != nullptr);

	// Dragged and dropped onto a widget outside the selection: that widget alone.
	const TSharedRef<FDreamUIPresetDragDropOp> Drag = FDreamUIPresetDragDropOp::New(Text(EDreamUIPresetKind::Style, TEXT("TestPanelShadow"),
		Settings->StylePresets[TEXT("TestPanelShadow")]));
	TestEqual(TEXT("the drag carries a style"), Drag->GetKind(), EDreamUIPresetKind::Style);
	const FDragDropEvent DropEvent(FPointerEvent(), Drag);
	TestTrue(TEXT("a style can be dropped onto a rect block"), FDreamUIPresetDragDropOp::CanDropOnto(DropEvent, Scoped.FindPreview(TEXT("Other"))));
	TestTrue(TEXT("and is taken there"), FDreamUIPresetDragDropOp::DropOnto(DropEvent, *Designer, Scoped.FindPreview(TEXT("Other"))));
	const UDreamLayerStyle* OtherStyle = FindStyle(Scoped.FindTemplate(TEXT("Other")));
	TestTrue(TEXT("onto that widget"), OtherStyle != nullptr && OtherStyle->GetStyle().DropShadows.Num() == 1);
	const UDreamLayerStyle* CardStyle = FindStyle(Scoped.FindTemplate(TEXT("Card")));
	TestTrue(TEXT("and not onto the selection it is not part of"), CardStyle != nullptr && CardStyle->GetStyle().DropShadows.IsEmpty());

	const TSharedRef<FDreamUIPresetDragDropOp> GlassDrag = FDreamUIPresetDragDropOp::New(Text(EDreamUIPresetKind::Effect, TEXT("Frosted"),
		TEXT("Glass { Refraction = 18 }")));
	const FDragDropEvent GlassEvent(FPointerEvent(), GlassDrag);
	TestFalse(TEXT("glass settings cannot be dropped onto a rect block"), FDreamUIPresetDragDropOp::CanDropOnto(GlassEvent, Scoped.FindPreview(TEXT("Other"))));
	TestTrue(TEXT("but can onto the glass"), FDreamUIPresetDragDropOp::CanDropOnto(GlassEvent, Scoped.FindPreview(TEXT("Pane"))));
	if (Tab.IsValid())
	{
		Tab->RequestCloseTab();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresetLibraryRectBlockGradientTest,
	"DreamGUI.Designer.PresetLibrary.AGradientOntoARectBlockTurnsOnTheSwitchItIsDrawnBy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A rect block draws its body gradient only while bEnableBodyGradient is on (the gradient's EditCondition): a gradient
 * preset onto a block with the switch off sets the gradient and turns the switch on, both written into the `.dui` and
 * compiled, and one undo puts the file back.
 */
bool FDreamPresetLibraryRectBlockGradientTest::RunTest(const FString&)
{
	using namespace DreamPresetLibraryTestLocal;

	FScopedDuiFile File(TEXT("PresetLibraryRectBlockGradient.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_PresetLibraryRectBlockGradient"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const FBoolProperty* Switch = FindFProperty<FBoolProperty>(UDreamRectBlock::StaticClass(), TEXT("bEnableBodyGradient"));
	if (!TestNotNull(TEXT("a rect block has its body gradient's switch"), Switch)) return false;
	const UDreamRectBlock* Before = Scoped.FindTemplate(TEXT("Card")) ? Cast<UDreamRectBlock>(Scoped.FindTemplate(TEXT("Card"))->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("the card is a rect block"), Before)) return false;
	TestFalse(TEXT("whose switch is off"), Switch->GetPropertyValue_InContainer(Before));

	const FString Original = File.Read();
	const FDreamUIPreset Sunset = Text(EDreamUIPresetKind::Gradient, TEXT("Sunset"), TEXT("linear-gradient(90deg, #FF8A5B, #2B5FD9)"));
	TestTrue(TEXT("a rect block takes a gradient"), DreamUIPresetLibrary::CanApply(Scoped.FindPreview(TEXT("Card")), Sunset));
	TestEqual(TEXT("and takes it"), DreamUIPresetLibrary::Apply(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")) }, Sunset), 1);
	const TArray<FString> CardLines = NodeLines(File.Read(), TEXT("Card"));
	TestTrue(TEXT("its body gradient is written"), HasLine(CardLines, TEXT("BodyGradient = ")));
	TestTrue(TEXT("with its switch on"), HasLine(CardLines, TEXT("bEnableBodyGradient = true")));
	if (const UDreamRectBlock* Card = Scoped.FindTemplate(TEXT("Card")) ? Cast<UDreamRectBlock>(Scoped.FindTemplate(TEXT("Card"))->GetVisual()) : nullptr)
	{
		TestTrue(TEXT("and the compiled block draws it"), Switch->GetPropertyValue_InContainer(Card));
		TestEqual(TEXT("with its two stops"), Card->GetBodyGradient().Stops.Num(), 2);
	}
	TestTrue(TEXT("one undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("puts the file back"), File.Read(), Original);
	return true;
}

#endif
