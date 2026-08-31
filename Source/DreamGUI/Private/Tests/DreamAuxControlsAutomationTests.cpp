// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamInputKeySelector.h"
#include "Controls/DreamProgressBar.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The three controls that came after the value family, aimed the way the rest of the suite is: at
 * the wiring that fails SILENTLY. A shape that half-changed leaves a ring with a bar's body still
 * on; a collapsed section that still measures expanded leaves a hole in the list it sits in; a
 * selectable with no explicit colours ships white; a label that is not re-pushed with the state says
 * the wrong thing until something else happens to touch it.
 *
 * Everything runs headless: no world, no registration, no layout pass. That shapes two assertions
 * here. Colours are read from BEHAVIOUR state rather than off the visual, because a transition with
 * a duration goes through the tween manager and the tween manager returns null without a world. And
 * sizes are read from the widgets' own authored numbers, which the controls write themselves --
 * which is exactly the property under test, since an Auto-slot consumer measures those.
 */
namespace DreamAuxControlsTestLocal
{
	template<class T>
	T* Make()
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->Initialize();
		return Control;
	}

	UDreamRectBlock* RectOf(const UDreamWidget* InNode)
	{
		return InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr;
	}

	FString TextOf(const UDreamWidget* InNode)
	{
		const UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr;
		return TextVisual != nullptr ? TextVisual->GetText().ToString() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlProgressBarRadialTest,
	"DreamGUI.Controls.ProgressBar.RadialSpendsThePercentAsTheRectsOwnSweptAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlProgressBarRadialTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuxControlsTestLocal;

	const FDreamProgressBarStyle Defaults;

	// Bar first, as the control against the control: the whole claim of the radial path is that it
	// turns something ON that a bar leaves off, and an assertion that only ever sees the radial case
	// cannot tell "enabled by Radial" from "enabled always".
	{
		TDreamTestControl<UDreamProgressBar> Bar(NewObject<UDreamProgressBar>(GetTransientPackage()));
		Bar->Percent = 0.6f;
		Bar->Initialize();
		UDreamRectBlock* TrackRect = RectOf(Bar->TrackNode);
		UDreamRectBlock* FillRect = RectOf(Bar->FillNode);
		if (!TestNotNull(TEXT("the bar's track draws a rect"), TrackRect) ||
			!TestNotNull(TEXT("the bar's fill draws a rect"), FillRect))
		{
			return false;
		}
		TestFalse(TEXT("a bar leaves the track's radial fill off"), TrackRect->GetEnableRadialFill());
		TestFalse(TEXT("a bar leaves the fill's radial fill off"), FillRect->GetEnableRadialFill());
		TestTrue(TEXT("a bar draws its body"), FillRect->GetEnableBody());
		TestFalse(TEXT("a bar draws no border ring"), FillRect->GetEnableBorder());
	}

	// Authored before initialization, the way a .dui line would leave it.
	TDreamTestControl<UDreamProgressBar> Ring(NewObject<UDreamProgressBar>(GetTransientPackage()));
	Ring->Shape = EDreamProgressShape::Radial;
	Ring->Percent = 0.6f;
	Ring->Initialize();

	UDreamRectBlock* TrackRect = RectOf(Ring->TrackNode);
	UDreamRectBlock* FillRect = RectOf(Ring->FillNode);
	if (!TestNotNull(TEXT("the ring's track draws a rect"), TrackRect) ||
		!TestNotNull(TEXT("the ring's fill draws a rect"), FillRect))
	{
		return false;
	}

	// THE claim: one control, two shapes, and the second one is the rect's own wedge rather than a
	// second visual or a mask. Nothing was added to the tree to get here.
	TestTrue(TEXT("radial adds no parts -- the same two nodes serve both shapes"),
		Ring->TrackNode->GetChildrenCount() == 1);
	TestTrue(TEXT("the fill still lives inside the track"),
		(UObject*)Ring->FillNode->GetParent() == (UObject*)Ring->TrackNode.Get());

	TestTrue(TEXT("the fill's radial fill is on"), FillRect->GetEnableRadialFill());
	// The percent, made an angle. 360 is the shader's "no mask at all", so a full bar is a whole ring.
	TestEqual(TEXT("the authored percent arrived as the swept angle"),
		FillRect->GetRadialFillAngle(), 0.6f * 360.0f);
	// The start angle is the style's, turned a quarter: the rect measures its wedge clockwise from
	// three o'clock and this style speaks clockwise from twelve.
	TestEqual(TEXT("the sweep starts where the style says"),
		FillRect->GetRadialFillRotation(), Defaults.RadialStartAngle - 90.0f);
	// The track is the UNFILLED ring and shows all the way round; a wedge on it would eat exactly the
	// part of the ring the fill is not covering.
	TestFalse(TEXT("the track keeps no wedge of its own"), TrackRect->GetEnableRadialFill());

	// The ring itself: a circle drawn as its own border, because a border is the only hole this
	// primitive has. Body ON here would be a pie, not a ring.
	TestFalse(TEXT("the ring's body is off"), FillRect->GetEnableBody());
	TestTrue(TEXT("the ring IS the border"), FillRect->GetEnableBorder());
	TestTrue(TEXT("the border width is stated as a fraction of half the size"),
		FillRect->GetBorderWidthUnitMode() == EDreamRectBlockUnitMode::Percentage);
	TestEqual(TEXT("and that fraction is the style's thickness, untranslated"),
		FillRect->GetBorderWidth(), Defaults.RadialThickness);
	TestTrue(TEXT("the corner radius is a percentage, so the ring stays round at any size"),
		FillRect->GetCornerRadiusUnitMode() == EDreamRectBlockUnitMode::Percentage);
	TestEqual(TEXT("a full-percent radius is a circle"), FillRect->GetCornerRadius().X, 1.0f);
	// The style's colour is written to the VISUAL, which multiplies everything the rect draws. The
	// border's own colour defaults to BLACK, and black times anything is a black ring.
	TestEqual(TEXT("the border carries no colour of its own"), FillRect->GetBorderColor(), FColor::White);
	if (UDreamVisual* FillVisual = Ring->FillNode->GetVisual())
	{
		TestEqual(TEXT("the fill still wears the style's colour"), FillVisual->GetColor(), Defaults.FillColor);
	}

	// A ring is square and states BOTH axes; a bar states only its height.
	TestEqual(TEXT("the control sizes itself to the ring"),
		Ring->GetWidth(), static_cast<float>(Defaults.RadialSize.X));
	TestEqual(TEXT("on both axes"), Ring->GetHeight(), static_cast<float>(Defaults.RadialSize.Y));

	// The same discipline the bar path is under: absolute numbers read from the track's live size,
	// point anchors, nothing left for an anchor setter to resolve against a stretched parent.
	const double TrackWidth = Ring->TrackNode->GetWidth();
	const double TrackHeight = Ring->TrackNode->GetHeight();
	TestEqual(TEXT("the fill covers the track exactly -- width"),
		static_cast<float>(Ring->FillNode->GetSizeDelta().X), static_cast<float>(TrackWidth));
	TestEqual(TEXT("the fill covers the track exactly -- height"),
		static_cast<float>(Ring->FillNode->GetSizeDelta().Y), static_cast<float>(TrackHeight));
	TestEqual(TEXT("concentric: the horizontal anchor is a centre point"),
		static_cast<float>(Ring->FillNode->GetAnchorMin().X), 0.5f);
	TestEqual(TEXT("concentric: the vertical anchor is a centre point"),
		static_cast<float>(Ring->FillNode->GetAnchorMin().Y), 0.5f);

	// The percent moves the angle and nothing else.
	Ring->SetPercent(0.25f);
	TestEqual(TEXT("SetPercent moves the sweep"), FillRect->GetRadialFillAngle(), 90.0f);
	// UMG's split, restated for the ring: the property keeps the author's number, the geometry clamps.
	Ring->SetPercent(1.7f);
	TestEqual(TEXT("the property stores what was set"), Ring->GetPercent(), 1.7f);
	TestEqual(TEXT("the sweep never passes a whole turn"), FillRect->GetRadialFillAngle(), 360.0f);

	// Back to a bar, which is the half that is easy to forget: a shape change has to undo the other
	// shape, or a control that was Radial a moment ago draws a bar with its body still switched off.
	Ring->SetShape(EDreamProgressShape::Bar);
	TestTrue(TEXT("the body comes back"), FillRect->GetEnableBody());
	TestFalse(TEXT("the ring border goes away"), FillRect->GetEnableBorder());
	TestFalse(TEXT("the wedge goes away"), FillRect->GetEnableRadialFill());
	TestTrue(TEXT("the radius is a pixel value again"),
		FillRect->GetCornerRadiusUnitMode() == EDreamRectBlockUnitMode::Value);
	TestEqual(TEXT("and the control measures as a bar again"), Ring->GetHeight(), Defaults.Height);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlExpandableAreaTest,
	"DreamGUI.Controls.ExpandableArea.CollapsingHidesTheContentAndShrinksTheControlToItsHeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlExpandableAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuxControlsTestLocal;

	const FDreamExpandableAreaStyle Defaults;

	TDreamTestControl<UDreamExpandableArea> Area(NewObject<UDreamExpandableArea>(GetTransientPackage()));
	Area->Label = FText::AsCultureInvariant(TEXT("Advanced"));
	Area->Initialize();

	if (!TestNotNull(TEXT("the header exists"), Area->HeaderNode.Get()) ||
		!TestNotNull(TEXT("the content column exists"), Area->ContentNode.Get()) ||
		!TestNotNull(TEXT("the header behaviour is always there"), Area->HeaderBehaviour.Get()))
	{
		return false;
	}

	// The header IS a button: the pointer transition tints the face it is standing on, and the white
	// trap applies here as everywhere -- a selectable-hosted face with no explicit colours ships white.
	TestTrue(TEXT("the header's transition tints its own face"),
		(UObject*)Area->HeaderBehaviour->GetTransitionTarget()
			== (UObject*)(Area->HeaderNode != nullptr ? Area->HeaderNode->GetVisual() : nullptr));
	TestEqual(TEXT("the header's normal colour is the style's"),
		Area->HeaderBehaviour->GetNormalColor(), Defaults.HeaderNormal);
	TestEqual(TEXT("the label says what was authored"), TextOf(Area->LabelNode), FString(TEXT("Advanced")));

	// Content goes in by NESTING, which for code is SetContent. The widget has to belong to this
	// control's own tree, the way a .dui author's nested node belongs to the host's.
	UDreamWidget* Body = Area->GetWidgetTree()->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Body"));
	if (!TestNotNull(TEXT("the test content was constructed"), Body))
	{
		return false;
	}
	Body->SetWidth(80.0f);
	Body->SetHeight(40.0f);
	Area->SetContent(Body);

	TestTrue(TEXT("the content moved into the content column, not beside the control's own root"),
		(UObject*)Body->GetParent() == (UObject*)Area->ContentNode.Get());
	TestTrue(TEXT("and the control can name it"), (UObject*)Area->GetContent() == (UObject*)Body);

	// Expanded by default -- an author writing a section means it to be readable.
	TestTrue(TEXT("it starts expanded"), Area->GetIsExpanded());
	TestTrue(TEXT("the content is awake"), Area->ContentNode->GetWidgetActive());
	// The measure, not the arranged rect: this is what an Auto slot asks the CONTROL for, and the
	// content's own height has to be inside the answer.
	const float ExpandedHeight = Area->GetHeight();
	TestTrue(TEXT("expanded, the control measures the header plus the content"),
		ExpandedHeight >= Defaults.HeaderHeight + 40.0f);

	// THE claim. Collapsed, the content is inactive -- not merely invisible, so it takes no layout
	// space either -- and the control shrinks to exactly the header. An expander that kept its
	// expanded height would leave a hole in every list it is in.
	Area->SetIsExpanded(false);
	TestFalse(TEXT("the content is asleep"), Area->ContentNode->GetWidgetActive());
	TestEqual(TEXT("collapsed, the control IS its header"), Area->GetHeight(), Defaults.HeaderHeight);

	// And back, which is the half a one-way test misses.
	Area->SetIsExpanded(true);
	TestTrue(TEXT("the content wakes again"), Area->ContentNode->GetWidgetActive());
	TestEqual(TEXT("and the height comes back to what it was"), Area->GetHeight(), ExpandedHeight);

	// The header click is wired to the toggle, driven through the behaviour's own event -- there is no
	// pointer headless, and the click road is what a player actually uses.
	Area->HeaderBehaviour->GetOnClickEvent().Broadcast();
	TestFalse(TEXT("a header click collapses it"), Area->GetIsExpanded());
	Area->HeaderBehaviour->GetOnClickEvent().Broadcast();
	TestTrue(TEXT("and the next one opens it"), Area->GetIsExpanded());

	// The indicator is a glyph while the state's brush is empty, and the two states are not the same
	// glyph -- an expander that looks identical open and closed is not one.
	TestTrue(TEXT("the glyph indicator is showing"), Area->ArrowNode->GetWidgetActive());
	TestFalse(TEXT("the image indicator is asleep"), Area->ArrowMarkNode->GetWidgetActive());
	const FString ExpandedGlyph = TextOf(Area->ArrowNode);
	Area->SetIsExpanded(false);
	TestTrue(TEXT("the two states wear different glyphs"), TextOf(Area->ArrowNode) != ExpandedGlyph);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlInputKeySelectorTest,
	"DreamGUI.Controls.InputKeySelector.TheLabelIsTheBoundKeyAndListeningRetintsTheFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlInputKeySelectorTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuxControlsTestLocal;

	const FDreamInputKeySelectorStyle Defaults;

	TDreamTestControl<UDreamInputKeySelector> Selector(Make<UDreamInputKeySelector>());
	if (!TestNotNull(TEXT("the face exists"), Selector->FaceNode.Get()) ||
		!TestNotNull(TEXT("the label exists"), Selector->LabelNode.Get()) ||
		!TestNotNull(TEXT("the button behaviour is always there"), Selector->ButtonBehaviour.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the pointer transition tints the face it stands on"),
		(UObject*)Selector->ButtonBehaviour->GetTransitionTarget()
			== (UObject*)(Selector->FaceNode != nullptr ? Selector->FaceNode->GetVisual() : nullptr));
	// The white trap: unset transition colours ship a white button.
	TestEqual(TEXT("the resting colour is the style's"),
		Selector->ButtonBehaviour->GetNormalColor(), Defaults.Normal);

	// Unbound: some words, and not the name of a key.
	TestFalse(TEXT("nothing is bound to start with"), Selector->GetSelectedKey().IsValid());
	TestTrue(TEXT("an unbound selector still says something"), !TextOf(Selector->LabelNode).IsEmpty());

	// THE label claim: it is the KEY's own display name, which the engine already localizes -- not a
	// spelling this control keeps, which would drift from the one the rest of the game shows.
	Selector->SetSelectedKey(EKeys::SpaceBar);
	TestTrue(TEXT("the value moved"), Selector->GetSelectedKey() == EKeys::SpaceBar);
	TestEqual(TEXT("and the label followed the key"),
		TextOf(Selector->LabelNode), EKeys::SpaceBar.GetDisplayName().ToString());

	// A click ARMS it, and the whole of the "press a key now" feedback is the face's colour. All
	// three states, not just Normal: the pointer is by definition still on the button that was just
	// clicked, so a listening colour written into Normal alone is invisible until the mouse moves.
	Selector->ButtonBehaviour->GetOnClickEvent().Broadcast();
	TestTrue(TEXT("a click arms it"), Selector->GetIsListening());
	TestEqual(TEXT("armed, the resting colour is the listening one"),
		Selector->ButtonBehaviour->GetNormalColor(), Defaults.Listening);
	TestEqual(TEXT("armed, the hovered colour is too"),
		Selector->ButtonBehaviour->GetHoveredColor(), Defaults.Listening);
	TestEqual(TEXT("armed, the pressed colour is too"),
		Selector->ButtonBehaviour->GetPressedColor(), Defaults.Listening);
	TestTrue(TEXT("armed, the label asks for a key rather than naming one"),
		TextOf(Selector->LabelNode) != EKeys::SpaceBar.GetDisplayName().ToString());

	// The fed key. Nothing in this plugin calls NotifyKeyPressed -- the project's input layer does --
	// so this is the contract that entry point states: taken, bound, disarmed.
	TestTrue(TEXT("an armed selector takes the key"), Selector->NotifyKeyPressed(EKeys::G));
	TestFalse(TEXT("and disarms"), Selector->GetIsListening());
	TestTrue(TEXT("the key became the value"), Selector->GetSelectedKey() == EKeys::G);
	TestEqual(TEXT("the label followed it"),
		TextOf(Selector->LabelNode), EKeys::G.GetDisplayName().ToString());
	TestEqual(TEXT("and the face is back to resting"),
		Selector->ButtonBehaviour->GetNormalColor(), Defaults.Normal);

	// Unarmed it refuses, which is what lets a project route every key here without asking first.
	TestFalse(TEXT("an unarmed selector takes nothing"), Selector->NotifyKeyPressed(EKeys::H));
	TestTrue(TEXT("and the binding did not move"), Selector->GetSelectedKey() == EKeys::G);

	// Escape is the guaranteed way out: consumed, so the screen underneath does not also close on it,
	// but never bound.
	Selector->BeginListening();
	TestTrue(TEXT("escape is consumed"), Selector->NotifyKeyPressed(EKeys::Escape));
	TestFalse(TEXT("escape disarms"), Selector->GetIsListening());
	TestTrue(TEXT("escape binds nothing"), Selector->GetSelectedKey() == EKeys::G);

	// A second click while armed is the way out for a player with only a mouse.
	Selector->ButtonBehaviour->GetOnClickEvent().Broadcast();
	TestTrue(TEXT("a click arms it again"), Selector->GetIsListening());
	Selector->ButtonBehaviour->GetOnClickEvent().Broadcast();
	TestFalse(TEXT("and a second click disarms it"), Selector->GetIsListening());

	// The words: empty keeps the built-in ones, an authored value wins -- the same bargain an empty
	// brush strikes with a built-in glyph.
	Selector->NoKeyText = FText::AsCultureInvariant(TEXT("--"));
	Selector->ListeningText = FText::AsCultureInvariant(TEXT("waiting"));
	Selector->SetSelectedKey(FKey());
	Selector->ApplyStyle();
	TestEqual(TEXT("the authored unbound words win"), TextOf(Selector->LabelNode), FString(TEXT("--")));
	Selector->BeginListening();
	TestEqual(TEXT("and the authored prompt wins"), TextOf(Selector->LabelNode), FString(TEXT("waiting")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
