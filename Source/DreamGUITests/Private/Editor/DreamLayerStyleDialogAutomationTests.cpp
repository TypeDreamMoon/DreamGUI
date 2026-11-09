// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"

#include "Designer/DreamWidgetPreviewHost.h"
#include "Workflow/DreamUILayerStyleDialog.h"
#include "Workflow/DreamUIStylePresets.h"
#include "Workflow/SDreamUILayerStyleDialog.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"

#include "Widgets/SWindow.h"

/*
 * The Layer Style dialog (DreamUILayerStyleDialog): it opens on the designer's widgets and lists the effects a layer style
 * has, ticked as the first widget's style has them; ticking, adding an instance, reordering and editing a parameter change
 * a working copy that is previewed on the widgets at once and nowhere else -- the authoring tree and the `.dui` are
 * untouched; Cancel puts the preview back and the file is byte for byte what it was; OK writes the style onto every widget
 * as one undo step; New Style keeps the working style as a preset.
 */
namespace DreamLayerStyleDialogTestLocal
{
	using namespace DreamUITextStructureFixtures;

	const TArray<FString>& Lines()
	{
		static const TArray<FString> Text = {
			TEXT("Widget Root {"),
			TEXT("    + CanvasPanel"),
			TEXT("    RectBlock Card {"),
			TEXT("        + DreamLayerStyle {"),
			TEXT("            Style.DropShadows = [\"(Distance=6,Size=10)\"]"),
			TEXT("            Style.OuterGlow.bEnabled = true"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("    RectBlock Plain { }"),
			TEXT("}")
		};
		return Text;
	}

	UDreamLayerStyle* FindStyle(UDreamWidget* InWidget)
	{
		if (InWidget != nullptr)
		{
			for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Behaviour))
				{
					return Style;
				}
			}
		}
		return nullptr;
	}

	bool HasRow(const TArray<FDreamUIStyleDialogRow>& InRows, EDreamUIStyleDialogPage InPage, int32 InIndex)
	{
		return InRows.Contains(FDreamUIStyleDialogRow{ InPage, InIndex });
	}

	/** The dialog Open made, found again; the window it is in closed when the test is done with it. */
	struct FOpenDialog
	{
		TSharedPtr<SWindow> Window;
		TSharedPtr<SDreamUILayerStyleDialog> Dialog;

		FOpenDialog(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InWidgets)
		{
			Window = DreamUILayerStyleDialog::Open(InDesigner, InWidgets);
			Dialog = DreamUILayerStyleDialog::Find(InDesigner);
		}
		~FOpenDialog()
		{
			if (Window.IsValid())
			{
				Window->RequestDestroyWindow();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleDialogOpensTest,
	"DreamGUI.Designer.StyleDialog.ItOpensOnTheWidgetsAndListsTheEffectsTheirStyleHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Opened on a widget with a drop shadow and an outer glow: a window, the dialog found for the designer, Blending Options
 * first and every kind of effect after it in an image editor's order, the shadow and the glow ticked and the rest not, a
 * kind there is none of listed once for its tick. Cancelled, it is gone. Opened from one widget, as the Layer section and
 * the hierarchy open it, it is on the whole selection when the widget is one of it and on the widget alone when not.
 */
bool FDreamLayerStyleDialogOpensTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleDialogTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogOpens.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogOpens"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	FOpenDialog Open(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")) });
	if (!TestTrue(TEXT("the dialog opens in a window of its own"), Open.Window.IsValid())) return false;
	if (!TestTrue(TEXT("and is found for its designer"), Open.Dialog.IsValid())) return false;
	const TSharedRef<FDreamUILayerStyleSession> Session = Open.Dialog->GetSession();
	const TArray<FDreamUIStyleDialogRow> Rows = Session->GetRows();
	TestTrue(TEXT("Blending Options comes first"), Rows.Num() > 0 && Rows[0].Page == EDreamUIStyleDialogPage::Blending);
	TestTrue(TEXT("the bevel's contour and texture follow the bevel"), Rows.Num() > 3 && Rows[1].Page == EDreamUIStyleDialogPage::BevelEmboss
		&& Rows[2].Page == EDreamUIStyleDialogPage::BevelContour && Rows[3].Page == EDreamUIStyleDialogPage::BevelTexture);
	TestTrue(TEXT("the drop shadow comes last, the one it has"), Rows.Num() > 0 && Rows.Last() == (FDreamUIStyleDialogRow{ EDreamUIStyleDialogPage::DropShadow, 0 }));
	TestTrue(TEXT("a kind there is none of is listed once"), HasRow(Rows, EDreamUIStyleDialogPage::Stroke, INDEX_NONE));
	TestEqual(TEXT("thirteen rows in all"), Rows.Num(), 13);
	TestTrue(TEXT("the drop shadow is ticked"), Session->IsChecked({ EDreamUIStyleDialogPage::DropShadow, 0 }));
	TestTrue(TEXT("and the outer glow"), Session->IsChecked({ EDreamUIStyleDialogPage::OuterGlow, INDEX_NONE }));
	TestFalse(TEXT("and not the satin"), Session->IsChecked({ EDreamUIStyleDialogPage::Satin, INDEX_NONE }));
	TestFalse(TEXT("Blending Options has no tick"), Session->HasCheckbox({ EDreamUIStyleDialogPage::Blending, INDEX_NONE }));
	TestTrue(TEXT("it shows Blending Options first"), Open.Dialog->GetShownStruct() == FDreamUIStyleDialogBlending::StaticStruct());

	Open.Dialog->Cancel();
	TestFalse(TEXT("cancelled, it is no longer open"), DreamUILayerStyleDialog::Find(*Scoped.Designer).IsValid());

	// From one widget, as the Layer section's Edit... and a hierarchy mark open it: the selection, when the widget is one of it.
	UDreamWidget* Card = Scoped.FindPreview(TEXT("Card"));
	UDreamWidget* Plain = Scoped.FindPreview(TEXT("Plain"));
	Scoped.Designer->SelectWidgets({ Card, Plain }, /*bAppendOrToggle*/false);
	TSharedPtr<SWindow> FromSelected = DreamUILayerStyleDialog::OpenFrom(Plain);
	TSharedPtr<SDreamUILayerStyleDialog> Dialog = DreamUILayerStyleDialog::Find(*Scoped.Designer);
	TestTrue(TEXT("opened from a selected widget, it is on the whole selection"),
		Dialog.IsValid() && [&Dialog]() { TArray<FString> Names = Dialog->GetSession()->GetTargetNames(); Names.Sort(); return Names == TArray<FString>({ TEXT("Card"), TEXT("Plain") }); }());
	if (Dialog.IsValid()) Dialog->Cancel();
	if (FromSelected.IsValid()) FromSelected->RequestDestroyWindow();
	Scoped.Designer->SelectWidgets({ Card }, /*bAppendOrToggle*/false);
	TSharedPtr<SWindow> FromOther = DreamUILayerStyleDialog::OpenFrom(Plain);
	Dialog = DreamUILayerStyleDialog::Find(*Scoped.Designer);
	TestTrue(TEXT("opened from another, it is on that one alone"),
		Dialog.IsValid() && Dialog->GetSession()->GetTargetNames() == TArray<FString>({ TEXT("Plain") }));
	if (Dialog.IsValid()) Dialog->Cancel();
	if (FromOther.IsValid()) FromOther->RequestDestroyWindow();
	TestFalse(TEXT("a widget of no designer opens nothing"), DreamUILayerStyleDialog::OpenFrom(nullptr).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleDialogEditsTest,
	"DreamGUI.Designer.StyleDialog.TickingAddingReorderingAndEditingArePreviewedOnTheWidgetAndNowhereElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * On a widget with no style: ticking Drop Shadow makes one, + makes a second, a parameter edited and the two reordered --
 * each previewed on the designer's widget at once (a layer style made for the preview), while the authoring template has
 * no style and the file is untouched. Selecting a row shows its parameters, the bevel's contour page only the contour's.
 * A rebuild of the preview keeps showing the edit.
 */
bool FDreamLayerStyleDialogEditsTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleDialogTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogEdits.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogEdits"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const FString Before = File.Read();

	FOpenDialog Open(*Scoped.Designer, { Scoped.FindPreview(TEXT("Plain")) });
	if (!TestTrue(TEXT("the dialog opens"), Open.Dialog.IsValid())) return false;
	SDreamUILayerStyleDialog& Dialog = *Open.Dialog;
	FDreamUILayerStyleSession& Session = Dialog.GetSession().Get();

	Dialog.SetChecked({ EDreamUIStyleDialogPage::DropShadow, INDEX_NONE }, true);
	TestEqual(TEXT("ticking Drop Shadow makes one"), Session.GetStyle().DropShadows.Num(), 1);
	TestTrue(TEXT("and shows it"), Dialog.GetSelectedRow() == (FDreamUIStyleDialogRow{ EDreamUIStyleDialogPage::DropShadow, 0 }));
	TestTrue(TEXT("its parameters"), Dialog.GetShownStruct() == FDreamStyleDropShadow::StaticStruct());
	TestFalse(TEXT("but not its tick, which is the row's"), Dialog.IsFieldShown(TEXT("bEnabled")));
	UDreamLayerStyle* Previewed = FindStyle(Scoped.FindPreview(TEXT("Plain")));
	TestTrue(TEXT("the preview has a style with the shadow"), Previewed != nullptr && Previewed->GetStyle().DropShadows.Num() == 1);
	TestNull(TEXT("the template has none"), FindStyle(Scoped.FindTemplate(TEXT("Plain"))));

	Dialog.AddInstance({ EDreamUIStyleDialogPage::DropShadow, 0 });
	TestEqual(TEXT("+ makes a second"), Session.GetStyle().DropShadows.Num(), 2);
	TestTrue(TEXT("and shows it"), Dialog.GetSelectedRow() == (FDreamUIStyleDialogRow{ EDreamUIStyleDialogPage::DropShadow, 1 }));
	const UScriptStruct* Struct = nullptr;
	FDreamStyleDropShadow* Second = static_cast<FDreamStyleDropShadow*>(Session.GetPageMemory({ EDreamUIStyleDialogPage::DropShadow, 1 }, Struct));
	if (TestNotNull(TEXT("its parameters are the working copy's"), Second))
	{
		Second->Distance = 42.0f;
		Session.NotifyChanged(EDreamUIStyleDialogPage::DropShadow, TEXT("Distance"));
	}
	Previewed = FindStyle(Scoped.FindPreview(TEXT("Plain")));
	TestTrue(TEXT("an edited parameter is previewed at once"), Previewed != nullptr && Previewed->GetStyle().DropShadows.Num() == 2
		&& Previewed->GetStyle().DropShadows[1].Distance == 42.0f);

	Dialog.MoveInstance({ EDreamUIStyleDialogPage::DropShadow, 1 }, { EDreamUIStyleDialogPage::DropShadow, 0 });
	TestEqual(TEXT("dragged onto the first, the second draws first"), Session.GetStyle().DropShadows[0].Distance, 42.0f);
	Previewed = FindStyle(Scoped.FindPreview(TEXT("Plain")));
	TestTrue(TEXT("and the preview has them in that order"), Previewed != nullptr && Previewed->GetStyle().DropShadows[0].Distance == 42.0f);

	Dialog.SelectRow({ EDreamUIStyleDialogPage::BevelContour, INDEX_NONE });
	TestTrue(TEXT("the contour page shows the bevel"), Dialog.GetShownStruct() == FDreamStyleBevelEmboss::StaticStruct());
	TestTrue(TEXT("its contour"), Dialog.IsFieldShown(TEXT("Contour")));
	TestFalse(TEXT("and not its size"), Dialog.IsFieldShown(TEXT("Size")));
	Dialog.SelectRow({ EDreamUIStyleDialogPage::BevelEmboss, INDEX_NONE });
	TestTrue(TEXT("the bevel's own page shows its size"), Dialog.IsFieldShown(TEXT("Size")));
	TestFalse(TEXT("and not the contour"), Dialog.IsFieldShown(TEXT("Contour")));

	Scoped.Designer->GetPreviewHost()->RebuildPreview();
	Previewed = FindStyle(Scoped.FindPreview(TEXT("Plain")));
	TestTrue(TEXT("a rebuilt preview still shows the edit"), Previewed != nullptr && Previewed->GetStyle().DropShadows.Num() == 2);

	TestEqual(TEXT("nothing reached the file"), File.Read(), Before);
	TestNull(TEXT("nor the template"), FindStyle(Scoped.FindTemplate(TEXT("Plain"))));
	Dialog.Cancel();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleDialogCancelTest,
	"DreamGUI.Designer.StyleDialog.CancelPutsThePreviewBackAndTheFileIsAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Opened on a styled widget and a plain one, the glow made bigger and a satin ticked, the blend mode changed: Cancel puts
 * the styled widget's preview style back, takes the style made for the plain one's preview away again, restores the blend
 * mode, and the file is byte for byte what it was.
 */
bool FDreamLayerStyleDialogCancelTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleDialogTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogCancel.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogCancel"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const FString Before = File.Read();
	const FDreamLayerEffects CardBefore = FindStyle(Scoped.FindPreview(TEXT("Card")))->GetStyle();

	FOpenDialog Open(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")), Scoped.FindPreview(TEXT("Plain")) });
	if (!TestTrue(TEXT("the dialog opens"), Open.Dialog.IsValid())) return false;
	FDreamUILayerStyleSession& Session = Open.Dialog->GetSession().Get();
	Session.GetMutableStyle().OuterGlow.Size = 30.0f;
	Session.NotifyChanged(EDreamUIStyleDialogPage::OuterGlow, TEXT("Size"));
	Open.Dialog->SetChecked({ EDreamUIStyleDialogPage::Satin, INDEX_NONE }, true);
	Session.GetMutableBlending().BlendMode = EDreamUIBlendMode::Screen;
	Session.NotifyChanged(EDreamUIStyleDialogPage::Blending, TEXT("BlendMode"));
	TestEqual(TEXT("the card's preview shows the bigger glow"), FindStyle(Scoped.FindPreview(TEXT("Card")))->GetStyle().OuterGlow.Size, 30.0f);
	TestNotNull(TEXT("the plain widget's preview has a style for the dialog"), FindStyle(Scoped.FindPreview(TEXT("Plain"))));
	const UDreamVisualBatchMesh* PlainVisual = Cast<UDreamVisualBatchMesh>(Scoped.FindPreview(TEXT("Plain"))->GetVisual());
	TestTrue(TEXT("and its blend mode"), PlainVisual != nullptr && PlainVisual->GetBlendMode() == EDreamUIBlendMode::Screen);

	Open.Dialog->Cancel();
	TestTrue(TEXT("cancelled, the card's preview has its own style again"), FindStyle(Scoped.FindPreview(TEXT("Card")))->GetStyle() == CardBefore);
	TestNull(TEXT("the plain one's has no style"), FindStyle(Scoped.FindPreview(TEXT("Plain"))));
	TestTrue(TEXT("and blends as it did"), PlainVisual != nullptr && PlainVisual->GetBlendMode() == EDreamUIBlendMode::Alpha);
	TestEqual(TEXT("the file is byte for byte what it was"), File.Read(), Before);
	TestFalse(TEXT("the dialog is closed"), DreamUILayerStyleDialog::Find(*Scoped.Designer).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleDialogOkTest,
	"DreamGUI.Designer.StyleDialog.OKWritesEveryWidgetInOneUndoAndNothingWhenNothingChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * OK with nothing changed writes nothing. Opened again on both widgets, a satin ticked and the blend mode changed: OK
 * writes the style and the blend mode into both widgets' lines and the compiled tree; one undo puts the file back.
 */
bool FDreamLayerStyleDialogOkTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleDialogTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogOk.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogOk"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const FString Before = File.Read();

	{
		FOpenDialog Untouched(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")) });
		if (!TestTrue(TEXT("the dialog opens"), Untouched.Dialog.IsValid())) return false;
		TestEqual(TEXT("OK with nothing changed writes nothing"), Untouched.Dialog->Ok(), 0);
		TestEqual(TEXT("the file is as it was"), File.Read(), Before);
	}

	FOpenDialog Open(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")), Scoped.FindPreview(TEXT("Plain")) });
	if (!TestTrue(TEXT("the dialog opens again"), Open.Dialog.IsValid())) return false;
	FDreamUILayerStyleSession& Session = Open.Dialog->GetSession().Get();
	Open.Dialog->SetChecked({ EDreamUIStyleDialogPage::Satin, INDEX_NONE }, true);
	Session.GetMutableBlending().BlendMode = EDreamUIBlendMode::Multiply;
	Session.NotifyChanged(EDreamUIStyleDialogPage::Blending, TEXT("BlendMode"));
	TestEqual(TEXT("OK writes both widgets"), Open.Dialog->Ok(), 2);

	const FString After = File.Read();
	TestNotEqual(TEXT("the file changed"), After, Before);
	for (const TCHAR* Name : { TEXT("Card"), TEXT("Plain") })
	{
		UDreamWidget* Template = Scoped.FindTemplate(Name);
		const UDreamLayerStyle* Style = FindStyle(Template);
		TestTrue(*FString::Printf(TEXT("%s has the satin"), Name), Style != nullptr && Style->GetStyle().Satin.bEnabled);
		TestTrue(*FString::Printf(TEXT("%s keeps the card's drop shadow"), Name), Style != nullptr && Style->GetStyle().DropShadows.Num() == 1);
		const UDreamVisualBatchMesh* Visual = Template != nullptr ? Cast<UDreamVisualBatchMesh>(Template->GetVisual()) : nullptr;
		TestTrue(*FString::Printf(TEXT("%s blends by Multiply"), Name), Visual != nullptr && Visual->GetBlendMode() == EDreamUIBlendMode::Multiply);
	}
	TestTrue(TEXT("the plain widget's lines say so"), After.Contains(TEXT("Style.Satin.bEnabled = true")) && After.Contains(TEXT("BlendMode = Multiply")));

	if (!TestTrue(TEXT("there is one thing to undo"), GEditor->UndoTransaction())) return false;
	TestEqual(TEXT("and undoing it puts the file back"), File.Read(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleDialogNewStyleTest,
	"DreamGUI.Designer.StyleDialog.NewStyleKeepsTheWorkingStyleAsAPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * New Style hands the save its working style and the layer's blending, named after the widget; made into an asset the way
 * the New Style window makes one, the preset holds the edited style. The widgets are not written.
 */
bool FDreamLayerStyleDialogNewStyleTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleDialogTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogNewStyle.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write(Lines()))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogNewStyle"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const FString Before = File.Read();

	FOpenDialog Open(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")) });
	if (!TestTrue(TEXT("the dialog opens"), Open.Dialog.IsValid())) return false;
	Open.Dialog->GetSession()->GetMutableStyle().OuterGlow.Size = 21.0f;
	Open.Dialog->GetSession()->NotifyChanged(EDreamUIStyleDialogPage::OuterGlow, TEXT("Size"));

	UDreamLayerStylePreset* Made = nullptr;
	FDreamUIStylePresetSaveArgs Seen;
	const bool bMade = Open.Dialog->NewStyle([&Made, &Seen](const FDreamUIStylePresetSaveArgs& InArgs)
	{
		Seen = InArgs;
		FDreamUIStylePresetSaveArgs Args = InArgs;
		Args.PackagePath = TEXT("/Temp/DreamGUITests/StyleDialog");
		Made = DreamUIStylePresets::CreatePresetAsset(Args);
		return Made != nullptr;
	});
	TestTrue(TEXT("New Style makes a preset"), bMade);
	TestTrue(TEXT("named after the widget"), Seen.Name.Contains(TEXT("Card")));
	TestTrue(TEXT("with the layer's blending to keep"), Seen.Blending.IsSet());
	if (TestNotNull(TEXT("an asset"), Made))
	{
		TestEqual(TEXT("holding the edited glow"), Made->GetStyle().OuterGlow.Size, 21.0f);
		TestEqual(TEXT("and the card's shadow"), Made->GetStyle().DropShadows.Num(), 1);
		Made->MarkAsGarbage();
	}
	TestEqual(TEXT("the widgets are not written"), File.Read(), Before);
	Open.Dialog->Cancel();
	return true;
}

#endif
