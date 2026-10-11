// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamDropdown.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UIDropdown.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamDropdown disabled, and on a canvas scaled to half and to double, held to UMG's UComboBoxString.
 *
 * The combo box is an SComboButton over an SMenuAnchor. Disabled, its button is off the hit path (FHittestGrid::
 * GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) and passed over by navigation (FindFocusableWidget,
 * HittestGrid.cpp:471), so neither a finger nor the pad's Accept can open it. Its menu goes up below the button and as wide
 * as it ("Place the menu immediately below the anchor and match its width to the anchor's content", MenuPlacement_ComboBox,
 * SlateCore/Public/Types/SlateEnums.h:224), on the button's own DPI-scaled geometry, so on a scaled layer the list opens
 * under the face as the face is drawn and its rows are hit where they are drawn.
 */
namespace DreamDropdownConfigTestLocal
{
	const FVector2D DropdownSize(200.0, 40.0);

	struct FPlacedDropdown
	{
		UDreamDropdown* Dropdown = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;

		bool IsReady() const { return Dropdown != nullptr && Dropdown->FaceNode != nullptr && Dropdown->ListNode != nullptr; }
	};

	/** A dropdown at InPosition (canvas units, Y up) holding Low, Medium and High with Low chosen, its events heard. */
	FPlacedDropdown PlaceDropdown(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition)
	{
		FPlacedDropdown Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(InName, nullptr, DropdownSize, InPosition);
		if (Dropdown == nullptr)
		{
			return Placed;
		}
		Dropdown->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")), FText::AsCultureInvariant(TEXT("High")) });
		Dropdown->SetSelectedIndex(0);
		Dropdown->OnSelectionChanged.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleSelectionChanged);
		Dropdown->OnOpening.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleOpening);
		Dropdown->OnItemGenerated.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleItemGenerated);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(1);
		Placed.Dropdown = Dropdown;
		return Placed;
	}

	/** Pumped until the open list has faded all the way in, for at most a second: past any fade a list takes. */
	bool WaitForListShown(FDreamDriverRig& InRig, const UDreamDropdown* InDropdown)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(1.0);
		return InRig.Driver()->Wait(FDreamUntil::Condition([InDropdown]()
		{
			return InDropdown->IsOpen() && InDropdown->ListNode != nullptr && InDropdown->ListNode->GetRenderOpacity() > 0.999f;
		}, Timeout), Timeout, TEXT("the dropdown's list open and faded in"));
	}

	/**
	 * Whether the open list is drawn the way a combo box's menu is: its top on the face's bottom edge, its centre under the
	 * face's, and as wide as the face, each to within a pixel and a half. OutWhere says where both were.
	 */
	bool ListHangsUnderFace(const UDreamDropdown* InDropdown, FString& OutWhere)
	{
		const TOptional<FBox2D> Face = DreamControlsConfigTest::DrawnRect(InDropdown->FaceNode.Get());
		const TOptional<FBox2D> List = DreamControlsConfigTest::DrawnRect(InDropdown->ListNode.Get());
		if (!Face.IsSet() || !List.IsSet())
		{
			OutWhere = TEXT("the face or the list has no pixels");
			return false;
		}
		OutWhere = FString::Printf(TEXT("the face at %s, the list at %s"), *DreamControlsConfigTest::Describe(Face.GetValue()),
			*DreamControlsConfigTest::Describe(List.GetValue()));
		return FMath::Abs(List->Min.Y - Face->Max.Y) <= 1.5
			&& FMath::Abs(List->GetCenter().X - Face->GetCenter().X) <= 1.5
			&& FMath::Abs(List->GetSize().X - Face->GetSize().X) <= 1.5;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownDisabledTapTest,
	"DreamGUI.Dropdown.ATapOnADisabledDropdownDoesNotOpenItUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownDisabledTapTest, "DreamGUI.Dropdown.ATapOnADisabledDropdownDoesNotOpenItUntilItIsEnabledAgain", "[Touch][Disabled]")

/*
 * A finger's tap on a disabled dropdown reaches nothing: the list stays closed and OnOpening never fires. Enabled again, the
 * same tap opens it.
 */
bool FDreamDropdownDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedDropdown Placed = PlaceDropdown(Rig, TEXT("Quality"), FVector2D(0.0, 200.0));
	if (!TestTrue(TEXT("A dropdown with a face and a list can be made"), Placed.IsReady()))
	{
		return false;
	}
	Placed.Dropdown->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef Face = Rig.Driver()->Find(FDreamBy::Widget(Placed.Dropdown));

	TestTrue(TEXT("Tapping the disabled dropdown completes"), Face->Tap());
	Rig.PumpFrames(2);
	TestFalse(TEXT("A disabled dropdown stays closed under a tap"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...and never began opening"), Placed.Listener->OpeningCount, 0);

	Placed.Dropdown->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping it enabled again completes"), Face->Tap());
	Rig.PumpFrames(2);
	TestTrue(TEXT("Enabled again, a tap opens it"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("...saying it is opening, once"), Placed.Listener->OpeningCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownDisabledPadTest,
	"DreamGUI.Dropdown.TheDPadPassesOverADisabledDropdownAndAcceptCannotOpenItUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownDisabledPadTest, "DreamGUI.Dropdown.TheDPadPassesOverADisabledDropdownAndAcceptCannotOpenItUntilItIsEnabledAgain", "[Nav][Disabled]")

/*
 * Three dropdowns in a row, the middle one disabled, the focus on the first. D-pad right passes over the middle one to the
 * third, and the pad's Accept opens the third -- never the disabled one. The pad's Back closes it, and D-pad left goes back past the
 * middle one. Enabled again, the middle dropdown is the next stop and Accept opens it.
 */
bool FDreamDropdownDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedDropdown First = PlaceDropdown(Rig, TEXT("Texture"), FVector2D(-260.0, 200.0));
	const FPlacedDropdown Middle = PlaceDropdown(Rig, TEXT("Shadow"), FVector2D(0.0, 200.0));
	const FPlacedDropdown Last = PlaceDropdown(Rig, TEXT("Effects"), FVector2D(260.0, 200.0));
	if (!TestTrue(TEXT("Three dropdowns can be made"), First.IsReady() && Middle.IsReady() && Last.IsReady()))
	{
		return false;
	}
	Middle.Dropdown->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the first dropdown"), PutFocusOn(Rig, First.Dropdown->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("D-pad right passes over the disabled dropdown to the third"), FocusIsIn(Rig, Last.Dropdown));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("Accept opened the dropdown the focus is on"), Last.Dropdown->IsOpen());
	TestFalse(TEXT("...and not the disabled one"), Middle.Dropdown->IsOpen());
	TestEqual(TEXT("The disabled dropdown never began opening"), Middle.Listener->OpeningCount, 0);
	TestTrue(TEXT("The pad's Back completes"), PressKey(Rig, EKeys::Gamepad_FaceButton_Right));
	TestFalse(TEXT("Back closed the third dropdown's list"), Last.Dropdown->IsOpen());
	TestTrue(TEXT("...the focus back on its face"), FocusIsIn(Rig, Last.Dropdown));
	TestTrue(TEXT("D-pad left completes"), PressKey(Rig, EKeys::Gamepad_DPad_Left));
	TestTrue(TEXT("D-pad left passes back over the disabled dropdown"), FocusIsIn(Rig, First.Dropdown));

	Middle.Dropdown->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("D-pad right completes once the middle dropdown is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("Enabled again, the middle dropdown is the next stop"), FocusIsIn(Rig, Middle.Dropdown));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("...and opens it"), Middle.Dropdown->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownScaledHitTest,
	"DreamGUI.Dropdown.OnACanvasScaledToHalfAndToDoubleTheListOpensUnderTheFaceAsDrawnAndAClickOrATapChoosesTheRowDrawnThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownScaledHitTest, "DreamGUI.Dropdown.OnACanvasScaledToHalfAndToDoubleTheListOpensUnderTheFaceAsDrawnAndAClickOrATapChoosesTheRowDrawnThere", "[Pointer][Touch][Scaled]")

/*
 * A dropdown off the middle, high on the screen, at each scale. A click three pixels inside the face's bottom-right corner
 * opens the list; the list hangs from the face's drawn bottom edge, centred under it and as wide as it is drawn; and a click
 * three pixels inside the bottom of the second row as drawn chooses Medium. Then a tap three pixels inside the face's
 * top-left corner opens it again, and a tap three pixels inside the top of the third row chooses High.
 */
bool FDreamDropdownScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedDropdown Placed = PlaceDropdown(Rig, TEXT("Quality"), FVector2D(-150.0, 120.0));
		if (!TestTrue(At(Scale, TEXT("A dropdown with a face and a list can be made")), Placed.IsReady()))
		{
			return false;
		}
		UDreamDropdown* Dropdown = Placed.Dropdown;
		UDreamPressInteractionListener* Listener = Placed.Listener.Get();
		const TOptional<FBox2D> Face = DrawnRect(Dropdown->FaceNode.Get());
		if (!TestTrue(At(Scale, TEXT("The face is on the viewport")), Face.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The face is drawn at the canvas's scale (%s)"), *Describe(Face.GetValue()))),
			IsDrawnAtScale(Face.GetValue(), Dropdown->FaceNode->GetSize(), Scale));

		TestTrue(At(Scale, TEXT("A click just inside the face's bottom-right corner completes")), ClickAt(Rig, InsideBottomRight(Face.GetValue(), 3.0)));
		if (!TestTrue(At(Scale, TEXT("It opens the list")), WaitForListShown(Rig, Dropdown)))
		{
			return false;
		}
		FString Where;
		TestTrue(At(Scale, TEXT("The list hangs from the face's drawn bottom edge, centred and as wide as the face")), ListHangsUnderFace(Dropdown, Where));
		AddInfo(At(Scale, Where));
		const TOptional<FBox2D> SecondRow = Listener->GeneratedItems.IsValidIndex(1) ? DrawnRect(Listener->GeneratedItems[1].Get()) : TOptional<FBox2D>();
		if (!TestTrue(At(Scale, TEXT("The second row is on the viewport")), SecondRow.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("A click just inside the bottom of the second row completes")),
			ClickAt(Rig, FVector2D(SecondRow->GetCenter().X, SecondRow->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the second option")), Dropdown->GetSelectedIndex(), 1);
		TestFalse(At(Scale, TEXT("...and closed the list")), Dropdown->IsOpen());
		Rig.PumpFrames(2);

		TestTrue(At(Scale, TEXT("A tap just inside the face's top-left corner completes")), TapAt(Rig, InsideTopLeft(Face.GetValue(), 3.0)));
		if (!TestTrue(At(Scale, TEXT("It opens the list again")), WaitForListShown(Rig, Dropdown)))
		{
			return false;
		}
		const TOptional<FBox2D> ThirdRow = Listener->GeneratedItems.IsValidIndex(2) ? DrawnRect(Listener->GeneratedItems[2].Get()) : TOptional<FBox2D>();
		if (!TestTrue(At(Scale, TEXT("The third row is on the viewport")), ThirdRow.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("A tap just inside the top of the third row completes")),
			TapAt(Rig, FVector2D(ThirdRow->GetCenter().X, ThirdRow->Min.Y + 3.0)));
		TestEqual(*At(Scale, TEXT("It chose the third option")), Dropdown->GetSelectedIndex(), 2);
		if (TestEqual(*At(Scale, TEXT("Two choices were announced")), Listener->SelectionIndices.Num(), 2))
		{
			TestEqual(*At(Scale, TEXT("...Medium first")), Listener->SelectionIndices[0], 1);
			TestEqual(*At(Scale, TEXT("...then High")), Listener->SelectionIndices[1], 2);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownScaledPadTest,
	"DreamGUI.Dropdown.OnACanvasScaledToHalfAndToDoubleThePadOpensTheListUnderTheFaceAsDrawnAndChoosesFromIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownScaledPadTest, "DreamGUI.Dropdown.OnACanvasScaledToHalfAndToDoubleThePadOpensTheListUnderTheFaceAsDrawnAndChoosesFromIt", "[Nav][Scaled]")

/*
 * Two dropdowns side by side, the focus on the first. D-pad right takes it to the second, whose face the ring then sits on
 * where it is drawn; the pad's Accept opens its list under the face as drawn, with the focus on the chosen row (SComboBox's
 * list takes the focus when it opens); D-pad down moves to the second row; Accept chooses Medium and closes the list.
 */
bool FDreamDropdownScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedDropdown Left = PlaceDropdown(Rig, TEXT("Texture"), FVector2D(-130.0, 120.0));
		const FPlacedDropdown Right = PlaceDropdown(Rig, TEXT("Shadow"), FVector2D(130.0, 120.0));
		if (!TestTrue(At(Scale, TEXT("Two dropdowns can be made")), Left.IsReady() && Right.IsReady())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the first dropdown")), PutFocusOn(Rig, Left.Dropdown->FaceNode.Get())))
		{
			return false;
		}
		UDreamDropdown* Dropdown = Right.Dropdown;

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestTrue(At(Scale, TEXT("The D-pad took the focus to the second dropdown")), FocusIsIn(Rig, Dropdown));
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the second dropdown where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		if (!TestTrue(At(Scale, TEXT("Accept opened the list")), WaitForListShown(Rig, Dropdown)))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("The list hangs from the face's drawn bottom edge, centred and as wide as the face")), ListHangsUnderFace(Dropdown, Where));
		AddInfo(At(Scale, Where));
		const TArray<TObjectPtr<UDreamWidget>>& Rows = Right.Listener->GeneratedItems;
		TestTrue(At(Scale, TEXT("The list opened with the focus on the chosen row")), Rows.IsValidIndex(0) && FocusIsIn(Rig, Rows[0].Get()));

		TestTrue(At(Scale, TEXT("D-pad down completes")), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		TestTrue(At(Scale, TEXT("The D-pad moved the focus to the second row")), Rows.IsValidIndex(1) && FocusIsIn(Rig, Rows[1].Get()));
		TestTrue(At(Scale, TEXT("...and the list is still open")), Dropdown->IsOpen());
		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestEqual(*At(Scale, TEXT("Accept chose the second option")), Dropdown->GetSelectedIndex(), 1);
		TestFalse(At(Scale, TEXT("...and closed the list")), Dropdown->IsOpen());
		TestEqual(*At(Scale, TEXT("The first dropdown never opened")), Left.Listener->OpeningCount, 0);
	}
	return true;
}

#endif
