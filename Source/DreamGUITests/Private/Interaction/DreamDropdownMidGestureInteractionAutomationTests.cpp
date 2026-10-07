// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamDropdown.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UIDropdown.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamDropdown under a finger, and caught while its list fades in or out.
 *
 * UMG's combo box is an SComboButton over an SMenuAnchor: the list is a menu on the Slate menu stack, it is up the moment it
 * is pushed and gone the moment it is dismissed (FMenuStack::DismissInternal, Slate/Private/Framework/Application/
 * MenuStack.cpp:667-676), and every push brings it in from transparent (the new menu window's InitialOpacity, :490-503). So
 * whatever a fade is doing, the list is open exactly while the player last opened it and has not closed it since, a list
 * that has been closed answers nothing, and a list opened again is a list that is open. DreamGUI fades the list in and out;
 * the fades are held to that.
 *
 * The fade's length is the behaviour's own and is not readable from outside it, so the waits for a fade to finish are a
 * second -- a stop, far past any fade a list takes -- and pass on the frame the fade does.
 */
namespace DreamDropdownMidGestureTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The bottom-right corner, clear of the dropdown and of its list. */
	const FVector2D FarFromTheDropdown(1200.0, 680.0);

	/** Longer than any list fade; see the file comment. */
	const double FadeStopSeconds = 1.0;

	struct FPlacedDropdown
	{
		UDreamDropdown* Dropdown = nullptr;
		TSharedPtr<FDreamDriverElement> Element;

		bool IsReady() const { return Dropdown != nullptr && Dropdown->ListNode != nullptr && Element.IsValid() && Element->Exists(); }
	};

	/** A dropdown high on the screen with three options, the first selected, its events going to InListener. */
	FPlacedDropdown PlaceDropdown(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedDropdown Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr, FVector2D(200.0, 40.0), FVector2D(0.0, 200.0));
		if (!InTest.TestNotNull(TEXT("A dropdown can be made on the rig"), Dropdown))
		{
			return Placed;
		}
		Dropdown->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")), FText::AsCultureInvariant(TEXT("High")) });
		Dropdown->SetSelectedIndex(0);
		Dropdown->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		Dropdown->OnOpening.AddDynamic(InListener, &UDreamPressInteractionListener::HandleOpening);
		Dropdown->OnClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClosed);
		Dropdown->OnItemGenerated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemGenerated);
		// Every click below is a click of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);

		Placed.Dropdown = Dropdown;
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Dropdown));
		InTest.TestTrue(TEXT("The dropdown has a list, and the driver can find it"), Placed.IsReady());
		return Placed;
	}

	float ListOpacity(const FPlacedDropdown& InPlaced)
	{
		return InPlaced.Dropdown->ListNode->GetRenderOpacity();
	}

	/** Pump until InCondition holds, for at most a second; see the file comment. */
	bool PumpUntil(FDreamDriverRig& InRig, TFunction<bool()> InCondition, const TCHAR* InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(FadeStopSeconds);
		return InRig.Driver()->Wait(FDreamUntil::Condition(MoveTemp(InCondition), Timeout), Timeout, InWhat);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownTapTest,
	"DreamGUI.Dropdown.ATapOnTheFaceOpensTheListAndATapOnAnOptionChoosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownTapTest, "DreamGUI.Dropdown.ATapOnTheFaceOpensTheListAndATapOnAnOptionChoosesIt", "[Touch][Animated]")

/*
 * SComboButton opens its menu from SButton's click, and a touch is a click of a button under the default touch method
 * (SButton.cpp:354, :410); a row of the open list is the same. So a tap on the face opens the list -- OnOpening, as for a
 * click -- and a tap on the second option chooses it and closes the list.
 */
bool FDreamDropdownTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Tapping the face completes"), Placed.Element->Tap());
	// Two frames: the first lays out the rows the open just built, the second what arranging them dirtied.
	Rig.PumpFrames(2);
	TestTrue(TEXT("A tap on the face opens the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying it is opening, once"), Listener->OpeningCount, 1);
	if (!TestTrue(TEXT("Opening built a row for the second option"), Listener->GeneratedItems.IsValidIndex(1) && Listener->GeneratedItems[1] != nullptr))
	{
		return false;
	}

	TestTrue(TEXT("Tapping the second option completes"), Rig.Driver()->Find(FDreamBy::Widget(Listener->GeneratedItems[1].Get()))->Tap());
	TestEqual(TEXT("The tapped option is the selection"), Placed.Dropdown->GetSelectedIndex(), 1);
	if (TestEqual(TEXT("...announced once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the second option"), Listener->SelectionIndices[0], 1);
	}
	TestFalse(TEXT("...and the list is closed"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying so once"), Listener->ClosedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownCloseDuringFadeInTest,
	"DreamGUI.Dropdown.AClickOutsideWhileTheListFadesInClosesItAndItsRowsAnswerNothingAfter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownCloseDuringFadeInTest, "DreamGUI.Dropdown.AClickOutsideWhileTheListFadesInClosesItAndItsRowsAnswerNothingAfter", "[Pointer][Animated]")

/*
 * Opened and, before the list has finished coming in, dismissed by a click outside it: the menu stack takes the menu down
 * on the press (MenuStack.cpp:667-676). The list is closed from that click on -- OnClosed once, no choice made -- and
 * whatever is left of it on screen while it fades is not a list: a click where its second row is chooses nothing and does
 * not bring it back. It fades from where its fade-in had got, never brighter, and is put away.
 */
bool FDreamDropdownCloseDuringFadeInTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Clicking the face completes"), Placed.Element->Click());
	TestTrue(TEXT("The click opened the list"), Placed.Dropdown->IsOpen());
	// The fade's first step is the frame after the open: a tween reads where it starts from on its first update
	// (UDreamTweener::BeginTween), and the open's own frame has had its tween step already (the input frame comes after
	// the DuringPhysics tweens). So the list is waited for until it is truly on its way in, not assumed to be.
	const bool bFadingIn = PumpUntil(Rig, [&Placed]() { const float Now = ListOpacity(Placed); return Now > 0.0f && Now < 1.0f; },
		TEXT("the list on its way in"));
	const float JustOpened = ListOpacity(Placed);
	if (!TestTrue(FString::Printf(TEXT("Once its fade has begun, the list is on its way in (opacity %.3f)"), JustOpened), bFadingIn && JustOpened > 0.0f && JustOpened < 1.0f)
		|| !TestTrue(TEXT("Opening built a row for the second option"), Listener->GeneratedItems.IsValidIndex(1) && Listener->GeneratedItems[1] != nullptr))
	{
		return false;
	}
	const TOptional<FVector2D> SecondRow = Rig.Driver()->Find(FDreamBy::Widget(Listener->GeneratedItems[1].Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The second row is on the viewport"), SecondRow.IsSet()))
	{
		return false;
	}

	float AtTheClose = 0.0f;
	TestTrue(TEXT("Clicking far from the list while it fades in completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(FarFromTheDropdown)
			.Press()
			.Then([&Placed, &AtTheClose](FDreamDriverContext&) { AtTheClose = ListOpacity(Placed); })
			.Release()
			.Perform());
	TestFalse(TEXT("The click outside closed the list"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying so once"), Listener->ClosedCount, 1);
	TestTrue(FString::Printf(TEXT("...while it was still coming in (opacity %.3f)"), AtTheClose), AtTheClose < 1.0f);

	TestTrue(TEXT("Clicking where the second row is completes"),
		Rig.Driver()->Sequence().MoveToPixel(SecondRow.GetValue()).Press().Release().Perform());
	TestEqual(TEXT("A row of a list that has been closed chooses nothing"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("...the selection is where it was"), Placed.Dropdown->GetSelectedIndex(), 0);
	TestFalse(TEXT("...and the list did not come back"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...nor say it was opening again"), Listener->OpeningCount, 1);

	float Brightest = ListOpacity(Placed);
	bool bBrightened = false;
	const bool bPutAway = PumpUntil(Rig, [&Placed, &Brightest, &bBrightened]()
	{
		const float Now = ListOpacity(Placed);
		bBrightened |= Now > Brightest + 0.001f;
		Brightest = FMath::Min(Brightest, Now);
		return !Placed.Dropdown->ListNode->GetWidgetActive();
	}, TEXT("the closed list being put away"));
	TestTrue(TEXT("The closed list is put away"), bPutAway);
	TestFalse(TEXT("...having only ever faded, never come back up"), bBrightened);
	TestTrue(FString::Printf(TEXT("...and is transparent when it goes (opacity %.3f)"), ListOpacity(Placed)), ListOpacity(Placed) <= 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownReopenDuringFadeOutTest,
	"DreamGUI.Dropdown.ReopeningWhileTheListFadesOutLeavesItOpenAndAnsweringClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownReopenDuringFadeOutTest, "DreamGUI.Dropdown.ReopeningWhileTheListFadesOutLeavesItOpenAndAnsweringClicks", "[Pointer][Animated]")

/*
 * Opened all the way, closed by a click outside, and opened again from the face before the list has faded out. An open is
 * an open (every push is a menu that is up, MenuStack.cpp:490-503): OnOpening again, the list comes back up to full and
 * stays -- the fade-out it interrupted does not finish later and put it away -- and a click on a row chooses that row.
 */
bool FDreamDropdownReopenDuringFadeOutTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDropdown Placed = PlaceDropdown(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Clicking the face completes"), Placed.Element->Click());
	TestTrue(TEXT("The list comes all the way in"), PumpUntil(Rig, [&Placed]() { return ListOpacity(Placed) >= 1.0f; }, TEXT("the list fully in")));
	TestTrue(TEXT("Clicking far from the list completes"),
		Rig.Driver()->Sequence().MoveToPixel(FarFromTheDropdown).Press().Release().Perform());
	TestFalse(TEXT("The click outside closed the list"), Placed.Dropdown->IsOpen());
	const float Leaving = ListOpacity(Placed);
	if (!TestTrue(FString::Printf(TEXT("...which is on its way out (opacity %.3f)"), Leaving), Leaving > 0.0f && Leaving < 1.0f)
		|| !TestTrue(TEXT("...and still on screen"), Placed.Dropdown->ListNode->GetWidgetActive()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the face again while the list fades out completes"), Placed.Element->Click());
	TestTrue(TEXT("The click opened the list again"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying it is opening, a second time"), Listener->OpeningCount, 2);

	float Dimmest = ListOpacity(Placed);
	bool bDimmed = false;
	TestTrue(TEXT("The list comes back up to full"), PumpUntil(Rig, [&Placed, &Dimmest, &bDimmed]()
	{
		const float Now = ListOpacity(Placed);
		bDimmed |= Now < Dimmest - 0.001f;
		Dimmest = FMath::Max(Dimmest, Now);
		return Now >= 1.0f;
	}, TEXT("the reopened list fully in")));
	TestFalse(TEXT("...without fading any further out on the way"), bDimmed);
	// Past where the interrupted fade-out would have ended and put the list away.
	TestTrue(TEXT("Waiting a while completes"), Rig.Driver()->Sequence().WaitSeconds(static_cast<float>(FadeStopSeconds)).Perform());
	TestTrue(TEXT("The reopened list is still open"), Placed.Dropdown->IsOpen());
	TestTrue(TEXT("...still on screen"), Placed.Dropdown->ListNode->GetWidgetActive());
	TestEqual(TEXT("...and nothing announced another close"), Listener->ClosedCount, 1);

	if (!TestTrue(TEXT("The reopened list has a row for the third option"), Listener->GeneratedItems.IsValidIndex(2) && Listener->GeneratedItems[2] != nullptr))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the third option completes"), Rig.Driver()->Find(FDreamBy::Widget(Listener->GeneratedItems[2].Get()))->Click());
	TestEqual(TEXT("A row of the reopened list answers the click"), Placed.Dropdown->GetSelectedIndex(), 2);
	TestEqual(TEXT("...announced once"), Listener->SelectionIndices.Num(), 1);
	return true;
}

#endif
