// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamListView.h"
#include "Controls/DreamMenuAnchor.h"
#include "Controls/DreamRingMenu.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamTabView.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UIToggle.h"
#include "WaitUntil.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamListsInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * THE WORLD WITH NO TWEENS.
 *
 * Every animated thing a control does -- a pointer state's colour, a check mark, a popup fading in, a
 * ring growing open, a glide to a scroll position -- goes through UDreamTweenManager, and the tween
 * manager is a game instance subsystem. A world that belongs to no game instance has none, and there
 * every UDreamTweenManager::To answers null. That world is not exotic: it is the designer's preview,
 * which is where the dropdown once crashed chaining OnComplete onto exactly that null. The rig's default
 * world belongs to a game instance and plays tweens, so the other case has to be asked for
 * (FDreamRigOptions::bWithGameInstance = false), and this file is where it is.
 *
 * The rule for such a world is the one UDreamMenuAnchor::Open states: with no tween to play, land on
 * the END state -- a popup that stayed at its start opacity would be open and invisible. Each test makes
 * one control do its animated thing through the real pointer pipeline and asserts two things: nothing
 * crashed on the way (the test got to the end), and the control arrived where the animation would
 * have taken it. Where a control has a written contract of its own (the menu anchor's, the ring's and
 * the dropdown's snap-to-end fallbacks; the list's reveal, which never animates; the expandable area's
 * own ticked expansion), that is what is asserted; where it has none, the end-state rule is.
 *
 * "Arrived" is waited for rather than read at once, for up to SettleSeconds -- longer than any default
 * transition -- so the claim is that the end state is reached, not how.
 */
namespace DreamNoTweenWorldTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/**
	 * How long an end state is given to arrive. The longest transition these controls play by default
	 * is a quarter of a second; a second is generous whether the end state is written at once or
	 * reached some other way, and a control that never gets there fails here instead of hanging.
	 */
	const float SettleSeconds = 1.0f;

	/** The designer preview's kind of world: made with UWorld::CreateWorld, owned by no game instance. */
	FDreamRigOptions NoTweenWorldOptions()
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.bWithGameInstance = false;
		return Options;
	}

	/** The premise every test here stands on, stated: this world has no tween manager to ask. */
	bool HasNoTweenManager(FAutomationTestBase& InTest, const FDreamDriverRig& InRig)
	{
		return InTest.TestNull(TEXT("The rig's world has no tween manager, as the designer's preview has none"),
			UDreamTweenManager::GetDreamTweenInstance(InRig.GetWorld()));
	}

	/**
	 * Let frames pass until InCondition holds, for at most SettleSeconds. False when it never does, in
	 * which case the wait has already reported InWhat against the bound test.
	 */
	bool SettlesOn(FDreamDriverRig& InRig, TFunction<bool()> InCondition, const FString& InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(SettleSeconds);
		return InRig.Driver()->Wait(FDreamUntil::Condition(MoveTemp(InCondition), Timeout), Timeout, InWhat);
	}

	/** A viewport pixel well clear of InRect: the far quadrant from its centre. */
	FVector2D PixelAwayFrom(const FBox2D& InRect)
	{
		const FVector2D Centre = InRect.GetCenter();
		return FVector2D(
			Centre.X < ViewportSize.X * 0.5 ? ViewportSize.X - 40.0 : 40.0,
			Centre.Y < ViewportSize.Y * 0.5 ? ViewportSize.Y - 40.0 : 40.0);
	}

	/** Whether InInner's rectangle lies inside InOuter's along the viewport's Y, give or take a pixel. */
	bool IsInsideVertically(const FDreamElementRef& InInner, const FDreamElementRef& InOuter)
	{
		const TOptional<FBox2D> Inner = InInner->GetPixelRect();
		const TOptional<FBox2D> Outer = InOuter->GetPixelRect();
		return Inner.IsSet() && Outer.IsSet()
			&& Inner->Min.Y >= Outer->Min.Y - 1.0
			&& Inner->Max.Y <= Outer->Max.Y + 1.0;
	}

	/** A scroll box of InRowCount rows of InRowSize, re-measured and laid out, the rows handed back in order. */
	UDreamScrollBox* MakeFilledBox(FDreamDriverRig& InRig, const FVector2D& InSize, int32 InRowCount, const FVector2D& InRowSize,
		TArray<UDreamWidget*>& OutRows)
	{
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, InSize);
		if (Box == nullptr || Box->GetContentNode() == nullptr)
		{
			return Box;
		}
		for (int32 RowIndex = 0; RowIndex < InRowCount; ++RowIndex)
		{
			OutRows.Add(InRig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), InRowSize));
		}
		// The public call for "the content changed, measure it again"; AddContent is a parent plus this.
		Box->RefreshContentExtent();
		InRig.PumpFrames(2);
		return Box;
	}

	/** A list of InItemCount items, rows exactly InRowHeight apart, laid out. The items are handed back so they stay referenced. */
	UDreamListView* MakeList(FDreamDriverRig& InRig, int32 InItemCount, float InRowHeight, TArray<UObject*>& OutItems)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, FVector2D(300.0, 400.0));
		if (List == nullptr)
		{
			return nullptr;
		}
		// Inline, so a project sheet cannot decide how tall a row is.
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), InRowHeight));
		OutItems = DreamListsInteraction::MakeItems(InItemCount);
		List->SetItemObjects(OutItems);
		InRig.PumpFrames(2);
		return List;
	}
}

