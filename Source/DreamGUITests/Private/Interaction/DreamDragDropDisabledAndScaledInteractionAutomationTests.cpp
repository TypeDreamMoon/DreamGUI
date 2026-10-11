// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamCanvas.h"
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
#include "DreamDragDropTestTypes.h"

/*
 * A DRAG FROM A DISABLED SOURCE, ONTO A DISABLED TARGET, AND ON A SCALED CANVAS.
 *
 * UMG's drag and drop rides on the pointer: a drag is detected on the widget the press landed on (DetectDrag, then
 * OnDragDetected), and the drag events -- OnDragEnter, OnDragLeave, OnDrop -- go to the widgets under the pointer as the hit
 * test finds them. The hit test cuts a path at the first disabled widget (FHittestGrid::GetBubblePath, SlateCore/Private/
 * Input/HittestGrid.cpp), so a disabled drag source is never pressed and never begins a drag, and a disabled drop target is
 * never entered and never dropped on: letting go over it is letting go over no target, the operation's DragCancelled
 * (FUMGDragDropOp::OnDrop, UMG/Private/Slate/UMGDragDropOp.cpp:45-65). A finger is the same press, and the gamepad's
 * cursor -- Slate's analog cursor, whose accept is the left mouse button at the cursor -- the same press and drag.
 *
 * On a canvas scaled to half and to double, laid out in pixels, the mouse, a finger and the gamepad's cursor each carry the
 * card from where it is drawn onto the slot where it is drawn and drop it there.
 */
