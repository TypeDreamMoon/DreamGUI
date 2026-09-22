// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"

/*
 * What "disabled" is allowed to mean, and where navigation is allowed to land.
 *
 * UUISelectable::bInteractable reached the LOOK and nothing else. The raycast gate one level up asks
 * the widget's own hierarchy flag, which is a different property with a different meaning, so a
 * control with bInteractable cleared was painted in its Disabled colours and then went on to hover,
 * press, take selection, click, play its click sound and rumble the pad exactly like a working one.
 * It also had no setter and was not Blueprint-readable, so it was an editor-time trap: the only way
 * to disable a control at runtime was UDreamWidget::SetInteractable, which takes the whole subtree
 * out of the raycast and is the wrong tool for a button that must stay visible and keep its tooltip.
 *
 * The navigation half is the same rule seen from the other side. The Auto scan has always skipped
 * anything not interactable or not navigable-to; the Explicit path skipped every one of those tests
 * and returned whatever was wired, so a gamepad could land on a control the UI had just told the
 * player they could not use -- and then not leave, because the next hop was computed from there.
 */

namespace DreamSelectableInteractableTestLocal
{
	UUISelectable* MakeSelectable(const TCHAR* InName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
		Widget->SetDisplayName(InName);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		return Widget->AddComponent<UUISelectable>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSelectableDisabledDoesNotClickTest,
	"DreamGUI.Interaction.Selectable.AControlPaintedDisabledDoesNotClickOrSoundLikeAWorkingOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSelectableDisabledDoesNotClickTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
	UUIButton* Button = Widget->AddComponent<UUIButton>();
	if (!TestNotNull(TEXT("the widget carries a button"), Button))
	{
		return false;
	}
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());

	int32 ClickCount = 0;
	Button->GetOnClickEvent().AddLambda([&ClickCount]() { ++ClickCount; });

	TestTrue(TEXT("a control starts enabled"), Button->GetInteractable());
	TestTrue(TEXT("...and reads as usable"), Button->IsInteractable());

	// There was no setter at all before this; disabling from game code meant reaching for the
	// widget-level raycast switch, which is a different thing.
	Button->SetInteractable(false);
	TestFalse(TEXT("the flag can be cleared at runtime"), Button->GetInteractable());
	TestEqual(TEXT("...and the control paints itself Disabled"),
		Button->GetSelectionState(), EUISelectableSelectionState::Disabled);

	IDreamPointerClickInterface::Execute_OnPointerClick(Button, EventData);
	TestEqual(TEXT("a disabled button does not click"), ClickCount, 0);

	// Pressing it does not take focus either: OnPointerDown used to hand selection to the control
	// regardless, so a disabled button could hold focus and a gamepad could be stranded on it.
	IDreamPointerDownUpInterface::Execute_OnPointerDown(Button, EventData);
	TestEqual(TEXT("...and a press on it is not a press"),
		Button->GetSelectionState(), EUISelectableSelectionState::Disabled);

	Button->SetInteractable(true);
	TestTrue(TEXT("the control is usable again"), Button->IsInteractable());
	TestEqual(TEXT("...and is not stuck looking pressed from the press it refused"),
		Button->GetSelectionState(), EUISelectableSelectionState::Normal);

	IDreamPointerClickInterface::Execute_OnPointerClick(Button, EventData);
	TestEqual(TEXT("an enabled button clicks"), ClickCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSelectableEnterWithoutEventDataTest,
	"DreamGUI.Interaction.Selectable.AnEnterCarryingNoEventDataIsHandledRatherThanDereferenced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSelectableEnterWithoutEventDataTest::RunTest(const FString& Parameters)
{
	using namespace DreamSelectableInteractableTestLocal;

	/*
	 * OnPointerEnter tested the event data for null on one line and dereferenced it bare on the next,
	 * which is a statement that the author knew it could be null. It can: the enter/exit path is
	 * driven from code that clears a pointer, and the widget-level interface entry points are public.
	 * Reaching the line after the call is the entire assertion.
	 */
	UUISelectable* Selectable = MakeSelectable(TEXT("Lonely"));
	if (!TestNotNull(TEXT("a selectable to enter"), Selectable))
	{
		return false;
	}

	IDreamPointerEnterExitInterface::Execute_OnPointerEnter(Selectable, nullptr);
	TestEqual(TEXT("an enter with no event data reads as a pointer hover, not a navigation focus"),
		Selectable->GetSelectionState(), EUISelectableSelectionState::Hovered);
	TestFalse(TEXT("...and it did not claim focus"), Selectable->IsFocused());

	IDreamPointerEnterExitInterface::Execute_OnPointerExit(Selectable, nullptr);
	TestEqual(TEXT("and the matching exit puts it back to normal"),
		Selectable->GetSelectionState(), EUISelectableSelectionState::Normal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExplicitNavigationSkipsUnusableTest,
	"DreamGUI.Navigation.Explicit.ALinkToAControlThatCannotBeUsedIsFollowedPastRatherThanLandedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamExplicitNavigationSkipsUnusableTest::RunTest(const FString& Parameters)
{
	using namespace DreamSelectableInteractableTestLocal;

	UUISelectable* From = MakeSelectable(TEXT("From"));
	UUISelectable* Middle = MakeSelectable(TEXT("Middle"));
	UUISelectable* Far = MakeSelectable(TEXT("Far"));
	if (!TestNotNull(TEXT("a control to navigate from"), From)
		|| !TestNotNull(TEXT("a control wired as its left neighbour"), Middle)
		|| !TestNotNull(TEXT("and one wired beyond that"), Far))
	{
		return false;
	}

	// A hand-authored row: From -> Middle -> Far, all explicit.
	From->SetNavigationLeft(EUISelectableNavigationMode::Explicit);
	From->SetNavigationLeftExplicit(Middle);
	Middle->SetNavigationLeft(EUISelectableNavigationMode::Explicit);
	Middle->SetNavigationLeftExplicit(Far);

	TestEqual(TEXT("with everything usable, the link lands where it was authored to"),
		From->FindSelectableOnLeft(), Middle);

	// The ordinary reason an authored entry is unusable: it is conditionally disabled.
	Middle->SetInteractable(false);
	TestEqual(TEXT("a link into a disabled control carries on down the author's own chain"),
		From->FindSelectableOnLeft(), Far);

	// The other test the Auto scan applies, and the explicit path equally ignored.
	Middle->SetInteractable(true);
	Middle->SetCanNavigateHere(false);
	TestEqual(TEXT("a control that refuses to be navigated to is skipped the same way"),
		From->FindSelectableOnLeft(), Far);

	// Chain exhausted: staying put beats parking focus somewhere the player was told they cannot go.
	Far->SetInteractable(false);
	TestNull(TEXT("with nothing usable along the chain, the move finds nowhere to go"),
		From->FindSelectableOnLeft());

	// A ring of unusable links must terminate rather than walk itself forever.
	Far->SetNavigationLeft(EUISelectableNavigationMode::Explicit);
	Far->SetNavigationLeftExplicit(Middle);
	TestNull(TEXT("a cycle of unusable links still terminates"), From->FindSelectableOnLeft());
	return true;
}

#endif