/*
 * UUISelectable::ApplyPointerSelectionState plays every state change through UDreamTweenManager::To and,
 * when that answers null, writes the state's colour at once. Before that fallback nothing was written,
 * so the face kept whatever it was first drawn with -- including through the style push that is meant
 * to give it its normal colour. Written to the end-state rule.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldButtonFaceTest,
	"DreamGUI.NoTweenWorld.AButtonsFaceLandsOnEachPointerStatesColourWithNoTweenToFadeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldButtonFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 80.0));
	UUIButton* Selectable = Button != nullptr ? Button->ButtonBehaviour.Get() : nullptr;
	UDreamVisual* Face = Selectable != nullptr ? Selectable->GetTransitionTarget() : nullptr;
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button)
		|| !TestNotNull(TEXT("It tints the face it stands on"), Face))
	{
		return false;
	}
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(1);

	const FColor Normal = Selectable->GetNormalColor();
	const FColor Hovered = Selectable->GetHoveredColor();
	const FColor Pressed = Selectable->GetPressedColor();
	// Each claim below compares against one state's colour; two equal colours would let a face that
	// never moved pass for one that did.
	if (!TestTrue(TEXT("The hovered colour differs from the normal one"), Hovered != Normal)
		|| !TestTrue(TEXT("And the pressed colour from the hovered one"), Pressed != Hovered))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Play = Driver->Find(FDreamBy::Widget(Button));
	const TOptional<FBox2D> Rect = Play->GetPixelRect();
	if (!TestTrue(TEXT("The button has a place on the viewport"), Rect.IsSet()))
	{
		return false;
	}

	// At rest, before anything touches it. Getting here is itself a transition: the style push sets the
	// normal colour while the button is in its normal state.
	TestTrue(TEXT("At rest the face shows the normal colour"),
		SettlesOn(Rig, [Face, Normal]() { return Face->GetColor() == Normal; }, TEXT("the face showing the normal colour")));

	TestTrue(TEXT("Hovering the button completes"), Play->Hover());
	TestTrue(TEXT("Hovered, the face shows the hovered colour"),
		SettlesOn(Rig, [Face, Hovered]() { return Face->GetColor() == Hovered; }, TEXT("the face showing the hovered colour")));

	// Off again before anything presses it: a button that has been clicked keeps the selection, and off
	// the pointer it is then Focused rather than Normal, which is a different colour and a different test.
	TestTrue(TEXT("Moving off the button completes"), Driver->Sequence().MoveToPixel(PixelAwayFrom(Rect.GetValue())).Perform());
	TestTrue(TEXT("Off the button, the face is back at the normal colour"),
		SettlesOn(Rig, [Face, Normal]() { return Face->GetColor() == Normal; }, TEXT("the face returning to the normal colour")));

	TestTrue(TEXT("Pressing the button completes"), Play->Press());
	TestTrue(TEXT("Pressed, the face shows the pressed colour"),
		SettlesOn(Rig, [Face, Pressed]() { return Face->GetColor() == Pressed; }, TEXT("the face showing the pressed colour")));

	TestTrue(TEXT("Letting go over the button completes"), Play->Release());
	TestEqual(TEXT("That was one click, tweens or no tweens"), Listener->ClickedCount, 1);
	TestTrue(TEXT("Let go of while still hovered, the face is back at the hovered colour"),
		SettlesOn(Rig, [Face, Hovered]() { return Face->GetColor() == Hovered; }, TEXT("the face returning to the hovered colour")));
	return true;
}

/*
 * Two transitions, and both write their colour at once when the tween request answers null: the box's
 * pointer one (UUISelectable::ApplyPointerSelectionState) and the tick's checked one
 * (UUIToggle::ApplyValueToVisual). Without that fallback both dropped their colour, so a toggle clicked
 * in such a world was checked and still showed the unchecked tick. Written to the end-state rule. The
 * check itself, and its announcement, do not depend on either and must hold regardless.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldToggleTest,
	"DreamGUI.NoTweenWorld.AToggleLandsItsBoxOnTheHoveredColourAndItsTickOnTheCheckedColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldToggleTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamToggle* Toggle = Rig.MakeControl<UDreamToggle>(TEXT("Mute"), nullptr, FVector2D(40.0, 40.0));
	UUIToggle* Behaviour = Toggle != nullptr ? Toggle->ToggleBehaviour.Get() : nullptr;
	UDreamVisual* Box = Behaviour != nullptr ? Behaviour->GetTransitionTarget() : nullptr;
	UDreamVisual* Tick = Behaviour != nullptr ? Behaviour->GetToggleTransitionTarget() : nullptr;
	if (!TestNotNull(TEXT("A toggle can be made on the rig"), Toggle)
		|| !TestNotNull(TEXT("Its box is tinted by the pointer transition"), Box)
		|| !TestNotNull(TEXT("And its tick by the checked one"), Tick))
	{
		return false;
	}
	Toggle->SetCheckedState(EDreamCheckState::Unchecked);
	Toggle->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
	Rig.PumpFrames(1);

	const FColor BoxNormal = Behaviour->GetNormalColor();
	const FColor BoxHovered = Behaviour->GetHoveredColor();
	const FColor TickChecked = Behaviour->GetOnColor();
	const FColor TickUnchecked = Behaviour->GetOffColor();
	if (!TestTrue(TEXT("The box's hovered colour differs from its normal one"), BoxHovered != BoxNormal)
		|| !TestTrue(TEXT("And the checked tick colour from the unchecked one"), TickChecked != TickUnchecked))
	{
		return false;
	}

	// The unchecked colour is written immediately when the style is pushed (UUIToggle::SetOffColor on a
	// toggle that is off), so this one holds whatever the tweens do.
	TestTrue(TEXT("Unchecked, the tick shows the unchecked colour"),
		SettlesOn(Rig, [Tick, TickUnchecked]() { return Tick->GetColor() == TickUnchecked; }, TEXT("the tick showing the unchecked colour")));

	FDreamElementRef Mute = Rig.Driver()->Find(FDreamBy::Widget(Toggle));
	TestTrue(TEXT("Hovering the toggle completes"), Mute->Hover());
	TestTrue(TEXT("Hovered, the box shows the hovered colour"),
		SettlesOn(Rig, [Box, BoxHovered]() { return Box->GetColor() == BoxHovered; }, TEXT("the box showing the hovered colour")));

	TestTrue(TEXT("Clicking the toggle completes"), Mute->Click());
	TestTrue(TEXT("One click checks it, tweens or no tweens"), Toggle->IsChecked());
	if (TestEqual(TEXT("And says so once"), Listener->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("Carrying Checked"), Listener->CheckStates[0], EDreamCheckState::Checked);
	}
	TestTrue(TEXT("Checked, the tick shows the checked colour"),
		SettlesOn(Rig, [Tick, TickChecked]() { return Tick->GetColor() == TickChecked; }, TEXT("the tick showing the checked colour")));
	return true;
}

/*
 * UUIDropdown::Show and ::Hide fade the list with RenderOpacityTo and write the end state themselves
 * when it answers null: fully opaque when shown, transparent and asleep when hidden. Hide is also where
 * OnComplete used to be chained straight onto that null; this is the test that would have caught it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldDropdownTest,
	"DreamGUI.NoTweenWorld.ADropdownOpensFullyOpaqueAndClosesAsleepWhenAnOptionIsChosen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldDropdownTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	// Near the top of the screen, so its list has room to open downward.
	UDreamDropdown* Dropdown = Rig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr,
		FVector2D(200.0, 40.0), FVector2D(0.0, 200.0));
	if (!TestNotNull(TEXT("A dropdown can be made on the rig"), Dropdown)
		|| !TestNotNull(TEXT("It has a list to open"), Dropdown->ListNode.Get()))
	{
		return false;
	}
	Dropdown->SetOptions({
		FText::AsCultureInvariant(TEXT("Low")),
		FText::AsCultureInvariant(TEXT("Medium")),
		FText::AsCultureInvariant(TEXT("High")) });
	Dropdown->SetSelectedIndex(0);
	Dropdown->OnSelectionChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleSelectionChanged);
	Dropdown->OnItemGenerated.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleItemGenerated);
	Rig.PumpFrames(1);
	UDreamWidget* List = Dropdown->ListNode;

	FDreamDriverRef Driver = Rig.Driver();
	TestTrue(TEXT("Clicking the dropdown completes"), Driver->Find(FDreamBy::Widget(Dropdown))->Click());
	TestTrue(TEXT("The click opened the list"), Dropdown->IsOpen());
	TestTrue(TEXT("The open list is awake"), List->GetWidgetActive());
	TestTrue(TEXT("Open, the list is fully opaque"),
		SettlesOn(Rig, [List]() { return FMath::IsNearlyEqual(List->GetRenderOpacity(), 1.0f, 0.001f); }, TEXT("the open list becoming fully opaque")));
	// Two frames: the first lays out the rows the open just created, the second anything arranging
	// them dirtied -- the settling the rig does before its own first action.
	Rig.PumpFrames(2);

	if (!TestTrue(TEXT("Opening the list generated a row for the second option"),
		Listener->GeneratedItems.IsValidIndex(1) && Listener->GeneratedItems[1] != nullptr))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the second option completes"), Driver->Find(FDreamBy::Widget(Listener->GeneratedItems[1].Get()))->Click());
	TestEqual(TEXT("The second option is chosen"), Dropdown->GetSelectedIndex(), 1);
	TestEqual(TEXT("And the change was announced once"), Listener->SelectionIndices.Num(), 1);
	TestFalse(TEXT("Choosing closes the list"), Dropdown->IsOpen());
	TestTrue(TEXT("Closed, the list is asleep"),
		SettlesOn(Rig, [List]() { return !List->GetWidgetActive(); }, TEXT("the closed list going to sleep")));
	return true;
}

/*
 * UDreamMenuAnchor::Open fades the popup in with RenderOpacityTo and, when that answers null, writes
 * full opacity itself -- the fallback this file's rule is named after. Opened the UMG way, from a
 * trigger button's click, and dismissed by a click outside.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldMenuAnchorTest,
	"DreamGUI.NoTweenWorld.AMenuAnchorOpensFullyOpaqueAndAClickOutsideClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldMenuAnchorTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	// Trigger and anchor share a place, left of centre and high; the anchor's own slot is the menu.
	const FVector2D TriggerPosition(-300.0, 150.0);
	const FVector2D TriggerSize(160.0, 40.0);
	UDreamButton* Trigger = Rig.MakeControl<UDreamButton>(TEXT("Trigger"), nullptr, TriggerSize, TriggerPosition);
	UDreamMenuAnchor* Anchor = Rig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, TriggerSize, TriggerPosition);
	if (!TestNotNull(TEXT("A trigger button can be made on the rig"), Trigger)
		|| !TestNotNull(TEXT("A menu anchor can be made on the rig"), Anchor)
		|| !TestNotNull(TEXT("The anchor has a popup to show"), Anchor->PopupNode.Get())
		|| !TestNotNull(TEXT("And a menu node to put content in"), Anchor->MenuNode.Get()))
	{
		return false;
	}
	Rig.MakeWidget(TEXT("MenuItem"), Anchor->MenuNode.Get(), FVector2D(160.0, 40.0));
	Listener->MenuAnchorToOpen = Anchor;
	Trigger->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleTriggerClicked);
	Anchor->OnMenuOpenChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleMenuOpenChanged);
	Rig.PumpFrames(1);
	UDreamWidget* Popup = Anchor->PopupNode;

	FDreamDriverRef Driver = Rig.Driver();
	TestTrue(TEXT("Clicking the trigger completes"), Driver->Find(FDreamBy::Widget(Trigger))->Click());
	TestTrue(TEXT("The trigger's click opened the menu"), Anchor->IsOpen());
	TestTrue(TEXT("The open popup is awake"), Popup->GetWidgetActive());
	TestTrue(TEXT("Open, the popup is fully opaque"),
		SettlesOn(Rig, [Popup]() { return FMath::IsNearlyEqual(Popup->GetRenderOpacity(), 1.0f, 0.001f); }, TEXT("the open popup becoming fully opaque")));
	// The open lifted the popup to the screen and laid a blocker behind it; two frames for both to be
	// arranged before a click aims past them.
	Rig.PumpFrames(2);

	// The bottom-right corner of the viewport, which neither the trigger nor any placement reaches.
	TestTrue(TEXT("Clicking far from the menu completes"),
		Driver->Sequence().MoveToPixel(FVector2D(ViewportSize.X - 80.0, ViewportSize.Y - 40.0)).Press().Release().Perform());
	TestFalse(TEXT("A click outside the open menu closes it"), Anchor->IsOpen());
	TestTrue(TEXT("Closed, the popup is asleep"),
		SettlesOn(Rig, [Popup]() { return !Popup->GetWidgetActive(); }, TEXT("the closed popup going to sleep")));
	if (TestEqual(TEXT("Opening and closing were each announced"), Listener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("The second as closed"), Listener->MenuOpenStates[1]);
	}
	return true;
}

/*
 * The dialog itself plays no animation: it shows as it is made and puts itself away as it closes. What
 * a world with no tweens puts in its way is its buttons, whose pointer transitions all ask for tweens
 * as the pointer crosses them -- so this is the "no tween anywhere does not stop an answer" test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldDialogTest,
	"DreamGUI.NoTweenWorld.ADialogIsAnsweredAndPutsItselfAwayWithNoTweenAnywhere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldDialogTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	// The whole viewport, which is what a standalone dialog stretches to fill.
	UDreamDialog* Dialog = Rig.MakeControl<UDreamDialog>(TEXT("Ask"), nullptr, FVector2D(ViewportSize.X, ViewportSize.Y));
	if (!TestNotNull(TEXT("A dialog can be made on the rig"), Dialog))
	{
		return false;
	}
	Dialog->SetTitle(FText::AsCultureInvariant(TEXT("Delete the save?")));
	Dialog->OnDialogClosed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleDialogClosed);
	Rig.PumpFrames(2);

	// The seeded row, Cancel then OK; the one answering Confirm is what gets clicked.
	UDreamButton* Confirm = nullptr;
	const TArray<FDreamDialogButton> Specs = Dialog->GetButtons();
	for (int32 Index = 0; Index < Specs.Num(); ++Index)
	{
		if (Specs[Index].Result == FName(TEXT("Confirm")) && Dialog->ButtonWidgets.IsValidIndex(Index))
		{
			Confirm = Dialog->ButtonWidgets[Index].Get();
		}
	}
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm))
	{
		return false;
	}
	TestTrue(TEXT("The dialog is up"), Dialog->GetWidgetActive());

	TestTrue(TEXT("Clicking the confirm button completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm))->Click());
	if (TestEqual(TEXT("The dialog closed once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the result of the button that was clicked"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	TestFalse(TEXT("A standalone dialog puts itself away when it closes"), Dialog->GetWidgetActive());
	return true;
}

/*
 * The expandable area's default expansion is instant (ExpansionDuration 0), and its header's pointer
 * colours are the only tweens it asks for; folding and unfolding must not depend on those.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldExpandableAreaTest,
	"DreamGUI.NoTweenWorld.ClickingAnExpandableAreasHeaderFoldsAndUnfoldsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldExpandableAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamExpandableArea* Area = Rig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, FVector2D(300.0, 200.0));
	if (!TestNotNull(TEXT("An expandable area can be made on the rig"), Area)
		|| !TestNotNull(TEXT("It has a header"), Area->HeaderNode.Get())
		|| !TestNotNull(TEXT("And a content column"), Area->ContentNode.Get()))
	{
		return false;
	}
	Rig.MakeWidget(TEXT("Body"), Area->ContentNode.Get(), FVector2D(200.0, 60.0));
	Area->SetIsExpanded(true);
	Area->OnExpansionChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleExpansionChanged);
	Rig.PumpFrames(2);
	UDreamWidget* Content = Area->ContentNode;

	FDreamElementRef Header = Rig.Driver()->Find(FDreamBy::Widget(Area->HeaderNode.Get()));
	TestTrue(TEXT("Clicking the header completes"), Header->Click());
	TestFalse(TEXT("One click folds the area"), Area->GetIsExpanded());
	TestTrue(TEXT("Folded, the content is asleep"),
		SettlesOn(Rig, [Content]() { return !Content->GetWidgetActive(); }, TEXT("the folded content going to sleep")));
	// Folding changes how tall the control is, and so where its header is; one frame for that to be laid
	// out before the next click aims at the header again.
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the header again completes"), Header->Click());
	TestTrue(TEXT("A second click unfolds it"), Area->GetIsExpanded());
	TestTrue(TEXT("Unfolded, the content is awake"),
		SettlesOn(Rig, [Content]() { return Content->GetWidgetActive(); }, TEXT("the unfolded content waking")));
	TestEqual(TEXT("Each flip was announced once"), Listener->ExpansionStates.Num(), 2);
	return true;
}

/*
 * A TIMED expansion is not a tween: UDreamExpandableArea advances its own alpha in NativeOnTick, which
 * the UI manager's frame drives in any world. So in a world with no tweens it still travels, and still
 * arrives -- which is the contract checked here, and what a future move onto tweens would have to keep.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldTimedExpansionTest,
	"DreamGUI.NoTweenWorld.ATimedExpansionStillArrivesBecauseTheAreaTicksItItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldTimedExpansionTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	UDreamExpandableArea* Area = Rig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, FVector2D(300.0, 200.0));
	if (!TestNotNull(TEXT("An expandable area can be made on the rig"), Area)
		|| !TestNotNull(TEXT("It has a header"), Area->HeaderNode.Get())
		|| !TestNotNull(TEXT("And a content column"), Area->ContentNode.Get()))
	{
		return false;
	}
	Rig.MakeWidget(TEXT("Body"), Area->ContentNode.Get(), FVector2D(200.0, 60.0));
	// Expanded at once (the duration is still zero here), then timed from now on.
	Area->SetIsExpanded(true);
	Rig.PumpFrames(2);
	const float ExpandedHeight = Area->GetHeight();
	const float HeaderHeight = Area->HeaderNode->GetHeight();
	if (!TestTrue(FString::Printf(TEXT("Expanded, the area is taller than its header (%.1f against %.1f)"), ExpandedHeight, HeaderHeight),
		ExpandedHeight > HeaderHeight + 1.0f))
	{
		return false;
	}
	Area->SetExpansionDuration(0.2f);
	UDreamWidget* Content = Area->ContentNode;
	FDreamElementRef Header = Rig.Driver()->Find(FDreamBy::Widget(Area->HeaderNode.Get()));

	TestTrue(TEXT("Clicking the header completes"), Header->Click());
	TestFalse(TEXT("The click folds the area"), Area->GetIsExpanded());
	// The click's own frame is the collapse's first: under way, not over.
	TestTrue(TEXT("A frame into the collapse the content is still showing: it travels rather than jumps"), Content->GetWidgetActive());
	TestTrue(TEXT("The collapse arrives: the content goes to sleep"),
		SettlesOn(Rig, [Content]() { return !Content->GetWidgetActive(); }, TEXT("the collapsing content going to sleep")));
	TestTrue(TEXT("And the area is shorter by its content"), Area->GetHeight() < ExpandedHeight - 1.0f);
	// Folding moved the header; one frame for that to be laid out before it is aimed at again.
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the header again completes"), Header->Click());
	TestTrue(TEXT("The click unfolds the area"), Area->GetIsExpanded());
	TestTrue(TEXT("The expansion arrives: the area is back at its expanded height"),
		SettlesOn(Rig, [Area, ExpandedHeight]() { return FMath::IsNearlyEqual(Area->GetHeight(), ExpandedHeight, 0.5f); },
			TEXT("the area growing back to its expanded height")));
	TestTrue(TEXT("And its content is awake"), Content->GetWidgetActive());
	return true;
}

/*
 * UDreamRingMenu::Open and ::Close fade and scale the ring with two tweens and fall back when either is
 * missing: an open ring is written fully opaque at full scale, a closing one is put to sleep at once
 * rather than from the fade's completion callback that will never come.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldRingMenuTest,
	"DreamGUI.NoTweenWorld.ARingMenuClosesAsleepAndReopensFullyOpaqueAtFullScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldRingMenuTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	UDreamRingMenu* Wheel = Rig.MakeControl<UDreamRingMenu>(TEXT("Wheel"), nullptr, FVector2D(400.0, 400.0));
	if (!TestNotNull(TEXT("A ring menu can be made on the rig"), Wheel)
		|| !TestNotNull(TEXT("It has a ring to open"), Wheel->RingNode.Get()))
	{
		return false;
	}
	// Inline, so the look is this instance's own style and not a project sheet's; the defaults are kept.
	Wheel->SetStyleSource(EDreamUIStyleSource::Inline);
	if (!TestTrue(FString::Printf(TEXT("The ring animates its opening by default (%.2f seconds, from %.2f scale)"),
			Wheel->Style.OpenDuration, Wheel->Style.OpenScaleFrom),
		Wheel->Style.OpenDuration > 0.0f && Wheel->Style.OpenScaleFrom < 1.0f))
	{
		return false;
	}
	TArray<FDreamRingMenuItem> Items;
	const TCHAR* const Labels[] = { TEXT("Reload"), TEXT("Grenade"), TEXT("Heal"), TEXT("Melee") };
	for (const TCHAR* Label : Labels)
	{
		FDreamRingMenuItem Item;
		Item.Label = FText::AsCultureInvariant(Label);
		Item.Tag = FName(Label);
		Items.Add(Item);
	}
	Wheel->SetItems(Items);
	Rig.PumpFrames(1);
	UDreamWidget* Ring = Wheel->RingNode;

	Wheel->Close();
	TestFalse(TEXT("Closing closes it"), Wheel->IsOpen());
	TestTrue(TEXT("Closed, the ring is asleep"),
		SettlesOn(Rig, [Ring]() { return !Ring->GetWidgetActive(); }, TEXT("the closed ring going to sleep")));

	Wheel->Open();
	TestTrue(TEXT("Opening opens it"), Wheel->IsOpen());
	TestTrue(TEXT("Open, the ring is awake, fully opaque and at full scale"),
		SettlesOn(Rig, [Ring]()
			{
				return Ring->GetWidgetActive()
					&& FMath::IsNearlyEqual(Ring->GetRenderOpacity(), 1.0f, 0.001f)
					&& Ring->GetRelativeScale().Equals(FVector::OneVector, 0.001);
			},
			TEXT("the open ring reaching full opacity and full scale")));
	return true;
}

/*
 * UUIScrollView::GlideContentTo hands an animated scroll to UDreamTweenManager::To and, when that answers
 * null, puts the content where the glide was going at once. Without that fallback it moved nothing:
 * ScrollWidgetIntoView with its animation on was accepted and then the content stayed where it was.
 * Written to the end-state rule -- land where the glide was going.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldScrollIntoViewTest,
	"DreamGUI.NoTweenWorld.AnAnimatedScrollIntoViewStillBringsTheScrollBoxChildIntoView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldScrollIntoViewTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	// Twenty rows of 100 in a 400-tall window.
	TArray<UDreamWidget*> Rows;
	UDreamScrollBox* Box = MakeFilledBox(Rig, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0), Rows);
	if (!TestNotNull(TEXT("A scroll box can be made on the rig"), Box)
		|| !TestNotNull(TEXT("It has a viewport"), Box->ViewportNode.Get())
		|| !TestTrue(TEXT("And holds twenty rows"), Rows.Num() == 20 && Rows[15] != nullptr))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Target = Driver->Find(FDreamBy::Widget(Rows[15]));
	FDreamElementRef Window = Driver->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	if (!TestFalse(TEXT("The sixteenth row starts outside the window"), IsInsideVertically(Target, Window)))
	{
		return false;
	}

	TestTrue(TEXT("Asking for it to be scrolled into view, animated, is accepted"), Box->ScrollWidgetIntoView(Rows[15], /*bInAnimate*/true));
	TestTrue(TEXT("The sixteenth row comes inside the window"),
		SettlesOn(Rig, [Target, Window]() { return IsInsideVertically(Target, Window); }, TEXT("the sixteenth row coming inside the scroll box's window")));
	TestTrue(TEXT("Because the box's offset moved to put it there"), Box->GetScrollOffset() > 0.5f);
	return true;
}