namespace DreamDragDropCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	struct FDragScene
	{
		UDreamWidget* Card = nullptr;
		UDreamWidget* Slot = nullptr;
		UDreamUIDragSource* Source = nullptr;
		UDreamUIDropTarget* Target = nullptr;
		UDreamUIDragDropSubsystem* DragDrop = nullptr;
		FVector2D CardCentre = FVector2D::ZeroVector;
		FVector2D SlotCentre = FVector2D::ZeroVector;
		double Threshold = 0.0;

		bool IsReady() const { return Source != nullptr && Target != nullptr && DragDrop != nullptr && Threshold > 0.0; }
	};

	/** What the target and the operations said, one probe per event, bound before the gesture. */
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
	 * A card with a drag source carrying an "Item" with a visual, and a slot to its right that takes "Item" -- laid out in
	 * pixels at InScale: the card 100 pixels square, the slot 160, their centres 500 pixels apart.
	 */
	FDragScene MakeScene(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		FDragScene Scene;
		Scene.Card = InRig.MakeWidget(TEXT("Card"), nullptr, FVector2D(100.0, 100.0) / InScale, FVector2D(-250.0, 100.0) / InScale);
		Scene.Slot = InRig.MakeWidget(TEXT("Slot"), nullptr, FVector2D(160.0, 160.0) / InScale, FVector2D(250.0, 100.0) / InScale);
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
		InRig.PumpFrames(1);

		const TOptional<FVector2D> CardCentre = InRig.Driver()->Find(FDreamBy::Widget(Scene.Card))->GetCentrePixel();
		const TOptional<FVector2D> SlotCentre = InRig.Driver()->Find(FDreamBy::Widget(Scene.Slot))->GetCentrePixel();
		if (!InTest.TestTrue(TEXT("The card and the slot are somewhere the pointer can reach"), CardCentre.IsSet() && SlotCentre.IsSet()))
		{
			return FDragScene();
		}
		Scene.CardCentre = CardCentre.GetValue();
		Scene.SlotCentre = SlotCentre.GetValue();
		Scene.Threshold = FMath::Sqrt(static_cast<double>(InRig.Raycaster()->GetScaledDragThresholdSquare()));
		return Scene;
	}

	/** The mouse pressed on the card, pulled past the threshold and across onto the slot, and let go there. */
	bool MouseDragOntoSlot(FDreamDriverRig& InRig, const FDragScene& InScene)
	{
		return InRig.Driver()->Sequence()
			.MoveToPixel(InScene.CardCentre)
			.Press()
			.MoveToPixel(InScene.CardCentre + FVector2D(InScene.Threshold * 4.0, 0.0))
			.MoveToPixel((InScene.CardCentre + InScene.SlotCentre) * 0.5)
			.MoveToPixel(InScene.SlotCentre)
			.WaitFrames(1)
			.Release()
			.WaitFrames(1)
			.Perform();
	}

	/** The same with a finger. */
	bool FingerDragOntoSlot(FDreamDriverRig& InRig, const FDragScene& InScene, int32 InFinger)
	{
		return InRig.Driver()->Sequence()
			.TouchDown(InFinger, InScene.CardCentre)
			.TouchMoveTo(InFinger, InScene.CardCentre + FVector2D(InScene.Threshold * 4.0, 0.0))
			.TouchMoveTo(InFinger, (InScene.CardCentre + InScene.SlotCentre) * 0.5)
			.TouchMoveTo(InFinger, InScene.SlotCentre)
			.WaitFrames(1)
			.TouchUp(InFinger)
			.WaitFrames(1)
			.Perform();
	}

	/** Seconds of full stick that carry the gamepad's cursor from the card to the slot. */
	float SecondsFromCardToSlot(const FDragScene& InScene)
	{
		return static_cast<float>((InScene.SlotCentre.X - InScene.CardCentre.X) / UDreamGUISettings::Get()->VirtualCursorSpeed);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDragDropDisabledSourceTest,
	"DreamGUI.DragDrop.ADisabledSourceIsPickedUpNeitherByTheMouseNorByAFingerUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDragDropDisabledSourceTest, "DreamGUI.DragDrop.ADisabledSourceIsPickedUpNeitherByTheMouseNorByAFingerUntilItIsEnabled", "[Pointer][Touch][Disabled]")

/*
 * The card disabled. The mouse pressed on it and pulled well past the threshold begins no drag, and let go over the slot
 * drops nothing; a finger does no better. Enabled, the mouse's drag drops the card on the slot once, and so does a finger's.
 */
bool FDreamCoverageDragDropDisabledSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	const FDragScene Scene = Rig.IsUsable() ? MakeScene(*this, Rig, 1.0) : FDragScene();
	if (!TestTrue(TEXT("The rig and the scene came up"), Scene.IsReady()))
	{
		return false;
	}
	FDragLog Log(Scene.Target);
	Scene.Card->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("The mouse pressing on the disabled card and pulling it past the threshold completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Scene.CardCentre)
		.Press()
		.MoveToPixel(Scene.CardCentre + FVector2D(Scene.Threshold * 4.0, 0.0))
		.Then([this, &Scene](FDreamDriverContext&)
		{
			TestNull(TEXT("No drag began from the disabled card"), Scene.DragDrop->GetDragOperationForPointer(0));
		})
		.MoveToPixel(Scene.SlotCentre)
		.Release()
		.WaitFrames(1)
		.Perform());
	TestTrue(TEXT("A finger pressing on the disabled card and pulling it past the threshold completes"), Rig.Driver()->Sequence()
		.TouchDown(0, Scene.CardCentre)
		.TouchMoveTo(0, Scene.CardCentre + FVector2D(Scene.Threshold * 4.0, 0.0))
		.Then([this, &Scene](FDreamDriverContext&)
		{
			TestNull(TEXT("No drag began from the disabled card under the finger"), Scene.DragDrop->GetDragOperationForPointer(DreamUIPointerIds::ForTouch(0)));
		})
		.TouchMoveTo(0, Scene.SlotCentre)
		.TouchUp(0)
		.WaitFrames(1)
		.Perform());
	TestFalse(TEXT("No drag is in progress"), Scene.DragDrop->IsDragInProgress());
	TestEqual(TEXT("...and nothing was dropped on the slot"), Log.Accepted->CallCount, 0);

	Scene.Card->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The mouse dragging the enabled card onto the slot completes"), MouseDragOntoSlot(Rig, Scene));
	TestEqual(TEXT("Enabled, the mouse's drag dropped the card on the slot once"), Log.Accepted->CallCount, 1);
	TestTrue(TEXT("A finger dragging it onto the slot completes"), FingerDragOntoSlot(Rig, Scene, 1));
	TestEqual(TEXT("...and the finger's drag once more"), Log.Accepted->CallCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDragDropDisabledTargetTest,
	"DreamGUI.DragDrop.ACardDraggedOverADisabledSlotIsNeitherTakenInNorDroppedThereUntilTheSlotIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDragDropDisabledTargetTest, "DreamGUI.DragDrop.ACardDraggedOverADisabledSlotIsNeitherTakenInNorDroppedThereUntilTheSlotIsEnabled", "[Pointer][Disabled]")

/*
 * The slot disabled. The card dragged onto it: the slot is never entered, and letting go there drops nothing -- the drag is
 * cancelled, as a drop on no target is. Enabled, the same drag enters it and drops the card there once.
 */
bool FDreamCoverageDragDropDisabledTargetTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	const FDragScene Scene = Rig.IsUsable() ? MakeScene(*this, Rig, 1.0) : FDragScene();
	if (!TestTrue(TEXT("The rig and the scene came up"), Scene.IsReady()))
	{
		return false;
	}
	FDragLog Log(Scene.Target);
	TStrongObjectPtr<UDreamDragDropCallProbe> Cancelled(NewObject<UDreamDragDropCallProbe>());
	Scene.Slot->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Dragging the card onto the disabled slot and letting go completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Scene.CardCentre)
		.Press()
		.MoveToPixel(Scene.CardCentre + FVector2D(Scene.Threshold * 4.0, 0.0))
		.Then([this, &Scene, &Cancelled](FDreamDriverContext&)
		{
			UDreamDragDropOperation* Operation = Scene.DragDrop->GetDragOperationForPointer(0);
			if (TestNotNull(TEXT("The drag began from the card"), Operation))
			{
				Operation->OnDragCancelled.AddDynamic(Cancelled.Get(), &UDreamDragDropCallProbe::OnOperation);
			}
		})
		.MoveToPixel((Scene.CardCentre + Scene.SlotCentre) * 0.5)
		.MoveToPixel(Scene.SlotCentre)
		.WaitFrames(1)
		.Release()
		.WaitFrames(1)
		.Perform());
	TestEqual(TEXT("The disabled slot was never entered"), Log.Enter->CallCount, 0);
	TestEqual(TEXT("...nothing was dropped on it"), Log.Accepted->CallCount, 0);
	TestEqual(TEXT("...and the drag was cancelled once, as a drop on no target is"), Cancelled->CallCount, 1);

	Scene.Slot->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Dragging the card onto the enabled slot completes"), MouseDragOntoSlot(Rig, Scene));
	TestTrue(TEXT("Enabled, the slot was entered"), Log.Enter->CallCount >= 1);
	TestEqual(TEXT("...and the card dropped on it once"), Log.Accepted->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDragDropDisabledCursorTest,
	"DreamGUI.DragDrop.TheGamepadsCursorPicksNothingUpFromADisabledSourceAndCarriesTheCardOntoTheSlotOnceItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDragDropDisabledCursorTest, "DreamGUI.DragDrop.TheGamepadsCursorPicksNothingUpFromADisabledSourceAndCarriesTheCardOntoTheSlotOnceItIsEnabled", "[Nav][Disabled]")

/*
 * The card disabled, the gamepad's cursor turned on over it: the confirm held and the stick pushed toward the slot begin no
 * drag, and the confirm let go there drops nothing. The stick takes the cursor back to the card; enabled now, the same
 * confirm-and-stick carries the card to the slot and drops it there once.
 */
bool FDreamCoverageDragDropDisabledCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller.
	Rig.EnsureGameInputHost();
	const FDragScene Scene = MakeScene(*this, Rig, 1.0);
	if (!TestTrue(TEXT("The scene came up"), Scene.IsReady()))
	{
		return false;
	}
	FDragLog Log(Scene.Target);
	Scene.Card->SetIsEnabled(false);
	Rig.PumpFrames(1);
	const float Seconds = SecondsFromCardToSlot(Scene);

	TestTrue(TEXT("The cursor on the disabled card, the confirm held and the stick pushed toward the slot completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Scene.CardCentre)
		.ActivateVirtualCursor()
		.VirtualCursorPress()
		.VirtualCursorStick(FVector2D(1.0, 0.0), Seconds)
		.WaitFrames(1)
		.Then([this, &Scene](FDreamDriverContext&)
		{
			TestFalse(TEXT("No drag began from the disabled card"), Scene.DragDrop->IsDragInProgress());
		})
		.VirtualCursorRelease()
		.WaitFrames(1)
		.Perform());
	TestEqual(TEXT("Nothing was dropped on the slot"), Log.Accepted->CallCount, 0);

	Scene.Card->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The stick taking the cursor back to the card, and the confirm-and-stick carrying it to the slot, complete"), Rig.Driver()->Sequence()
		.VirtualCursorStick(FVector2D(-1.0, 0.0), Seconds)
		.WaitFrames(1)
		.VirtualCursorPress()
		.VirtualCursorStick(FVector2D(1.0, 0.0), Seconds)
		.WaitFrames(1)
		.Then([this, &Scene](FDreamDriverContext&)
		{
			TestTrue(TEXT("Enabled, the cursor's press and the stick began a drag"), Scene.DragDrop->IsDragInProgress());
		})
		.VirtualCursorRelease()
		.WaitFrames(1)
		.Perform());
	TestEqual(TEXT("...which dropped the card on the slot once"), Log.Accepted->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDragDropScaledPointerTest,
	"DreamGUI.DragDrop.OnACanvasScaledToHalfAndToDoubleTheMouseAndAFingerEachDropTheCardOnTheSlotWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDragDropScaledPointerTest, "DreamGUI.DragDrop.OnACanvasScaledToHalfAndToDoubleTheMouseAndAFingerEachDropTheCardOnTheSlotWhereItIsDrawn", "[Pointer][Touch][Scaled]")

/* At half and at double, laid out in pixels: the mouse's drag drops the card on the slot once, and a finger's once more. */
bool FDreamCoverageDragDropScaledPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		const FDragScene Scene = MakeScene(*this, Rig, Scale);
		if (!TestTrue(At(Scale, TEXT("The scene came up")), Scene.IsReady()))
		{
			return false;
		}
		FDragLog Log(Scene.Target);
		TestTrue(At(Scale, TEXT("The mouse dragging the card onto the slot completes")), MouseDragOntoSlot(Rig, Scene));
		TestEqual(*At(Scale, TEXT("The mouse's drag dropped the card on the slot once")), Log.Accepted->CallCount, 1);
		TestTrue(At(Scale, TEXT("A finger dragging the card onto the slot completes")), FingerDragOntoSlot(Rig, Scene, 0));
		TestEqual(*At(Scale, TEXT("...and the finger's drag once more")), Log.Accepted->CallCount, 2);
		TestFalse(At(Scale, TEXT("No drag is left in progress")), Scene.DragDrop->IsDragInProgress());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDragDropScaledCursorTest,
	"DreamGUI.DragDrop.OnACanvasScaledToHalfAndToDoubleTheGamepadsCursorCarriesTheCardOntoTheSlotWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDragDropScaledCursorTest, "DreamGUI.DragDrop.OnACanvasScaledToHalfAndToDoubleTheGamepadsCursorCarriesTheCardOntoTheSlotWhereItIsDrawn", "[Nav][Scaled]")

/*
 * At half and at double, laid out in pixels: the gamepad's cursor turned on over the card, the confirm held, the stick pushed
 * for as long as its speed takes to cover the 500 pixels to the slot, and the confirm let go -- the card is dropped on the
 * slot once.
 */
bool FDreamCoverageDragDropScaledCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		Rig.EnsureGameInputHost();
		const FDragScene Scene = MakeScene(*this, Rig, Scale);
		if (!TestTrue(At(Scale, TEXT("The scene came up")), Scene.IsReady()))
		{
			return false;
		}
		FDragLog Log(Scene.Target);
		TestTrue(At(Scale, TEXT("The cursor on the card, the confirm held, the stick pushed to the slot and the confirm let go completes")),
			Rig.Driver()->Sequence()
				.MoveToPixel(Scene.CardCentre)
				.ActivateVirtualCursor()
				.VirtualCursorPress()
				.VirtualCursorStick(FVector2D(1.0, 0.0), SecondsFromCardToSlot(Scene))
				.WaitFrames(1)
				.VirtualCursorRelease()
				.WaitFrames(1)
				.Perform());
		TestEqual(*At(Scale, TEXT("The cursor's drag dropped the card on the slot once")), Log.Accepted->CallCount, 1);
		TestFalse(At(Scale, TEXT("No drag is left in progress")), Scene.DragDrop->IsDragInProgress());
	}
	return true;
}

#endif
