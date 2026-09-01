// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIEventBlocker.h"
#include "Tests/DreamDialogTestTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * A dialog, aimed the same way as the rest of the control suite: at the wiring that fails SILENTLY.
 *
 * Three things here can be wrong while everything still looks built. The centred panel can be given
 * a ratio anchor whose setter resolves the stretched dimmer's span at write time and comes back
 * zero -- the defect family this codebase paid four bugs for in a day, and the reason the panel is
 * asserted as POINT anchors with an absolute size rather than merely as "somewhere in the middle".
 * The button row can be built from the specs and then never told the dialog style's Button /
 * PrimaryButton, in which case every dialog button quietly wears the sheet's plain one and those two
 * style fields do nothing. And a click can reach the right button but answer with the wrong name,
 * which no structural assertion notices at all.
 *
 * Everything runs headless: no world, no registration, no layout pass. That shapes two things. There
 * is no modal subsystem to reach (its Get needs a world), so the dialog under test is in its
 * STANDALONE arrangement and Close puts it to sleep rather than closing a modal -- which is exactly
 * the branch worth pinning here, the hosted one belonging to the subsystem's own tests. And a button
 * is clicked by broadcasting the seam the dialog subscribed to, its UIButton's native click event,
 * which is all a test with no pointer input may claim to be doing.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlDialogStructureTest,
	"DreamGUI.Controls.Dialog.ItsPartsNestAsAStretchedDimmerAndAPointAnchoredPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlDialogStructureTest::RunTest(const FString& Parameters)
{
	// Authored before initialization, the way a .dui line would leave it.
	TDreamTestControl<UDreamDialog> Dialog(NewObject<UDreamDialog>(GetTransientPackage()));
	Dialog->Title = FText::FromString(TEXT("Delete save"));
	Dialog->Message = FText::FromString(TEXT("This cannot be undone."));
	Dialog->Initialize();

	if (!TestNotNull(TEXT("the dimmer exists"), Dialog->DimmerNode.Get()) ||
		!TestNotNull(TEXT("the panel exists"), Dialog->PanelNode.Get()) ||
		!TestNotNull(TEXT("the title exists"), Dialog->TitleNode.Get()) ||
		!TestNotNull(TEXT("the content area exists"), Dialog->ContentNode.Get()) ||
		!TestNotNull(TEXT("the message exists"), Dialog->MessageNode.Get()) ||
		!TestNotNull(TEXT("the button row exists"), Dialog->ButtonRowNode.Get()))
	{
		return false;
	}

	// The dimmer and the panel are SIBLINGS under a root that draws nothing. Nesting the panel inside
	// the dimmer would be one node fewer and a bug: putting the dimmer away when a host already
	// scrims would take the whole dialog with it.
	UDreamWidget* Root = Dialog->GetContentRoot();
	TestNotNull(TEXT("the dialog built a content root"), Root);
	TestTrue(TEXT("the dimmer hangs off the root"), (UObject*)Dialog->DimmerNode->GetParent() == (UObject*)Root);
	TestTrue(TEXT("the panel hangs off the root, beside the dimmer, not inside it"),
		(UObject*)Dialog->PanelNode->GetParent() == (UObject*)Root);
	TestTrue(TEXT("the title is in the panel"),
		(UObject*)Dialog->TitleNode->GetParent() == (UObject*)Dialog->PanelNode.Get());
	TestTrue(TEXT("the content area is in the panel"),
		(UObject*)Dialog->ContentNode->GetParent() == (UObject*)Dialog->PanelNode.Get());
	TestTrue(TEXT("the message is in the content area, which is what makes the area replaceable"),
		(UObject*)Dialog->MessageNode->GetParent() == (UObject*)Dialog->ContentNode.Get());
	TestTrue(TEXT("the button row is in the panel"),
		(UObject*)Dialog->ButtonRowNode->GetParent() == (UObject*)Dialog->PanelNode.Get());

	// The standalone arrangement's input blocking lives on the dimmer. Under the modal subsystem the
	// subsystem's own layer carries one and this node is asleep; nothing about the tree changes.
	TestNotNull(TEXT("the dimmer eats pointer events aimed at what is behind it"),
		Dialog->DimmerNode->GetComponent<UUIEventBlocker>());

	// A stretch, which is (0,0)-(1,1) AND a zero SizeDelta. The zero delta is the whole point: it
	// says "exactly the parent's span, whenever the span is decided", so there is nothing for the
	// anchor setter to resolve against a parent rect that has not been arranged yet.
	TestEqual(TEXT("the dimmer's anchor min is the parent's corner -- X"),
		static_cast<float>(Dialog->DimmerNode->GetAnchorMin().X), 0.0f);
	TestEqual(TEXT("the dimmer's anchor max is the far corner -- X"),
		static_cast<float>(Dialog->DimmerNode->GetAnchorMax().X), 1.0f);
	TestEqual(TEXT("the dimmer's anchor max is the far corner -- Y"),
		static_cast<float>(Dialog->DimmerNode->GetAnchorMax().Y), 1.0f);
	TestEqual(TEXT("a stretched dimmer carries no size delta at all"),
		static_cast<float>(Dialog->DimmerNode->GetSizeDelta().X), 0.0f);

	// The panel: POINT anchors on both axes and an absolute size. A ratio anchor would read the same
	// on paper and be resolved by its SETTER against the stretched dimmer's SizeDelta -- zero on
	// every frame but a full-layout one -- so the panel would be born zero-sized. This assertion is
	// what notices if anyone ever "simplifies" it back.
	TestEqual(TEXT("the panel's horizontal anchor is a point -- min"),
		static_cast<float>(Dialog->PanelNode->GetAnchorMin().X), 0.5f);
	TestEqual(TEXT("the panel's horizontal anchor is a point -- max"),
		static_cast<float>(Dialog->PanelNode->GetAnchorMax().X), 0.5f);
	TestEqual(TEXT("the panel's vertical anchor is a point -- min"),
		static_cast<float>(Dialog->PanelNode->GetAnchorMin().Y), 0.5f);
	TestEqual(TEXT("the panel's vertical anchor is a point -- max"),
		static_cast<float>(Dialog->PanelNode->GetAnchorMax().Y), 0.5f);
	TestEqual(TEXT("its pivot is its middle, so the anchor point is the panel's centre"),
		static_cast<float>(Dialog->PanelNode->GetPivot().X), 0.5f);
	TestEqual(TEXT("and its width is the style's number, absolutely"),
		static_cast<float>(Dialog->PanelNode->GetSizeDelta().X),
		static_cast<float>(FDreamDialogStyle().PanelSize.X));
	TestEqual(TEXT("and its height too"),
		static_cast<float>(Dialog->PanelNode->GetSizeDelta().Y),
		static_cast<float>(FDreamDialogStyle().PanelSize.Y));

	// The words arrived at the glyphs, not merely at the properties.
	if (UDreamText* TitleVisual = Cast<UDreamText>(Dialog->TitleNode->GetVisual()))
	{
		TestEqual(TEXT("the authored title reached the title's text"),
			TitleVisual->GetText().ToString(), FString(TEXT("Delete save")));
	}
	if (UDreamText* MessageVisual = Cast<UDreamText>(Dialog->MessageNode->GetVisual()))
	{
		TestEqual(TEXT("the authored message reached the message's text"),
			MessageVisual->GetText().ToString(), FString(TEXT("This cannot be undone.")));
	}
	TestTrue(TEXT("a written title is shown"), Dialog->TitleNode->GetWidgetActive());

	// Empty means ABSENT, not a blank line: an Auto slot around an unwritten title would otherwise
	// keep a line's worth of gap above the message forever.
	Dialog->SetTitle(FText::GetEmpty());
	TestFalse(TEXT("an empty title is put away rather than reserving a line"),
		Dialog->TitleNode->GetWidgetActive());
	Dialog->SetMessage(FText::GetEmpty());
	TestFalse(TEXT("and an empty message likewise"), Dialog->MessageNode->GetWidgetActive());
	TestTrue(TEXT("but the content area stays -- it is where other content goes"),
		Dialog->ContentNode->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlDialogButtonsTest,
	"DreamGUI.Controls.Dialog.OneButtonPerSpecAndAClickAnswersWithThatButtonsResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlDialogButtonsTest::RunTest(const FString& Parameters)
{
	// The seeded default first: .dui cannot write an array literal yet, so a dialog nobody configured
	// still has to be answerable or the tag is useless the moment it is placed.
	{
		TDreamTestControl<UDreamDialog> Bare(NewObject<UDreamDialog>(GetTransientPackage()));
		Bare->Initialize();
		TestEqual(TEXT("an unconfigured dialog still offers two buttons"), Bare->ButtonWidgets.Num(), 2);
		if (Bare->Buttons.Num() == 2)
		{
			TestEqual(TEXT("the first answers Cancel"), Bare->Buttons[0].Result.ToString(), FString(TEXT("Cancel")));
			TestEqual(TEXT("the second answers Confirm"), Bare->Buttons[1].Result.ToString(), FString(TEXT("Confirm")));
			TestTrue(TEXT("and the confirming one is the primary"), Bare->Buttons[1].bIsPrimary);
		}
	}

	TDreamTestControl<UDreamDialog> Dialog(NewObject<UDreamDialog>(GetTransientPackage()));
	// Distinct inline colours, because the two default button styles are identical structs and an
	// assertion against them would pass no matter which one each button got.
	Dialog->Style.Button.Normal = FColor(11, 22, 33, 255);
	Dialog->Style.PrimaryButton.Normal = FColor(200, 100, 50, 255);
	Dialog->Buttons = {
		FDreamDialogButton(FText::FromString(TEXT("Cancel")), TEXT("Cancel"), false),
		FDreamDialogButton(FText::FromString(TEXT("Discard")), TEXT("Discard"), false),
		FDreamDialogButton(FText::FromString(TEXT("Save")), TEXT("Confirm"), true),
	};
	Dialog->Initialize();

	if (!TestEqual(TEXT("one button per spec, in order"), Dialog->ButtonWidgets.Num(), 3))
	{
		return false;
	}

	static const TCHAR* ExpectedLabels[] = { TEXT("Cancel"), TEXT("Discard"), TEXT("Save") };
	for (int32 Index = 0; Index < Dialog->ButtonWidgets.Num(); ++Index)
	{
		UDreamButton* Button = Dialog->ButtonWidgets[Index].Get();
		if (!TestNotNull(TEXT("the button was built"), Button))
		{
			return false;
		}
		TestTrue(TEXT("it lives in the button row"),
			(UObject*)Button->GetParent() == (UObject*)Dialog->ButtonRowNode.Get());
		// A button draws no text of its own, so the dialog is a HOST here: the label is a widget of
		// the dialog's tree hanging in the button's hole. Reaching it through the hole is what a
		// reader would do, and it is the only place it has ever been -- there is no label property
		// on the button to check first and no second copy to disagree with this one.
		if (UDreamText* LabelVisual = Cast<UDreamText>(
			Button->ContentNode != nullptr && Button->ContentNode->GetChildrenCount() > 0
				? Button->ContentNode->GetChildByIndex(0)->GetVisual()
				: nullptr))
		{
			TestEqual(TEXT("the glyphs in its hole are the spec's label"),
				LabelVisual->GetText().ToString(), FString(ExpectedLabels[Index]));
		}
		else
		{
			AddError(FString::Printf(TEXT("button %d has no text in its content hole"), Index));
		}
		// Inline, deliberately: with the sheet as the source every dialog button would re-resolve to
		// the sheet's plain button and FDreamDialogStyle::Button / ::PrimaryButton would do nothing.
		TestEqual(TEXT("the button reads its look from the dialog, not from the sheet"),
			static_cast<int32>(Button->StyleSource), static_cast<int32>(EDreamUIStyleSource::Inline));
	}
	TestEqual(TEXT("a plain button wears the dialog style's Button"),
		Dialog->ButtonWidgets[0]->Style.Normal, FColor(11, 22, 33, 255));
	TestEqual(TEXT("the primary one wears PrimaryButton instead"),
		Dialog->ButtonWidgets[2]->Style.Normal, FColor(200, 100, 50, 255));

	// The click. Driving the UIButton's native click event is not simulating a press -- it is the
	// seam the dialog subscribed to, exercised directly, which is all a headless test may claim.
	TStrongObjectPtr<UDreamDialogResultProbe> Clicked(NewObject<UDreamDialogResultProbe>(GetTransientPackage()));
	TStrongObjectPtr<UDreamDialogResultProbe> Closed(NewObject<UDreamDialogResultProbe>(GetTransientPackage()));
	Dialog->OnButtonClicked.AddDynamic(Clicked.Get(), &UDreamDialogResultProbe::Record);
	Dialog->OnDialogClosed.AddDynamic(Closed.Get(), &UDreamDialogResultProbe::Record);

	UUIButton* MiddleBehaviour = Dialog->ButtonWidgets[1]->ButtonBehaviour;
	if (!TestNotNull(TEXT("every dialog button carries a UIButton -- a button that cannot be clicked is not one"),
		MiddleBehaviour))
	{
		return false;
	}
	MiddleBehaviour->GetOnClickEvent().Broadcast();

	// The name, not an index: inserting a button in the middle must not re-map anyone's handler, and
	// this is the assertion that would fail if the wiring ever went back to carrying positions.
	TestEqual(TEXT("the click answered with the clicked button's own result"),
		Clicked->LastResult.ToString(), FString(TEXT("Discard")));
	TestEqual(TEXT("...exactly once"), Clicked->CallCount, 1);
	TestEqual(TEXT("and the dialog closed with the same name"),
		Closed->LastResult.ToString(), FString(TEXT("Discard")));
	TestEqual(TEXT("...exactly once"), Closed->CallCount, 1);
	// Standalone, because there is no world and therefore no modal subsystem to hand the result to:
	// Close puts the dialog back to the state a .dui-placed one waits in.
	TestFalse(TEXT("a standalone dialog put itself away rather than lingering"), Dialog->GetWidgetActive());

	// The specs are the only source the row is made from, so replacing them replaces the widgets.
	Dialog->SetButtons({ FDreamDialogButton(FText::FromString(TEXT("Close")), TEXT("Close"), true) });
	TestEqual(TEXT("re-specifying the buttons rebuilt the row"), Dialog->ButtonWidgets.Num(), 1);
	if (Dialog->ButtonWidgets.Num() == 1 && IsValid(Dialog->ButtonWidgets[0]))
	{
		const UDreamWidget* Hole = Dialog->ButtonWidgets[0]->ContentNode.Get();
		if (UDreamText* LabelVisual = Cast<UDreamText>(
			IsValid(Hole) && Hole->GetChildrenCount() > 0 ? Hole->GetChildByIndex(0)->GetVisual() : nullptr))
		{
			TestEqual(TEXT("with the new label"), LabelVisual->GetText().ToString(), FString(TEXT("Close")));
		}
		else
		{
			AddError(TEXT("the rebuilt button has no text in its content hole"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlDialogStyleTest,
	"DreamGUI.Controls.Dialog.EveryStyleNumberReachesAPartOrABehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlDialogStyleTest::RunTest(const FString& Parameters)
{
	// No project sheet under a test, so ResolveStyle falls back to the inline Style without
	// StyleSource being touched -- the same arrangement the rest of the control suite relies on.
	TDreamTestControl<UDreamDialog> Dialog(NewObject<UDreamDialog>(GetTransientPackage()));
	Dialog->Title = FText::FromString(TEXT("Title"));
	Dialog->Message = FText::FromString(TEXT("Message"));
	Dialog->Style.DimmerColor = FColor(1, 2, 3, 200);
	Dialog->Style.PanelBackground = FColor(9, 8, 7, 255);
	Dialog->Style.PanelSize = FVector2D(360.0, 180.0);
	Dialog->Style.PanelPadding = FMargin(11.0f);
	Dialog->Style.CornerRadius = 13.0f;
	Dialog->Style.TitleColor = FColor(31, 32, 33, 255);
	Dialog->Style.TitleFontSize = 23.0f;
	Dialog->Style.MessageColor = FColor(41, 42, 43, 255);
	Dialog->Style.MessageFontSize = 12.0f;
	Dialog->Style.Spacing = 17.0f;
	Dialog->Style.ButtonSpacing = 19.0f;
	Dialog->Style.Button.Normal = FColor(51, 52, 53, 255);
	Dialog->Style.Button.Height = 44.0f;
	Dialog->Style.PrimaryButton.Normal = FColor(61, 62, 63, 255);
	Dialog->Style.PrimaryButton.Height = 40.0f;
	Dialog->Style.ButtonLabelColor = FColor(71, 72, 73, 255);
	Dialog->Style.ButtonLabelFontSize = 14.0f;
	Dialog->Initialize();

	if (!TestNotNull(TEXT("the dimmer exists"), Dialog->DimmerNode.Get()) ||
		!TestNotNull(TEXT("the panel exists"), Dialog->PanelNode.Get()) ||
		!TestNotNull(TEXT("the button row exists"), Dialog->ButtonRowNode.Get()))
	{
		return false;
	}

	// Absolute colours, with no behaviour in between to carry them: nothing in the dialog's own tree
	// hosts a UUISelectable, so there is no transition writing these and no second source to
	// disagree with.
	if (UDreamVisual* DimmerVisual = Dialog->DimmerNode->GetVisual())
	{
		TestEqual(TEXT("the dimmer wears the style's colour"), DimmerVisual->GetColor(), FColor(1, 2, 3, 200));
	}
	if (UDreamVisual* PanelVisual = Dialog->PanelNode->GetVisual())
	{
		TestEqual(TEXT("the panel wears the style's background"), PanelVisual->GetColor(), FColor(9, 8, 7, 255));
	}
	if (UDreamRectBlock* PanelRect = Cast<UDreamRectBlock>(Dialog->PanelNode->GetVisual()))
	{
		TestEqual(TEXT("the panel is rounded by the style"), PanelRect->GetCornerRadius().X, 13.0f);
	}
	TestEqual(TEXT("the panel is the style's size"),
		static_cast<float>(Dialog->PanelNode->GetSizeDelta().X), 360.0f);
	TestEqual(TEXT("...on both axes"),
		static_cast<float>(Dialog->PanelNode->GetSizeDelta().Y), 180.0f);

	if (UDreamText* TitleVisual = Cast<UDreamText>(
		Dialog->TitleNode != nullptr ? Dialog->TitleNode->GetVisual() : nullptr))
	{
		TestEqual(TEXT("the title is the style's size"), TitleVisual->GetFontSize(), 23.0f);
		TestEqual(TEXT("and the style's colour"), TitleVisual->GetColor(), FColor(31, 32, 33, 255));
	}
	if (UDreamText* MessageVisual = Cast<UDreamText>(
		Dialog->MessageNode != nullptr ? Dialog->MessageNode->GetVisual() : nullptr))
	{
		TestEqual(TEXT("the message is the style's size"), MessageVisual->GetFontSize(), 12.0f);
		TestEqual(TEXT("and the style's colour"), MessageVisual->GetColor(), FColor(41, 42, 43, 255));
	}

	// The two gaps a dialog has an opinion about live on the two stack boxes, not in any node's
	// geometry: Spacing between title / content / buttons, ButtonSpacing between the buttons.
	if (UDreamLayoutContainerStackBox* Column = Cast<UDreamLayoutContainerStackBox>(Dialog->PanelNode->GetLayoutContainer()))
	{
		TestEqual(TEXT("the panel's column carries the style's spacing"), Column->Spacing, 17.0f);
		TestEqual(TEXT("and the style's padding"), Column->Padding.Left, 11.0f);
	}
	else
	{
		AddError(TEXT("the panel lays its parts out as a column"));
	}
	if (UDreamLayoutContainerStackBox* Row = Cast<UDreamLayoutContainerStackBox>(Dialog->ButtonRowNode->GetLayoutContainer()))
	{
		TestEqual(TEXT("the button row carries the style's button spacing"), Row->Spacing, 19.0f);
	}
	else
	{
		AddError(TEXT("the button row lays its buttons out in a row"));
	}

	// The row's Auto slot SNAPSHOTS an authored size, and its first capture happens before any style
	// was ever applied -- so the height has to be re-written (and the snapshot re-taken) or the row
	// measures the pre-style default forever. The taller of the two button styles, because either
	// kind may be the tall one.
	TestEqual(TEXT("the row is exactly one button tall"), Dialog->ButtonRowNode->GetHeight(), 44.0f);

	// The white trap: a UUISelectable with no explicit colours renders white, so the dialog style's
	// two button looks must land ON the behaviours, not merely on the widgets' properties.
	if (Dialog->ButtonWidgets.Num() == 2
		&& IsValid(Dialog->ButtonWidgets[0]) && IsValid(Dialog->ButtonWidgets[1])
		&& Dialog->ButtonWidgets[0]->ButtonBehaviour != nullptr
		&& Dialog->ButtonWidgets[1]->ButtonBehaviour != nullptr)
	{
		TestEqual(TEXT("the plain button's selectable got the dialog's Button colour"),
			Dialog->ButtonWidgets[0]->ButtonBehaviour->GetNormalColor(), FColor(51, 52, 53, 255));
		TestEqual(TEXT("the primary button's selectable got PrimaryButton's"),
			Dialog->ButtonWidgets[1]->ButtonBehaviour->GetNormalColor(), FColor(61, 62, 63, 255));

		// The wording is the DIALOG's to describe, because the dialog is what puts it there: a button
		// draws no text of its own. These two numbers have no reader anywhere else, so they are
		// exactly the pair that would quietly do nothing if the dialog stopped applying them.
		auto LabelOf = [](const UDreamButton* InButton) -> UDreamText*
		{
			const UDreamWidget* Hole = IsValid(InButton) ? InButton->ContentNode.Get() : nullptr;
			return Cast<UDreamText>(IsValid(Hole) && Hole->GetChildrenCount() > 0
				? Hole->GetChildByIndex(0)->GetVisual() : nullptr);
		};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (UDreamText* Label = LabelOf(Dialog->ButtonWidgets[Index]))
			{
				TestEqual(TEXT("the button's label wears the dialog's label colour"),
					Label->GetColor(), FColor(71, 72, 73, 255));
				TestEqual(TEXT("and its label font size"), Label->GetFontSize(), 14.0f);
			}
			else
			{
				AddError(FString::Printf(TEXT("button %d has no label in its hole"), Index));
			}
		}
	}
	else
	{
		AddError(TEXT("the seeded pair of buttons was built with behaviours"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
