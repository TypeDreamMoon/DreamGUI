// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputTypes.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "DreamDragDropTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * DRAG AND DROP IN THE WORLD, AND A FINGER AND THE GAMEPAD'S CURSOR CARRYING A CARD AS SHIPPED.
 *
 * UMG's drag and drop rides on the pointer: a drag is detected on the widget the press landed on (DetectDrag, then
 * OnDragDetected), and the drag events -- OnDragEnter, OnDragLeave, OnDrop -- go to the widgets under the pointer as the hit
 * test finds them (FUMGDragDropOp, UMG/Private/Slate/UMGDragDropOp.cpp). In a widget component the hit test is the one the
 * interaction's ray makes, so a card on a panel in the level is picked up where it is seen and dropped on the slot the ray
 * lands on, by the mouse or by a finger -- FSlateApplication hands an unanswered touch on as a mouse press and its moves as
 * the mouse's. The gamepad has a pointer of its own for this, Slate's analog cursor, whose accept is the left mouse button at
 * the cursor and whose stick moves it (FAnalogCursor): held and pushed, it carries a card as the mouse does.
 *
 * A world-space press becomes a drag by the world pointer's own rule (UDreamWorldSpaceRaycaster::ShouldStartDrag, its
 * DragThreshold in viewport pixels), so the first move here goes well past it.
 */
