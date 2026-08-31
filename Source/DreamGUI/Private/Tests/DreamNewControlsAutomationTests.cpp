// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamProgressBar.h"
#include "Controls/DreamRadioButton.h"
#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/UIButton.h"
#include "Interaction/UITextInput.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The three controls added after the first four, one test each, aimed the same way as the library
 * suite: at the wiring that fails SILENTLY. A fill whose anchors nobody drives is a bar stuck at
 * where it was built; a transition aimed at the wrong visual dies on the next hover; a selectable
 * without explicit colours ships white; a value that reaches the property but not the field is two
 * numbers wearing one name.
 *
 * Everything runs headless: no world, no registration, no layout pass. That shapes two assertions
 * here -- colours are read from behaviour state or through immediate application paths (the tween
 * manager returns null without a world), and the spin box's value is read from the input
 * behaviour's text and the control's own push onto the visual, because the behaviour's visual write
 * sits behind a render-canvas check.
 */
namespace DreamNewControlsTestLocal
{
	template<class T>
	T* Make()
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->Initialize();
		return Control;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlProgressBarTest,
	"DreamGUI.Controls.ProgressBar.ThePercentIsTheFillsHorizontalAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlProgressBarTest::RunTest(const FString& Parameters)
{
	using namespace DreamNewControlsTestLocal;

	// Authored before initialization, the way a .dui line would leave it.
	TStrongObjectPtr<UDreamProgressBar> Bar(NewObject<UDreamProgressBar>(GetTransientPackage()));
	Bar->Percent = 0.6f;
	Bar->Initialize();

	if (!TestNotNull(TEXT("the track exists"), Bar->TrackNode.Get()) ||
		!TestNotNull(TEXT("the fill exists"), Bar->FillNode.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the fill lives inside the track"),
		(UObject*)Bar->FillNode->GetParent() == (UObject*)Bar->TrackNode.Get());

	// There is no behaviour here on purpose; the control is the only writer of this anchor, so the
	// anchor IS the claim.
	TestEqual(TEXT("the authored percent arrived as the fill's anchor"),
		static_cast<float>(Bar->FillNode->GetAnchorMax().X), 0.6f);

	Bar->SetPercent(0.25f);
	TestEqual(TEXT("SetPercent moves the fill's horizontal max"),
		static_cast<float>(Bar->FillNode->GetAnchorMax().X), 0.25f);
	TestEqual(TEXT("while its horizontal min stays at the track's start"),
		static_cast<float>(Bar->FillNode->GetAnchorMin().X), 0.0f);
	TestEqual(TEXT("and the vertical span stays whole -- bottom"),
		static_cast<float>(Bar->FillNode->GetAnchorMin().Y), 0.0f);
	TestEqual(TEXT("and the vertical span stays whole -- top"),
		static_cast<float>(Bar->FillNode->GetAnchorMax().Y), 1.0f);

	// UMG's split: the property keeps the author's number, the geometry clamps.
	Bar->SetPercent(1.7f);
	TestEqual(TEXT("the property stores what was set"), Bar->GetPercent(), 1.7f);
	TestEqual(TEXT("the fill never leaves the track"),
		static_cast<float>(Bar->FillNode->GetAnchorMax().X), 1.0f);

	// The style reached the parts -- absolute colours, no behaviour in between to carry them.
	if (UDreamVisual* TrackVisual = Bar->TrackNode->GetVisual())
	{
		TestEqual(TEXT("the track wears the style's colour"), TrackVisual->GetColor(), FDreamProgressBarStyle().TrackColor);
	}
	if (UDreamVisual* FillVisual = Bar->FillNode->GetVisual())
	{
		TestEqual(TEXT("the fill wears the style's colour"), FillVisual->GetColor(), FDreamProgressBarStyle().FillColor);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlRadioButtonTest,
	"DreamGUI.Controls.RadioButton.ItsTwoTransitionsLandOnTwoVisualsAndTheGroupIsHonoured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlRadioButtonTest::RunTest(const FString& Parameters)
{
	using namespace DreamNewControlsTestLocal;

	// Authored: checked, with an inline checked colour to watch arrive. (No project sheet under a
	// test, so ResolveStyle falls back to the inline Style without StyleSource being touched.)
	TStrongObjectPtr<UDreamRadioButton> Radio(NewObject<UDreamRadioButton>(GetTransientPackage()));
	Radio->Style.DotChecked = FColor(11, 22, 33, 255);
	Radio->bIsOn = true;
	Radio->Initialize();

	if (!TestNotNull(TEXT("the toggle behaviour is always there"), Radio->ToggleBehaviour.Get()) ||
		!TestNotNull(TEXT("the box exists"), Radio->BoxNode.Get()) ||
		!TestNotNull(TEXT("the dot exists"), Radio->DotNode.Get()))
	{
		return false;
	}

	// The library's recurring claim, restated for the dot: two transitions, two visuals. On one
	// visual they overwrite each other and the checked state dies on the next hover.
	UDreamVisual* Hover = Radio->ToggleBehaviour->GetTransitionTarget();
	UDreamVisual* Checked = Radio->ToggleBehaviour->GetToggleTransitionTarget();
	TestNotNull(TEXT("the pointer transition has a target"), Hover);
	TestNotNull(TEXT("the checked transition has a target"), Checked);
	TestTrue(TEXT("they are two visuals, not one"), (UObject*)Hover != (UObject*)Checked);
	TestTrue(TEXT("the pointer transition tints the box"),
		(UObject*)Hover == (UObject*)(Radio->BoxNode != nullptr ? Radio->BoxNode->GetVisual() : nullptr));
	TestTrue(TEXT("the checked transition tints the dot"),
		(UObject*)Checked == (UObject*)(Radio->DotNode != nullptr ? Radio->DotNode->GetVisual() : nullptr));

	// The authored value reached the behaviour, and the checked colour reached the dot through the
	// immediate path (SetOnColor applies at once when the toggle is on; no tween runs worldless).
	TestTrue(TEXT("the authored value reached the behaviour"), Radio->GetIsOn());
	if (UDreamVisual* DotVisual = Radio->DotNode->GetVisual())
	{
		TestEqual(TEXT("the dot is wearing the checked colour"), DotVisual->GetColor(), FColor(11, 22, 33, 255));
	}

	// The dot is a rect block, which states no intrinsic size: the authored width is what the
	// centred overlay slot's desired-size fallback reads.
	TestEqual(TEXT("the dot states the style's size to the layout"),
		Radio->DotNode->GetWidth(), static_cast<float>(FDreamRadioButtonStyle().DotSize.X));

	// What makes it read as a radio out of the box: the default radius is half the default box.
	TestEqual(TEXT("the default corner radius is half the default box"),
		static_cast<double>(FDreamRadioButtonStyle().CornerRadius), FDreamRadioButtonStyle().BoxSize.X * 0.5);

	// The group: a passthrough to the behaviour, and the behaviour's group semantics through it.
	// Selecting one member switches the other off, and the mirror property follows.
	TStrongObjectPtr<UDreamRadioButton> Other(Make<UDreamRadioButton>());
	TStrongObjectPtr<UUIToggleGroup> Group(NewObject<UUIToggleGroup>(GetTransientPackage()));
	Radio->SetToggleGroup(Group.Get());
	Other->SetToggleGroup(Group.Get());
	TestTrue(TEXT("the group passthrough reaches the behaviour"),
		(UObject*)Radio->GetToggleGroup() == (UObject*)Group.Get());
	TestTrue(TEXT("joining while on made this the group's selection"),
		(UObject*)Group->GetSelectedItem() == (UObject*)Radio->ToggleBehaviour.Get());

	Other->SetIsOn(true);
	TestTrue(TEXT("selecting a sibling switched this one off"), !Radio->GetIsOn());
	TestTrue(TEXT("and the mirror property followed"), !Radio->bIsOn);
	TestTrue(TEXT("the sibling is on"), Other->GetIsOn());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlSpinBoxTest,
	"DreamGUI.Controls.SpinBox.TheValueReachesTheFieldAndSteppingClampsAtTheEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlSpinBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamNewControlsTestLocal;

	TStrongObjectPtr<UDreamSpinBox> Spin(NewObject<UDreamSpinBox>(GetTransientPackage()));
	Spin->Value = 2.5f;
	Spin->Initialize();

	if (!TestNotNull(TEXT("the field behaviour is always there"), Spin->InputBehaviour.Get()) ||
		!TestNotNull(TEXT("the decrement behaviour is always there"), Spin->DecrementBehaviour.Get()) ||
		!TestNotNull(TEXT("the increment behaviour is always there"), Spin->IncrementBehaviour.Get()))
	{
		return false;
	}

	// The parts reached their behaviours: the field edits THROUGH the text visual, and each step
	// face's pointer transition tints the face it stands on -- three targets, three visuals.
	TestTrue(TEXT("the value text was handed to the field"),
		(UObject*)Spin->InputBehaviour->GetTextComponent()
			== (UObject*)(Spin->ValueTextNode != nullptr ? Spin->ValueTextNode->GetVisual() : nullptr));
	TestTrue(TEXT("the decrement transition tints its own face"),
		(UObject*)Spin->DecrementBehaviour->GetTransitionTarget()
			== (UObject*)(Spin->DecrementNode != nullptr ? Spin->DecrementNode->GetVisual() : nullptr));
	TestTrue(TEXT("the increment transition tints its own face"),
		(UObject*)Spin->IncrementBehaviour->GetTransitionTarget()
			== (UObject*)(Spin->IncrementNode != nullptr ? Spin->IncrementNode->GetVisual() : nullptr));
	TestTrue(TEXT("the two faces are two visuals"),
		(UObject*)Spin->DecrementBehaviour->GetTransitionTarget()
			!= (UObject*)Spin->IncrementBehaviour->GetTransitionTarget());

	// The white trap: the field's behaviour is a selectable and tints the field's own visual, so
	// unset transition colours ship a white bar. The style's colours must be ON the behaviour.
	TestEqual(TEXT("the field's normal colour is the style's background"),
		Spin->InputBehaviour->GetNormalColor(), FDreamSpinBoxStyle().FieldBackground);
	TestEqual(TEXT("a step face's normal colour is the style's"),
		Spin->DecrementBehaviour->GetNormalColor(), FDreamSpinBoxStyle().ButtonNormal);

	// The authored value arrived as culture-invariant text -- in the behaviour's state, and on the
	// visual through the control's own push (the behaviour's visual write needs a render canvas).
	TestEqual(TEXT("the authored value reached the field's text"),
		Spin->InputBehaviour->GetText(), FString(TEXT("2.5")));
	if (UDreamText* ValueVisual = Cast<UDreamText>(Spin->ValueTextNode != nullptr ? Spin->ValueTextNode->GetVisual() : nullptr))
	{
		TestEqual(TEXT("and the glyphs show it"), ValueVisual->GetText().ToString(), FString(TEXT("2.5")));
	}
	TestEqual(TEXT("the overflow is clipped"),
		Spin->ClipNode->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);

	// A fractional value over a fractional value. UUITextInput's DecimalNumber filter refuses a dot
	// the OLD text already contains, so a naive wholesale push turns "3.5" into "35"; the control
	// replaces through empty to give the filter nothing stale to refuse against. This assertion is
	// what notices if that workaround is ever simplified away.
	Spin->SetValue(3.5f);
	TestEqual(TEXT("a fractional push over a fractional field keeps its dot"),
		Spin->InputBehaviour->GetText(), FString(TEXT("3.5")));

	// Stepping from code clamps at the edges and never overshoots.
	Spin->SetValue(99.0f);
	Spin->StepSize = 5.0f;
	Spin->Increment();
	TestEqual(TEXT("a step against the ceiling lands ON the ceiling"), Spin->GetValue(), 100.0f);
	TestEqual(TEXT("and the field spells the clamped value"),
		Spin->InputBehaviour->GetText(), FString(TEXT("100")));
	Spin->Increment();
	TestEqual(TEXT("a step at the ceiling stays there"), Spin->GetValue(), 100.0f);
	Spin->SetValue(2.0f);
	Spin->Decrement();
	TestEqual(TEXT("a step through the floor lands ON the floor"), Spin->GetValue(), 0.0f);

	// The submit road, driven through the behaviour's own event: parseable text becomes the clamped
	// value; unparseable text is refused and the field snaps back to the number the control holds.
	Spin->InputBehaviour->GetOnSubmitEvent().Broadcast(TEXT("41"));
	TestEqual(TEXT("submitted text became the value"), Spin->GetValue(), 41.0f);
	Spin->InputBehaviour->GetOnSubmitEvent().Broadcast(TEXT("not a number"));
	TestEqual(TEXT("junk did not become a value"), Spin->GetValue(), 41.0f);
	TestEqual(TEXT("and the field snapped back to the held value"),
		Spin->InputBehaviour->GetText(), FString(TEXT("41")));
	Spin->InputBehaviour->GetOnSubmitEvent().Broadcast(TEXT("500"));
	TestEqual(TEXT("an out-of-range submit clamps"), Spin->GetValue(), 100.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