/*
 * The same glide, reached from the wheel: with AnimateWheelScrolling on, a notch is a GlideContentTo to
 * where the notch would have put the content, and with no tween to play the content lands there at
 * once. Without that fallback a notch moved nothing when the tween request answered null.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldScrollBoxWheelTest,
	"DreamGUI.NoTweenWorld.AnAnimatedWheelNotchStillMovesTheScrollBoxByOneNotch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldScrollBoxWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TArray<UDreamWidget*> Rows;
	UDreamScrollBox* Box = MakeFilledBox(Rig, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0), Rows);
	if (!TestNotNull(TEXT("A scroll box can be made on the rig"), Box)
		|| !TestNotNull(TEXT("It has a viewport"), Box->ViewportNode.Get()))
	{
		return false;
	}
	Box->SetAnimateWheelScrolling(true);
	Rig.PumpFrames(1);
	// One notch is the box's own statement of what a notch travels, read rather than assumed.
	const float Notch = Box->GetScrollSensitivity() * Box->GetWheelScrollMultiplier();
	if (!TestTrue(TEXT("The wheel glides on this box"), Box->GetAnimateWheelScrolling())
		|| !TestTrue(FString::Printf(TEXT("There is more than a notch to scroll (end %.1f, notch %.1f)"), Box->GetScrollOffsetOfEnd(), Notch),
			Notch > 0.5f && Box->GetScrollOffsetOfEnd() > Notch))
	{
		return false;
	}

	FDreamElementRef Window = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	// Toward the user, in the shape the production input actors send a wheel: one notch on both axes.
	TestTrue(TEXT("A notch toward the user completes"), Window->ScrollBy(FVector2D(-1.0, -1.0)));
	TestTrue(TEXT("The box comes to rest one notch down"),
		SettlesOn(Rig, [Box, Notch]() { return FMath::IsNearlyEqual(Box->GetScrollOffset(), Notch, 0.5f); }, TEXT("the box's offset reaching one notch")));
	return true;
}

/*
 * The list's reveal does not animate at all -- UDreamListViewBase::ScrollItemIntoView sets the offset
 * whether or not it was asked to glide -- so it lands in any world. Pinned so that a reveal moved onto a
 * tween has to keep landing where there is none.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldListRevealTest,
	"DreamGUI.NoTweenWorld.ScrollingAListItemIntoViewBringsItsRowOnScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldListRevealTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	// Thirty items forty units apart in a four-hundred-unit window: ten rows on screen, twenty below,
	// and under the virtualization threshold, so every item keeps a row of its own to aim at.
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, 40.0f, Items);
	if (!TestNotNull(TEXT("A list can be made on the rig"), List)
		|| !TestNotNull(TEXT("It has a window"), List->ViewportNode.Get())
		|| !TestNotNull(TEXT("And the twenty-sixth item has a row"), List->GetRowWidget(25)))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Row = Driver->Find(FDreamBy::Widget(List->GetRowWidget(25)));
	FDreamElementRef Window = Driver->Find(FDreamBy::Widget(List->ViewportNode.Get()));
	if (!TestFalse(TEXT("The twenty-sixth row starts below the window"), IsInsideVertically(Row, Window)))
	{
		return false;
	}

	List->ScrollIndexIntoView(25);
	TestTrue(TEXT("The twenty-sixth row comes inside the window"),
		SettlesOn(Rig, [Row, Window]() { return IsInsideVertically(Row, Window); }, TEXT("the twenty-sixth row coming inside the list's window")));
	TestTrue(TEXT("Because the list's offset moved to put it there"), List->GetScrollOffset() > 0.5f);
	return true;
}

/*
 * With bEnableScrollAnimation on, the list's wheel glides through the same UUIScrollView::GlideContentTo
 * as the scroll box's, so with no tween to play it lands where the glide was going at once. Without that
 * fallback the wheel moved nothing when the tween request answered null.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldListWheelTest,
	"DreamGUI.NoTweenWorld.AnAnimatedWheelNotchStillMovesTheListOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldListWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TArray<UObject*> Items;
	UDreamListView* List = MakeList(Rig, 30, 40.0f, Items);
	if (!TestNotNull(TEXT("A list can be made on the rig"), List))
	{
		return false;
	}
	List->SetEnableScrollAnimation(true);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The wheel glides on this list"), List->GetEnableScrollAnimation()))
	{
		return false;
	}
	const float OffsetBefore = List->GetScrollOffset();

	// Toward the user: on towards the later items.
	TestTrue(TEXT("A notch toward the user completes"), Rig.Driver()->Find(FDreamBy::Widget(List))->ScrollBy(FVector2D(-1.0, -1.0)));
	TestTrue(TEXT("The list moves on towards its later items"),
		SettlesOn(Rig, [List, OffsetBefore]() { return List->GetScrollOffset() > OffsetBefore + 0.5f; }, TEXT("the list's offset moving on")));
	return true;
}

/*
 * A tab view switches its page at once; what it animates is the selected plate, which rides each tab's
 * UUIToggle -- lit is the plate's checked colour, unlit the same colour at zero alpha -- and
 * UUIToggle::ApplyValueToVisual writes that colour at once when the tween request answers null. Without
 * that fallback it dropped the colour, so the switch happened and the plate stayed on the tab that was
 * left. The switch is asserted as it must hold; the plate is written to the end-state rule.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoTweenWorldTabViewTest,
	"DreamGUI.NoTweenWorld.SwitchingTabsOpensTheTabAndMovesTheSelectedPlateToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNoTweenWorldTabViewTest::RunTest(const FString& Parameters)
{
	using namespace DreamNoTweenWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(NoTweenWorldOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !HasNoTweenManager(*this, Rig))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamTabView* TabView = Rig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0));
	if (!TestNotNull(TEXT("A tab view can be made on the rig"), TabView))
	{
		return false;
	}
	TabView->SetTabLabels({
		FText::AsCultureInvariant(TEXT("Video")),
		FText::AsCultureInvariant(TEXT("Audio")),
		FText::AsCultureInvariant(TEXT("Input")) });
	TabView->OnTabChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleTabChanged);
	Rig.PumpFrames(2);

	if (!TestEqual(TEXT("The strip has a tab per caption"), TabView->Tabs.Num(), 3)
		|| !TestEqual(TEXT("And the first is open"), TabView->GetActiveTabIndex(), 0))
	{
		return false;
	}
	UUIToggle* FirstToggle = TabView->Tabs[0].Toggle.Get();
	UUIToggle* ThirdToggle = TabView->Tabs[2].Toggle.Get();
	UDreamVisual* FirstPlate = FirstToggle != nullptr ? FirstToggle->GetToggleTransitionTarget() : nullptr;
	UDreamVisual* ThirdPlate = ThirdToggle != nullptr ? ThirdToggle->GetToggleTransitionTarget() : nullptr;
	if (!TestNotNull(TEXT("The first tab has a selected plate"), FirstPlate)
		|| !TestNotNull(TEXT("And so does the third"), ThirdPlate))
	{
		return false;
	}
	const FColor FirstLit = FirstToggle->GetOnColor();
	const FColor FirstUnlit = FirstToggle->GetOffColor();
	const FColor ThirdLit = ThirdToggle->GetOnColor();
	const FColor ThirdUnlit = ThirdToggle->GetOffColor();
	if (!TestTrue(TEXT("A lit plate looks different from an unlit one"), FirstLit != FirstUnlit && ThirdLit != ThirdUnlit))
	{
		return false;
	}

	// Written straight onto the plates by the style push, which applies each toggle's colour for the
	// state it is in without a tween -- so this holds in any world.
	TestTrue(TEXT("At rest the open tab's plate is lit and the third's is not"),
		SettlesOn(Rig, [FirstPlate, FirstLit, ThirdPlate, ThirdUnlit]()
			{
				return FirstPlate->GetColor() == FirstLit && ThirdPlate->GetColor() == ThirdUnlit;
			},
			TEXT("the plates showing which tab is open")));

	TestTrue(TEXT("Clicking the third tab completes"), Rig.Driver()->Find(FDreamBy::Widget(TabView->Tabs[2].TabNode.Get()))->Click());
	TestEqual(TEXT("The third tab is the open one"), TabView->GetActiveTabIndex(), 2);
	TestEqual(TEXT("And the switch was announced once"), Listener->TabChangedIndices.Num(), 1);
	TestTrue(TEXT("The plate lights on the tab that was opened"),
		SettlesOn(Rig, [ThirdPlate, ThirdLit]() { return ThirdPlate->GetColor() == ThirdLit; }, TEXT("the opened tab's plate lighting")));
	TestTrue(TEXT("And goes out on the tab that was left"),
		SettlesOn(Rig, [FirstPlate, FirstUnlit]() { return FirstPlate->GetColor() == FirstUnlit; }, TEXT("the left tab's plate going out")));
	return true;
}

#endif
