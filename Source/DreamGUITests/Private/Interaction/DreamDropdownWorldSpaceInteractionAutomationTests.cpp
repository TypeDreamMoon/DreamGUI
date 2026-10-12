// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * UDreamDropdown on a panel in the level, and on a canvas drawn into a texture that a mesh in the level shows.
 *
 * UMG's combo box is an SComboButton over an SMenuAnchor, and in a UWidgetComponent its menu stays in the component's own
 * window: the component's virtual window asks for popups to go up in the current window (SVirtualWindow::OnQueryPopupMethod
 * answers UseCurrentWindow, Slate/Private/Widgets/SVirtualWindow.cpp:35-38) and the menu stack hosts them in that window's
 * popup layer (FMenuStack::SetHostPath, Slate/Private/Framework/Application/MenuStack.cpp:680-712). So the list is drawn on
 * the panel, below the button and as wide as it ("Place the menu immediately below the anchor and match its width to the
 * anchor's content", MenuPlacement_ComboBox, SlateCore/Public/Types/SlateEnums.h:224), above what else the panel shows, and
 * the interaction component's ray reaches its rows where the camera sees them. A row chosen closes it; a press anywhere the
 * list is not -- elsewhere on the panel, on another panel -- closes it choosing nothing, through the menu stack, and goes
 * on to what it landed on (DreamGUI's default, bMenusConsumeOutsideClick off). The pad opens it with Accept, walks its rows
 * and chooses with Accept, and Back closes it with the focus back on the button.
 *
 * DreamGUI's list is the dropdown's own widget, lifted by UDreamUIPopupLayer onto the top of the tree the dropdown is in --
 * here the panel's root, as a widget component's window -- and its rows are hit and focused there.
 */
namespace DreamDropdownWorldTestLocal
{
	const FVector2D DropdownSize(200.0, 40.0);

	struct FPlacedDropdown
	{
		UDreamDropdown* Dropdown = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;

		bool IsReady() const { return Dropdown != nullptr && Dropdown->FaceNode != nullptr && Dropdown->ListNode != nullptr; }

		UDreamWidget* Row(int32 InIndex) const
		{
			return Listener.IsValid() && Listener->GeneratedItems.IsValidIndex(InIndex) ? Listener->GeneratedItems[InIndex].Get() : nullptr;
		}
	};

	/** A dropdown under InParent at InPosition (its units, Y up) holding Low, Medium and High with Low chosen, its events heard. */
	FPlacedDropdown PlaceDropdown(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		FPlacedDropdown Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(InName, InParent, DropdownSize, InPosition);
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
		InRig.PumpFrames(2);
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

	/** Pumped until the closed list has faded out and gone home under its dropdown, for at most a second. */
	bool WaitForListPutAway(FDreamDriverRig& InRig, const UDreamDropdown* InDropdown, const UDreamWidget* InLiftedOnto)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(1.0);
		return InRig.Driver()->Wait(FDreamUntil::Condition([InDropdown, InLiftedOnto]()
		{
			return !InDropdown->IsOpen() && InDropdown->ListNode != nullptr && InDropdown->ListNode->GetParent() != InLiftedOnto;
		}, Timeout), Timeout, TEXT("the dropdown's list closed and put away"));
	}

	/**
	 * Whether the open list is up on the tree InRoot heads -- its child, drawn by its root canvas, in a canvas sorted above
	 * the face's -- and not on the player's screen. OutWhere says what it is on.
	 */
	bool ListIsUpOn(const UDreamDropdown* InDropdown, const UDreamWidget* InRoot, FString& OutWhere)
	{
		const UDreamWidget* List = InDropdown->ListNode.Get();
		const UDreamCanvas* ListCanvas = List != nullptr ? List->GetRenderCanvas() : nullptr;
		const UDreamCanvas* FaceCanvas = InDropdown->FaceNode->GetRenderCanvas();
		if (ListCanvas == nullptr || FaceCanvas == nullptr)
		{
			OutWhere = TEXT("the list or the face has no canvas");
			return false;
		}
		OutWhere = FString::Printf(TEXT("the list under '%s', drawn by the root canvas of '%s' at sort order %d; the face at %d"),
			List->GetParent() != nullptr ? *List->GetParent()->GetDisplayName() : TEXT("nothing"),
			ListCanvas->GetRootCanvas() != nullptr && ListCanvas->GetRootCanvas()->GetWidget() != nullptr ? *ListCanvas->GetRootCanvas()->GetWidget()->GetDisplayName() : TEXT("nothing"),
			ListCanvas->GetActualSortOrder(), FaceCanvas->GetActualSortOrder());
		return List->GetParent() == InRoot
			&& List->GetRootCanvas() == InRoot->GetRootCanvas()
			&& !List->IsScreenSpaceOverlayUI()
			&& ListCanvas->GetActualSortOrder() > FaceCanvas->GetActualSortOrder();
	}

	/**
	 * Whether the open list is seen the way a combo box's menu is drawn: its top on the face's bottom edge, its centre under
	 * the face's, and as wide as the face, each to within a pixel and a half, as the camera sees both. OutWhere says where.
	 */
	bool ListHangsUnderFaceAsSeen(const FDreamDriverRig& InRig, const UDreamDropdown* InDropdown, FString& OutWhere)
	{
		const TOptional<FBox2D> Face = DreamControlsWorldTest::SeenRect(InRig, InDropdown->FaceNode.Get());
		const TOptional<FBox2D> List = DreamControlsWorldTest::SeenRect(InRig, InDropdown->ListNode.Get());
		if (!Face.IsSet() || !List.IsSet())
		{
			OutWhere = TEXT("the camera sees no face or no list");
			return false;
		}
		OutWhere = FString::Printf(TEXT("the face seen at %s, the list at %s"), *DreamControlsConfigTest::Describe(Face.GetValue()),
			*DreamControlsConfigTest::Describe(List.GetValue()));
		return FMath::Abs(List->Min.Y - Face->Max.Y) <= 1.5
			&& FMath::Abs(List->GetCenter().X - Face->GetCenter().X) <= 1.5
			&& FMath::Abs(List->GetSize().X - Face->GetSize().X) <= 1.5;
	}

	/** A pixel on InRect's horizontal middle line, InInset pixels in from its right edge: clear of a rounded face's corners. */
	FVector2D InsideRightMiddle(const FBox2D& InRect, double InInset)
	{
		return FVector2D(InRect.Max.X - InInset, InRect.GetCenter().Y);
	}

	/** The list open and up on InRoot, hanging under the face as seen; reported against InTest, and false when it is not. */
	bool CheckOpenListOn(FAutomationTestBase& InTest, const FDreamDriverRig& InRig, const UDreamDropdown* InDropdown, const UDreamWidget* InRoot,
		const FString& InWhen)
	{
		FString Where;
		const bool bUp = ListIsUpOn(InDropdown, InRoot, Where);
		InTest.TestTrue(FString::Printf(TEXT("%s: the list is up on the tree the dropdown is in, above its face, not on the screen (%s)"), *InWhen, *Where), bUp);
		const bool bHangs = ListHangsUnderFaceAsSeen(InRig, InDropdown, Where);
		InTest.TestTrue(FString::Printf(TEXT("%s: the camera sees the list hang from the face's bottom edge, centred and as wide as the face (%s)"), *InWhen, *Where), bHangs);
		return bUp && bHangs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownWorldPressTest,
	"DreamGUI.Dropdown.OnAWorldPanelTheListOpensOnThePanelUnderTheFaceAndAClickOrATapChoosesTheRowSeenThereOrClosesItFromOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownWorldPressTest, "DreamGUI.Dropdown.OnAWorldPanelTheListOpensOnThePanelUnderTheFaceAndAClickOrATapChoosesTheRowSeenThereOrClosesItFromOutside", "[Pointer][Touch][World]")

/*
 * A dropdown on a panel three metres ahead, a second panel to its right with a button on it. A click just inside the face's
 * right edge opens the list: it is up on the panel, above the face, and the camera sees it hang from the face. A click just
 * inside the bottom of the second row as seen chooses Medium and closes it. A tap on the face opens it again, and a tap just
 * inside the top of the third row chooses High. Opened by a click again, a click on the bare panel below the list closes
 * it choosing nothing; opened by a tap, a tap on the other panel's button closes it choosing nothing, and goes on to click
 * that button.
 */
bool FDreamDropdownWorldPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownWorldTestLocal;
	using namespace DreamControlsConfigTest;
	TStrongObjectPtr<UDreamPressInteractionListener> OtherClicks(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The world pointer first, then two panels side by side three metres ahead: the dropdown's, left of the middle, and a
	// small one to its right.
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig,
		DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, DreamControlsWorldTest::ViewportSize()), EDreamWorldPointerSource::Mouse);
	UDreamWidget* Panel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Panel"),
		FTransform(FVector(DreamControlsWorldTest::PanelDistance, -100.0, 0.0)), FVector2D(360.0, 320.0));
	UDreamWidget* OtherPanel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("OtherPanel"),
		FTransform(FVector(DreamControlsWorldTest::PanelDistance, 190.0, 0.0)), FVector2D(160.0, 120.0));
	if (!TestTrue(TEXT("A world pointer and two panels in front of it"), Pointer != nullptr && Panel != nullptr && OtherPanel != nullptr))
	{
		return false;
	}
	UDreamButton* OtherButton = Rig.MakeControl<UDreamButton>(TEXT("OtherButton"), OtherPanel, FVector2D(120.0, 50.0), FVector2D::ZeroVector);
	if (!TestTrue(TEXT("A button on the other panel"), OtherButton != nullptr && OtherButton->FaceNode != nullptr))
	{
		return false;
	}
	OtherButton->OnClicked.AddDynamic(OtherClicks.Get(), &UDreamPressInteractionListener::HandleClicked);
	const FPlacedDropdown Placed = PlaceDropdown(Rig, TEXT("Quality"), Panel, FVector2D(-40.0, 100.0));
	if (!TestTrue(TEXT("A dropdown with a face and a list can be made on the panel"), Placed.IsReady()))
	{
		return false;
	}
	UDreamDropdown* Dropdown = Placed.Dropdown;
	const TOptional<FBox2D> Face = DreamControlsWorldTest::SeenRect(Rig, Dropdown->FaceNode.Get());
	const TOptional<FBox2D> Other = DreamControlsWorldTest::SeenRect(Rig, OtherButton->FaceNode.Get());
	if (!TestTrue(TEXT("The camera sees the face and the other panel's button"), Face.IsSet() && Other.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The face is seen at its size in panel units (%s)"), *Describe(Face.GetValue())),
		DreamControlsWorldTest::IsSeenAtItsSize(Face.GetValue(), Dropdown->FaceNode->GetSize()));

	// A click opens it, and a click on a row seen there chooses it.
	TestTrue(TEXT("A click just inside the face's right edge completes"), ClickAt(Rig, InsideRightMiddle(Face.GetValue(), 3.0)));
	if (!TestTrue(TEXT("It opens the list"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	CheckOpenListOn(*this, Rig, Dropdown, Panel, TEXT("Opened by a click"));
	const TOptional<FBox2D> SecondRow = DreamControlsWorldTest::SeenRect(Rig, Placed.Row(1));
	if (!TestTrue(TEXT("The camera sees the second row"), SecondRow.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click just inside the bottom of the second row as seen completes"),
		ClickAt(Rig, FVector2D(SecondRow->GetCenter().X, SecondRow->Max.Y - 3.0)));
	TestEqual(TEXT("It chose the second option"), Dropdown->GetSelectedIndex(), 1);
	TestFalse(TEXT("...and closed the list"), Dropdown->IsOpen());
	TestTrue(TEXT("The list fades out and goes home"), WaitForListPutAway(Rig, Dropdown, Panel));

	// A tap opens it, and a tap on a row seen there chooses it.
	TestTrue(TEXT("A tap on the face completes"), TapAt(Rig, Face->GetCenter()));
	if (!TestTrue(TEXT("It opens the list again"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	CheckOpenListOn(*this, Rig, Dropdown, Panel, TEXT("Opened by a tap"));
	const TOptional<FBox2D> ThirdRow = DreamControlsWorldTest::SeenRect(Rig, Placed.Row(2));
	if (!TestTrue(TEXT("The camera sees the third row"), ThirdRow.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A tap just inside the top of the third row as seen completes"),
		TapAt(Rig, FVector2D(ThirdRow->GetCenter().X, ThirdRow->Min.Y + 3.0)));
	TestEqual(TEXT("It chose the third option"), Dropdown->GetSelectedIndex(), 2);
	TestFalse(TEXT("...and closed the list"), Dropdown->IsOpen());
	TestTrue(TEXT("The list fades out and goes home"), WaitForListPutAway(Rig, Dropdown, Panel));

	// A click on the bare panel below the open list closes it.
	TestTrue(TEXT("A click on the face completes"), ClickAt(Rig, Face->GetCenter()));
	if (!TestTrue(TEXT("It opens the list a third time"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	const TOptional<FBox2D> List = DreamControlsWorldTest::SeenRect(Rig, Dropdown->ListNode.Get());
	if (!TestTrue(TEXT("The camera sees the open list"), List.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click on the bare panel twenty pixels below the list completes"), ClickAt(Rig, PastBottomEdge(List.GetValue(), 20.0)));
	TestFalse(TEXT("It closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("...choosing nothing"), Dropdown->GetSelectedIndex(), 2);
	TestTrue(TEXT("The list fades out and goes home"), WaitForListPutAway(Rig, Dropdown, Panel));

	// A tap on the other panel's button closes it, and goes on to the button.
	TestTrue(TEXT("A tap on the face completes"), TapAt(Rig, Face->GetCenter()));
	if (!TestTrue(TEXT("It opens the list a fourth time"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	TestTrue(TEXT("A tap on the other panel's button completes"), TapAt(Rig, Other->GetCenter()));
	TestFalse(TEXT("The tap on another panel closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("...choosing nothing"), Dropdown->GetSelectedIndex(), 2);
	TestEqual(TEXT("...and went on to click the button it landed on"), OtherClicks->ClickedCount, 1);
	if (TestEqual(TEXT("Two choices were announced in all"), Placed.Listener->SelectionIndices.Num(), 2))
	{
		TestEqual(TEXT("...Medium first"), Placed.Listener->SelectionIndices[0], 1);
		TestEqual(TEXT("...then High"), Placed.Listener->SelectionIndices[1], 2);
	}
	TestEqual(TEXT("The list opened four times"), Placed.Listener->OpeningCount, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownWorldPadTest,
	"DreamGUI.Dropdown.OnAWorldPanelThePadOpensTheListOnThePanelUnderTheFaceWalksItsRowsWithTheRingOnThemAsSeenAndChoosesFromIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownWorldPadTest, "DreamGUI.Dropdown.OnAWorldPanelThePadOpensTheListOnThePanelUnderTheFaceWalksItsRowsWithTheRingOnThemAsSeenAndChoosesFromIt", "[Nav][World]")

/*
 * Two dropdowns side by side on a panel, the focus on the first. D-pad right takes it to the second, with the ring on it as
 * the camera sees it. The pad's Accept opens its list up on the panel, hanging from the face as seen, with the focus on the
 * chosen row (SComboBox's list takes the focus when it opens); D-pad down moves to the second row, the ring on that row
 * where the camera sees it on the list; Accept chooses Medium, closes the list and gives the focus back to the face.
 * Accept opens it again and Back closes it, choosing nothing, the focus back on the face.
 */
bool FDreamDropdownWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownWorldTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamControlsWorldTest::FWorldStage Stage = DreamControlsWorldTest::SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FPlacedDropdown Left = PlaceDropdown(Rig, TEXT("Texture"), Stage.Panel, FVector2D(-130.0, 100.0));
	const FPlacedDropdown Right = PlaceDropdown(Rig, TEXT("Shadow"), Stage.Panel, FVector2D(130.0, 100.0));
	if (!TestTrue(TEXT("Two dropdowns can be made on the panel"), Left.IsReady() && Right.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the first dropdown"), PutFocusOn(Rig, Left.Dropdown->FaceNode.Get())))
	{
		return false;
	}
	UDreamDropdown* Dropdown = Right.Dropdown;

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad took the focus to the second dropdown"), FocusIsIn(Rig, Dropdown));
	FString Where;
	bool bRingSits = DreamControlsWorldTest::WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second dropdown where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	if (!TestTrue(TEXT("Accept opened the list"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	CheckOpenListOn(*this, Rig, Dropdown, Stage.Panel, TEXT("Opened by the pad"));
	TestTrue(TEXT("The list opened with the focus on the chosen row"), Right.Row(0) != nullptr && FocusIsIn(Rig, Right.Row(0)));

	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("The D-pad moved the focus to the second row"), Right.Row(1) != nullptr && FocusIsIn(Rig, Right.Row(1)));
	TestTrue(TEXT("...and the list is still open"), Dropdown->IsOpen());
	bRingSits = DreamControlsWorldTest::WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second row where the camera sees it on the list (%s)"), *Where), bRingSits);
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept chose the second option"), Dropdown->GetSelectedIndex(), 1);
	TestFalse(TEXT("...and closed the list"), Dropdown->IsOpen());
	TestTrue(TEXT("...with the focus back on the dropdown"), FocusIsIn(Rig, Dropdown));
	TestTrue(TEXT("The list fades out and goes home"), WaitForListPutAway(Rig, Dropdown, Stage.Panel));

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	if (!TestTrue(TEXT("Accept opened the list again"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	TestTrue(TEXT("The pad's Back completes"), PressKey(Rig, EKeys::Gamepad_FaceButton_Right));
	TestFalse(TEXT("Back closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("...choosing nothing"), Dropdown->GetSelectedIndex(), 1);
	TestTrue(TEXT("...with the focus back on the dropdown"), FocusIsIn(Rig, Dropdown));
	TestEqual(TEXT("One choice was announced"), Right.Listener->SelectionIndices.Num(), 1);
	TestEqual(TEXT("The first dropdown never opened"), Left.Listener->OpeningCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropdownSurfacePressTest,
	"DreamGUI.Dropdown.ShownOnARenderTargetSurfaceTheListOpensOnTheSurfaceUnderTheFaceAndAClickChoosesTheRowSeenThereOrClosesItFromOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDropdownSurfacePressTest, "DreamGUI.Dropdown.ShownOnARenderTargetSurfaceTheListOpensOnTheSurfaceUnderTheFaceAndAClickChoosesTheRowSeenThereOrClosesItFromOutside", "[Pointer][World]")

/*
 * The same dropdown on a canvas drawn into a texture that a plane three metres ahead shows -- a widget component drawn into
 * its render target, whose menus stay in its window as on a panel. A click on the face as the camera sees it opens the list:
 * it is up on the surface's canvas, above the face, and seen hanging from it. A click inside the second row as seen chooses
 * Medium and closes it; opened again, a click on the bare surface below the list closes it choosing nothing.
 */
bool FDreamDropdownSurfacePressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDropdownWorldTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamTextNavInfraWorldTest::FSurfaceStage Stage = DreamTextNavInfraWorldTest::SetUpSurface(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamWidget* CanvasRoot = Stage.Screen.CanvasRoot;
	const FPlacedDropdown Placed = PlaceDropdown(Rig, TEXT("Quality"), CanvasRoot, FVector2D(0.0, 90.0));
	if (!TestTrue(TEXT("A dropdown with a face and a list can be made on the surface's canvas"), Placed.IsReady()))
	{
		return false;
	}
	UDreamDropdown* Dropdown = Placed.Dropdown;
	const TOptional<FBox2D> Face = DreamControlsWorldTest::SeenRect(Rig, Dropdown->FaceNode.Get());
	if (!TestTrue(TEXT("The camera sees the face on the surface"), Face.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("A click on the face completes"), ClickAt(Rig, Face->GetCenter()));
	if (!TestTrue(TEXT("It opens the list"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	CheckOpenListOn(*this, Rig, Dropdown, CanvasRoot, TEXT("On the surface"));
	TestTrue(TEXT("...drawn into the surface's texture"), Dropdown->ListNode->IsRenderTargetUI() && Dropdown->ListNode->GetRootCanvas() == Stage.Screen.Canvas);
	const TOptional<FBox2D> SecondRow = DreamControlsWorldTest::SeenRect(Rig, Placed.Row(1));
	if (!TestTrue(TEXT("The camera sees the second row"), SecondRow.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click just inside the bottom of the second row as seen completes"),
		ClickAt(Rig, FVector2D(SecondRow->GetCenter().X, SecondRow->Max.Y - 3.0)));
	TestEqual(TEXT("It chose the second option"), Dropdown->GetSelectedIndex(), 1);
	TestFalse(TEXT("...and closed the list"), Dropdown->IsOpen());
	TestTrue(TEXT("The list fades out and goes home"), WaitForListPutAway(Rig, Dropdown, CanvasRoot));

	TestTrue(TEXT("A click on the face completes"), ClickAt(Rig, Face->GetCenter()));
	if (!TestTrue(TEXT("It opens the list again"), WaitForListShown(Rig, Dropdown)))
	{
		return false;
	}
	const TOptional<FBox2D> List = DreamControlsWorldTest::SeenRect(Rig, Dropdown->ListNode.Get());
	if (!TestTrue(TEXT("The camera sees the open list"), List.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click on the bare surface twelve pixels below the list completes"), ClickAt(Rig, PastBottomEdge(List.GetValue(), 12.0)));
	TestFalse(TEXT("It closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("...choosing nothing"), Dropdown->GetSelectedIndex(), 1);
	TestEqual(TEXT("One choice was announced"), Placed.Listener->SelectionIndices.Num(), 1);
	return true;
}

#endif
