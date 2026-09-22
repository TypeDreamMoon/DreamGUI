// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Interaction/UISelectable.h"

/*
 * Focus and hover used to be the same thing. Navigation reaches a control by synthesising a pointer
 * enter on it -- that is what makes the confirm button press whatever navigation landed on -- so a
 * gamepad move and a mouse hover arrived at the selectable as the same event and drew the same way.
 * They are now told apart by the input type on the event, and being the selected control keeps a
 * widget focused after the pointer has gone elsewhere.
 */

namespace DreamSelectableFocusTestLocal
{
	UUISelectable* MakeSelectable()
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		Widget->OnRegister();
		return Widget->AddComponent<UUISelectable>();
	}

	UDreamPointerEventData* MakeEvent(EDreamUIPointerInputType InputType)
	{
		UDreamPointerEventData* Event = NewObject<UDreamPointerEventData>();
		Event->InputType = InputType;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSelectableFocusVersusHoverTest,
	"DreamGUI.Navigation.Focus.NavigationFocusesWhereAPointerHovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSelectableFocusVersusHoverTest::RunTest(const FString& Parameters)
{
	using namespace DreamSelectableFocusTestLocal;
	UUISelectable* Selectable = MakeSelectable();
	if (!TestNotNull(TEXT("Selectable created"), Selectable))
	{
		return false;
	}

	IDreamPointerEnterExitInterface::Execute_OnPointerEnter(Selectable, MakeEvent(EDreamUIPointerInputType::Pointer));
	TestEqual(TEXT("A real pointer hovers"), Selectable->GetSelectionState(), EUISelectableSelectionState::Hovered);
	TestFalse(TEXT("...and does not focus"), Selectable->IsFocused());
	IDreamPointerEnterExitInterface::Execute_OnPointerExit(Selectable, nullptr);

	IDreamPointerEnterExitInterface::Execute_OnPointerEnter(Selectable, MakeEvent(EDreamUIPointerInputType::Navigation));
	TestEqual(TEXT("A navigation move focuses"), Selectable->GetSelectionState(), EUISelectableSelectionState::Focused);
	TestTrue(TEXT("...and reports itself focused"), Selectable->IsFocused());

	// Pressing still outranks both, or a held button would stop looking held the moment focus moved on.
	IDreamPointerDownUpInterface::Execute_OnPointerDown(Selectable, MakeEvent(EDreamUIPointerInputType::Navigation));
	TestEqual(TEXT("Pressed outranks focused"), Selectable->GetSelectionState(), EUISelectableSelectionState::Pressed);
	IDreamPointerDownUpInterface::Execute_OnPointerUp(Selectable, MakeEvent(EDreamUIPointerInputType::Navigation));

	IDreamPointerEnterExitInterface::Execute_OnPointerExit(Selectable, nullptr);
	TestEqual(TEXT("Leaving with nothing selected is Normal"), Selectable->GetSelectionState(), EUISelectableSelectionState::Normal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSelectableFocusOutlivesThePointerTest,
	"DreamGUI.Navigation.Focus.SelectionOutlivesThePointerLeaving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSelectableFocusOutlivesThePointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSelectableFocusTestLocal;
	UUISelectable* Selectable = MakeSelectable();
	if (!TestNotNull(TEXT("Selectable created"), Selectable))
	{
		return false;
	}

	// Click it, then move the mouse away: it is still the selected control, and that is what a focus
	// ring is for. Before, the exit put it straight back to Normal and the selection was invisible.
	IDreamPointerEnterExitInterface::Execute_OnPointerEnter(Selectable, MakeEvent(EDreamUIPointerInputType::Pointer));
	IDreamPointerSelectDeselectInterface::Execute_OnPointerSelect(Selectable, MakeEvent(EDreamUIPointerInputType::Pointer));
	TestEqual(TEXT("The pointer still wins while it is on the control"), Selectable->GetSelectionState(), EUISelectableSelectionState::Hovered);
	IDreamPointerEnterExitInterface::Execute_OnPointerExit(Selectable, nullptr);
	TestEqual(TEXT("Once it leaves, the selection shows"), Selectable->GetSelectionState(), EUISelectableSelectionState::Focused);

	IDreamPointerSelectDeselectInterface::Execute_OnPointerDeselect(Selectable, MakeEvent(EDreamUIPointerInputType::Pointer));
	TestEqual(TEXT("Deselected goes back to Normal"), Selectable->GetSelectionState(), EUISelectableSelectionState::Normal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSelectableFocusVisualFallbackTest,
	"DreamGUI.Navigation.Focus.FocusBorrowsHoverUntilItIsGivenALook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSelectableFocusVisualFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamSelectableFocusTestLocal;
	UUISelectable* Selectable = MakeSelectable();
	if (!TestNotNull(TEXT("Selectable created"), Selectable))
	{
		return false;
	}
	Selectable->SetHoveredColor(FColor(10, 20, 30, 255));
	Selectable->SetFocusedColor(FColor(90, 80, 70, 255));

	// Focus used to BE hover, so anything authored before it existed must keep looking the same.
	TestFalse(TEXT("Focus visuals are off by default"), Selectable->GetUseFocusedVisuals());
	TestEqual(TEXT("Focused borrows the hovered colour"), Selectable->GetFocusedColor(), FColor(10, 20, 30, 255));

	Selectable->SetUseFocusedVisuals(true);
	TestEqual(TEXT("Switched on, it uses its own"), Selectable->GetFocusedColor(), FColor(90, 80, 70, 255));
	return true;
}

#endif