namespace DreamDragDropWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	struct FDragScene
	{
		UDreamWidget* Card = nullptr;
		UDreamWidget* Slot = nullptr;
		UDreamUIDragSource* Source = nullptr;
		UDreamUIDropTarget* Target = nullptr;
		UDreamUIDragDropSubsystem* DragDrop = nullptr;
		FVector2D CardCentre = FVector2D::ZeroVector;
		FVector2D SlotCentre = FVector2D::ZeroVector;
		/** The first move of a drag: well past whichever threshold the press's raycaster keeps. */
		double FirstMove = 40.0;

		bool IsReady() const { return Source != nullptr && Target != nullptr && DragDrop != nullptr; }
	};

	struct FDragLog
	{
		TStrongObjectPtr<UDreamDragDropCallProbe> Enter;
		TStrongObjectPtr<UDreamDragDropCallProbe> Accepted;

		explicit FDragLog(UDreamUIDropTarget* InTarget)
			: Enter(NewObject<UDreamDragDropCallProbe>())
			, Accepted(NewObject<UDreamDragDropCallProbe>())
		{
			InTarget->OnDragEnter.AddDynamic(Enter.Get(), &UDreamDragDropCallProbe::OnOperation);
			InTarget->OnDropAccepted.AddDynamic(Accepted.Get(), &UDreamDragDropCallProbe::OnOperation);
		}
	};

	/**
	 * A card with a drag source carrying an "Item" with a visual, and a slot to its right that takes "Item", under InParent
	 * (the rig's root when null): the card 80 units square at (-150, 0), the slot 160 at (150, 0).
	 */
	FDragScene MakeScene(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InParent)
	{
		FDragScene Scene;
		Scene.Card = InRig.MakeWidget(TEXT("Card"), InParent, FVector2D(80.0, 80.0), FVector2D(-150.0, 0.0));
		Scene.Slot = InRig.MakeWidget(TEXT("Slot"), InParent, FVector2D(160.0, 160.0), FVector2D(150.0, 0.0));
		Scene.Source = Scene.Card != nullptr ? Scene.Card->AddComponent<UDreamUIDragSource>() : nullptr;
		Scene.Target = Scene.Slot != nullptr ? Scene.Slot->AddComponent<UDreamUIDropTarget>() : nullptr;
		Scene.DragDrop = UDreamUIDragDropSubsystem::Get(InRig.GetWorld());
		if (!InTest.TestTrue(TEXT("A drag source on the card, a drop target on the slot, and the drag-drop service"),
			Scene.Source != nullptr && Scene.Target != nullptr && Scene.DragDrop != nullptr))
		{
			return FDragScene();
		}
		Scene.Source->Tag = TEXT("Item");
		Scene.Source->Payload = Scene.Card;
		Scene.Source->DragVisualClass = UDreamBorder::StaticClass();
		Scene.Target->RequiredTag = TEXT("Item");
		InRig.PumpFrames(2);

		const TOptional<FVector2D> CardCentre = PixelOf(InRig, Scene.Card);
		const TOptional<FVector2D> SlotCentre = PixelOf(InRig, Scene.Slot);
		if (!InTest.TestTrue(TEXT("The card and the slot are somewhere the pointer can reach"), CardCentre.IsSet() && SlotCentre.IsSet()))
		{
			return FDragScene();
		}
		Scene.CardCentre = CardCentre.GetValue();
		Scene.SlotCentre = SlotCentre.GetValue();
		if (InParent == nullptr && InRig.Raycaster() != nullptr)
		{
			Scene.FirstMove = FMath::Max(Scene.FirstMove, 4.0 * FMath::Sqrt(static_cast<double>(InRig.Raycaster()->GetScaledDragThresholdSquare())));
		}
		return Scene;
	}

	/** A finger pressed on the card, pulled past the threshold and across onto the slot, and lifted there. */
	bool FingerDragOntoSlot(FDreamDriverRig& InRig, const FDragScene& InScene, int32 InFinger)
	{
		return InRig.Driver()->Sequence()
			.TouchDown(InFinger, InScene.CardCentre)
			.TouchMoveTo(InFinger, InScene.CardCentre + FVector2D(InScene.FirstMove, 0.0))
			.TouchMoveTo(InFinger, (InScene.CardCentre + InScene.SlotCentre) * 0.5)
			.TouchMoveTo(InFinger, InScene.SlotCentre)
			.WaitFrames(1)
			.TouchUp(InFinger)
			.WaitFrames(1)
			.Perform();
	}

	/** Seconds of full stick that carry the gamepad's cursor from the card to the slot: its speed is viewport pixels a second. */
	float SecondsFromCardToSlot(const FDragScene& InScene)
	{
		return static_cast<float>((InScene.SlotCentre.X - InScene.CardCentre.X) / UDreamGUISettings::Get()->VirtualCursorSpeed);
	}

	/**
	 * The gamepad's cursor turned on over the card, the confirm held, the stick pushed for as long as its speed takes to reach
	 * the slot, and the confirm let go: a drag began while it was held, and the card was dropped on the slot once.
	 */
	bool RunCursorCarries(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		// The cursor reads the stick from player 0's controller; made after the world pointer, which asks to come first.
		Rig.EnsureGameInputHost();
		const FDragScene Scene = MakeScene(InTest, Rig, Parent);
		if (!InTest.TestTrue(TEXT("The scene came up"), Scene.IsReady()))
		{
			return false;
		}
		FDragLog Log(Scene.Target);
		bool bDragging = false;

		InTest.TestTrue(TEXT("The cursor on the card, the confirm held, the stick pushed to the slot and the confirm let go completes"),
			Rig.Driver()->Sequence()
				.MoveToPixel(Scene.CardCentre)
				.ActivateVirtualCursor()
				.VirtualCursorPress()
				.VirtualCursorStick(FVector2D(1.0, 0.0), SecondsFromCardToSlot(Scene))
				.WaitFrames(1)
				.Then([&bDragging, &Scene](FDreamDriverContext&) { bDragging = Scene.DragDrop->IsDragInProgress(); })
				.VirtualCursorRelease()
				.WaitFrames(1)
				.Perform());
		InTest.TestTrue(TEXT("The cursor's held confirm and the stick began a drag from the card"), bDragging);
		InTest.TestTrue(TEXT("The slot was entered on the way"), Log.Enter->CallCount >= 1);
		InTest.TestEqual(TEXT("The cursor's drag dropped the card on the slot once"), Log.Accepted->CallCount, 1);
		InTest.TestFalse(TEXT("No drag is left in progress"), Scene.DragDrop->IsDragInProgress());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldDragDropPointerTest,
	"DreamGUI.DragDrop.OnAWorldPanelTheMouseAndAFingerEachDropTheCardOnTheSlotWhereTheRayLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldDragDropPointerTest, "DreamGUI.DragDrop.OnAWorldPanelTheMouseAndAFingerEachDropTheCardOnTheSlotWhereTheRayLands", "[Pointer][Touch][World]")

/*
 * The card and the slot on a panel. The mouse pressed on the card through the world pointer, pulled past the threshold and
 * across onto the slot, and let go: a drag was in progress on the way, the slot was entered, and the card dropped there once.
 * A finger does the same once more.
 */
bool FDreamWorldDragDropPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	const FDragScene Scene = Stage.IsReady() ? MakeScene(*this, Rig, Stage.Panel) : FDragScene();
	if (!TestTrue(TEXT("The scene came up on the panel"), Scene.IsReady()))
	{
		return false;
	}
	FDragLog Log(Scene.Target);

	TestTrue(TEXT("The mouse pressing on the card and pulling it past the threshold completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Scene.CardCentre)
		.Press()
		.MoveToPixel(Scene.CardCentre + FVector2D(Scene.FirstMove, 0.0))
		.Then([this, &Scene](FDreamDriverContext&)
		{
			TestNotNull(TEXT("The press on the card through the world pointer began a drag"), Scene.DragDrop->GetDragOperationForPointer(0));
		})
		.MoveToPixel((Scene.CardCentre + Scene.SlotCentre) * 0.5)
		.MoveToPixel(Scene.SlotCentre)
		.WaitFrames(1)
		.Release()
		.WaitFrames(1)
		.Perform());
	TestTrue(TEXT("The slot was entered on the way"), Log.Enter->CallCount >= 1);
	TestEqual(TEXT("The mouse's drag dropped the card on the slot once"), Log.Accepted->CallCount, 1);
	TestFalse(TEXT("No drag is left in progress"), Scene.DragDrop->IsDragInProgress());

	TestTrue(TEXT("A finger dragging the card onto the slot through the world pointer completes"), FingerDragOntoSlot(Rig, Scene, 0));
	TestEqual(TEXT("...and the finger's drag dropped it once more"), Log.Accepted->CallCount, 2);
	TestFalse(TEXT("No drag is left in progress"), Scene.DragDrop->IsDragInProgress());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldDragDropCursorTest,
	"DreamGUI.DragDrop.OnAWorldPanelTheGamepadsCursorCarriesTheCardOntoTheSlotAndDropsItThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldDragDropCursorTest, "DreamGUI.DragDrop.OnAWorldPanelTheGamepadsCursorCarriesTheCardOntoTheSlotAndDropsItThere", "[Nav][World]")

/* The cursor's pixel is the world pointer's screen position, so its ray lands on the panel where the cursor is seen. */
bool FDreamWorldDragDropCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropWorldTestLocal;
	return RunCursorCarries(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedDragDropFingerTest,
	"DreamGUI.DragDrop.AFingerDraggingTheCardOntoTheSlotEntersItAndDropsTheCardThereOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedDragDropFingerTest, "DreamGUI.DragDrop.AFingerDraggingTheCardOntoTheSlotEntersItAndDropsTheCardThereOnce", "[Touch][Animated]")

/*
 * As shipped on the screen: a finger pressed on the card, pulled past the threshold and across onto the slot, and lifted. The
 * drag is the finger's own pointer's while it is in progress, the slot is entered on the way, and the card is dropped there
 * once; nothing is left dragging.
 */
bool FDreamAnimatedDragDropFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	const FDragScene Scene = Rig.IsUsable() ? MakeScene(*this, Rig, nullptr) : FDragScene();
	if (!TestTrue(TEXT("The rig and the scene came up"), Scene.IsReady()))
	{
		return false;
	}
	FDragLog Log(Scene.Target);

	TestTrue(TEXT("A finger dragging the card onto the slot completes"), Rig.Driver()->Sequence()
		.TouchDown(0, Scene.CardCentre)
		.TouchMoveTo(0, Scene.CardCentre + FVector2D(Scene.FirstMove, 0.0))
		.Then([this, &Scene](FDreamDriverContext&)
		{
			TestNotNull(TEXT("The finger's drag is its own pointer's"), Scene.DragDrop->GetDragOperationForPointer(DreamUIPointerIds::ForTouch(0)));
			TestNull(TEXT("...and not the mouse's"), Scene.DragDrop->GetDragOperationForPointer(0));
		})
		.TouchMoveTo(0, (Scene.CardCentre + Scene.SlotCentre) * 0.5)
		.TouchMoveTo(0, Scene.SlotCentre)
		.WaitFrames(1)
		.TouchUp(0)
		.WaitFrames(1)
		.Perform());
	TestTrue(TEXT("The slot was entered on the way"), Log.Enter->CallCount >= 1);
	TestEqual(TEXT("The finger's drag dropped the card on the slot once"), Log.Accepted->CallCount, 1);
	TestFalse(TEXT("No drag is left in progress"), Scene.DragDrop->IsDragInProgress());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedDragDropCursorTest,
	"DreamGUI.DragDrop.TheGamepadsCursorCarriesTheCardOntoTheSlotAndDropsItThereOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedDragDropCursorTest, "DreamGUI.DragDrop.TheGamepadsCursorCarriesTheCardOntoTheSlotAndDropsItThereOnce", "[Nav][Animated]")

/* As shipped on the screen: Slate's analog cursor turns the held accept and the stick into the mouse's drag. */
bool FDreamAnimatedDragDropCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropWorldTestLocal;
	return RunCursorCarries(*this, false);
}

#endif
