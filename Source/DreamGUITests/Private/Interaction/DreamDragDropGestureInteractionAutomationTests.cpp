// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "DreamDragDropTestTypes.h"

/*
 * A drag with meaning, carried by a real pointer from a drag source: its visual under the pointer, the target it passes over,
 * and the two ways it ends without a drop -- Escape, and letting go over nothing.
 *
 * The reference is UMG's drag and drop (FUMGDragDropOp over Slate's FDragDropOperation): the decorator is moved to the pointer
 * on every drag event (FDragDropOperation::OnDragged, SlateCore/Private/Input/DragAndDrop.cpp:31-46) and destroyed when the
 * drag ends (:26-29); a widget is told OnDragEnter and OnDragLeave once each as the drag crosses its edges; Escape while a
 * drag is in flight cancels it (FSlateApplication::ProcessKeyDownEvent, Slate/Private/Framework/Application/
 * SlateApplication.cpp:4992-4996); and a drop nobody handled is the operation's DragCancelled (FUMGDragDropOp::OnDrop,
 * UMG/Private/Slate/UMGDragDropOp.cpp:45-65).
 */
namespace DreamDragDropGestureTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D CardSize(100.0, 100.0);

	struct FDragScene
	{
		UDreamWidget* Card = nullptr;
		UDreamWidget* Slot = nullptr;
		UDreamUIDragSource* Source = nullptr;
		UDreamUIDropTarget* Target = nullptr;
		UDreamUIDragDropSubsystem* DragDrop = nullptr;
		FVector2D CardCentre = FVector2D::ZeroVector;
		FVector2D SlotCentre = FVector2D::ZeroVector;
		/** Below both, where there is nothing but the rig's root. */
		FVector2D OverNothing = FVector2D::ZeroVector;
		double Threshold = 0.0;

		bool IsReady() const { return Source != nullptr && Target != nullptr && DragDrop != nullptr && Threshold > 0.0; }
	};

	/** What the target and the operation said, one probe per event; bound before the gesture. */
	struct FDragLog
	{
		TStrongObjectPtr<UDreamDragDropCallProbe> Enter;
		TStrongObjectPtr<UDreamDragDropCallProbe> Over;
		TStrongObjectPtr<UDreamDragDropCallProbe> Leave;
		TStrongObjectPtr<UDreamDragDropCallProbe> Accepted;
		TStrongObjectPtr<UDreamDragDropCallProbe> Cancelled;
		TStrongObjectPtr<UDreamDragDropCallProbe> Handled;

		explicit FDragLog(UDreamUIDropTarget* InTarget)
			: Enter(NewObject<UDreamDragDropCallProbe>())
			, Over(NewObject<UDreamDragDropCallProbe>())
			, Leave(NewObject<UDreamDragDropCallProbe>())
			, Accepted(NewObject<UDreamDragDropCallProbe>())
			, Cancelled(NewObject<UDreamDragDropCallProbe>())
			, Handled(NewObject<UDreamDragDropCallProbe>())
		{
			InTarget->OnDragEnter.AddDynamic(Enter.Get(), &UDreamDragDropCallProbe::OnOperation);
			InTarget->OnDragOver.AddDynamic(Over.Get(), &UDreamDragDropCallProbe::OnOperation);
			InTarget->OnDragLeave.AddDynamic(Leave.Get(), &UDreamDragDropCallProbe::OnOperation);
			InTarget->OnDropAccepted.AddDynamic(Accepted.Get(), &UDreamDragDropCallProbe::OnOperation);
		}

		/** The operation exists only once the drag has begun; its own two events are bound then. */
		bool ListenTo(UDreamDragDropOperation* InOperation)
		{
			if (InOperation == nullptr)
			{
				return false;
			}
			InOperation->OnDragCancelled.AddDynamic(Cancelled.Get(), &UDreamDragDropCallProbe::OnOperation);
			InOperation->OnDropHandled.AddDynamic(Handled.Get(), &UDreamDragDropCallProbe::OnOperation);
			return true;
		}
	};

	/** A card with a drag source carrying an "Item" with a visual, and a slot well apart that takes "Item". */
	FDragScene MakeScene(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FDragScene Scene;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Scene;
		}
		Scene.Card = InRig.MakeWidget(TEXT("Card"), nullptr, CardSize, FVector2D(-250.0, 100.0));
		Scene.Slot = InRig.MakeWidget(TEXT("Slot"), nullptr, CardSize, FVector2D(250.0, 100.0));
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
		Scene.OverNothing = FVector2D((Scene.CardCentre.X + Scene.SlotCentre.X) * 0.5, Scene.CardCentre.Y + 250.0);
		Scene.Threshold = FMath::Sqrt(static_cast<double>(InRig.Raycaster()->GetScaledDragThresholdSquare()));
		return Scene;
	}

	/** Press on the card and pull it past the threshold; then the operation the drag carries, or null. */
	UDreamDragDropOperation* BeginDrag(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FDragScene& InScene)
	{
		InTest.TestTrue(TEXT("Pressing on the card and pulling it past the threshold completes"),
			InRig.Driver()->Sequence()
				.MoveToPixel(InScene.CardCentre)
				.Press()
				.MoveToPixel(InScene.CardCentre + FVector2D(InScene.Threshold * 4.0, 0.0))
				.Perform());
		UDreamDragDropOperation* Operation = InScene.DragDrop->GetDragOperationForPointer(0);
		InTest.TestNotNull(TEXT("The drag carries the source's operation"), Operation);
		return Operation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragDropVisualFollowsTest,
	"DreamGUI.DragDrop.TheDragVisualFollowsThePointerAndGoesWhenTheDragEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDragDropVisualFollowsTest, "DreamGUI.DragDrop.TheDragVisualFollowsThePointerAndGoesWhenTheDragEnds", "[Pointer][Animated]")

/*
 * Once the drag has begun its visual is up, and every move of the pointer moves it by the same pixels -- right and down, then
 * left and down. Let go, the drag ends and the visual is gone.
 */
bool FDreamDragDropVisualFollowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropGestureTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FDragScene Scene = MakeScene(*this, Rig);
	if (!Scene.IsReady() || BeginDrag(*this, Rig, Scene) == nullptr)
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Visual = Driver->Find(FDreamBy::Name(TEXT("DreamUIDragVisual")));
	if (!TestTrue(TEXT("The drag's visual is up"), Visual->Exists()))
	{
		Driver->Sequence().Release().Perform();
		return false;
	}
	const TOptional<FBox2D> Before = Visual->GetPixelRect();

	const FVector2D FirstMove(60.0, 40.0);
	TestTrue(TEXT("Moving right and down completes"), Driver->Sequence().MoveBy(FirstMove).WaitFrames(1).Perform());
	const TOptional<FBox2D> AfterFirst = Visual->GetPixelRect();
	const FVector2D SecondMove(-30.0, 20.0);
	TestTrue(TEXT("Moving left and down completes"), Driver->Sequence().MoveBy(SecondMove).WaitFrames(1).Perform());
	const TOptional<FBox2D> AfterSecond = Visual->GetPixelRect();
	if (TestTrue(TEXT("The visual is on the viewport throughout"), Before.IsSet() && AfterFirst.IsSet() && AfterSecond.IsSet()))
	{
		const FVector2D FirstShift = AfterFirst->Min - Before->Min;
		const FVector2D SecondShift = AfterSecond->Min - AfterFirst->Min;
		TestTrue(FString::Printf(TEXT("The visual moved with the pointer (%.1f, %.1f for %.1f, %.1f)"), FirstShift.X, FirstShift.Y, FirstMove.X, FirstMove.Y),
			FirstShift.Equals(FirstMove, 1.0));
		TestTrue(FString::Printf(TEXT("...and again (%.1f, %.1f for %.1f, %.1f)"), SecondShift.X, SecondShift.Y, SecondMove.X, SecondMove.Y),
			SecondShift.Equals(SecondMove, 1.0));
	}

	TestTrue(TEXT("Letting go completes"), Driver->Sequence().Release().WaitFrames(1).Perform());
	TestFalse(TEXT("The drag is over"), Scene.DragDrop->IsDragInProgress());
	TestFalse(TEXT("...and its visual is gone"), Driver->Find(FDreamBy::Name(TEXT("DreamUIDragVisual")))->Exists());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragDropHoverEdgesTest,
	"DreamGUI.DragDrop.DraggingOntoATargetAndOffItEntersItOnceAndLeavesItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDragDropHoverEdgesTest, "DreamGUI.DragDrop.DraggingOntoATargetAndOffItEntersItOnceAndLeavesItOnce", "[Pointer][Animated]")

/*
 * The drag carried onto the slot, held there a few frames while moving a little, and carried off it again before letting go
 * over nothing. The slot is entered once, told Over while the drag is on it, and left once; it reads as drag-hovered exactly
 * while the drag is on it; and a drop that never happened accepts nothing.
 */
bool FDreamDragDropHoverEdgesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropGestureTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FDragScene Scene = MakeScene(*this, Rig);
	if (!Scene.IsReady())
	{
		return false;
	}
	FDragLog Log(Scene.Target);
	if (BeginDrag(*this, Rig, Scene) == nullptr)
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("Carrying the drag onto the slot completes"),
		Driver->Sequence().MoveToPixel(Scene.SlotCentre).MoveBy(FVector2D(5.0, 0.0)).MoveBy(FVector2D(-10.0, 5.0)).Perform());
	TestEqual(TEXT("The slot was entered once"), Log.Enter->CallCount, 1);
	TestTrue(TEXT("...is told the drag is over it"), Log.Over->CallCount >= 1);
	TestTrue(TEXT("...and reads as drag-hovered"), Scene.Target->IsDragHovered());
	TestEqual(TEXT("...and has not been left"), Log.Leave->CallCount, 0);

	TestTrue(TEXT("Carrying the drag off the slot completes"), Driver->Sequence().MoveToPixel(Scene.OverNothing).WaitFrames(1).Perform());
	TestEqual(TEXT("The slot was left once"), Log.Leave->CallCount, 1);
	TestEqual(TEXT("...not entered again"), Log.Enter->CallCount, 1);
	TestFalse(TEXT("...and no longer reads as drag-hovered"), Scene.Target->IsDragHovered());

	TestTrue(TEXT("Letting go over nothing completes"), Driver->Sequence().Release().WaitFrames(1).Perform());
	TestEqual(TEXT("Nothing was dropped on the slot"), Log.Accepted->CallCount, 0);
	TestEqual(TEXT("...and it was not left a second time"), Log.Leave->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragDropEscapeCancelsTest,
	"DreamGUI.DragDrop.EscapeDuringADragCancelsItAndALetGoAfterDropsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDragDropEscapeCancelsTest, "DreamGUI.DragDrop.EscapeDuringADragCancelsItAndALetGoAfterDropsNothing", "[Pointer][Animated]")

/*
 * The drag held over the slot, and Escape pressed: the drag is cancelled there and then -- the operation told so once, the
 * slot left, nothing in progress -- and the button let go over the slot afterwards drops nothing on it.
 */
bool FDreamDragDropEscapeCancelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropGestureTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FDragScene Scene = MakeScene(*this, Rig);
	if (!Scene.IsReady())
	{
		return false;
	}
	FDragLog Log(Scene.Target);
	if (!Log.ListenTo(BeginDrag(*this, Rig, Scene)))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	TestTrue(TEXT("Carrying the drag onto the slot completes"), Driver->Sequence().MoveToPixel(Scene.SlotCentre).WaitFrames(1).Perform());
	if (!TestTrue(TEXT("The slot is drag-hovered"), Scene.Target->IsDragHovered()))
	{
		Driver->Sequence().Release().Perform();
		return false;
	}

	TestTrue(TEXT("Pressing Escape with the drag held completes"), Driver->Sequence().Key(EKeys::Escape).Perform());
	TestEqual(TEXT("Escape cancelled the drag, once"), Log.Cancelled->CallCount, 1);
	TestFalse(TEXT("...nothing is being dragged"), Scene.DragDrop->IsDragInProgress());
	TestFalse(TEXT("...and the slot is no longer drag-hovered"), Scene.Target->IsDragHovered());

	TestTrue(TEXT("Letting go over the slot completes"), Driver->Sequence().Release().WaitFrames(1).Perform());
	TestEqual(TEXT("A cancelled drag drops nothing on the slot"), Log.Accepted->CallCount, 0);
	TestEqual(TEXT("...nor is its drop handled"), Log.Handled->CallCount, 0);
	TestEqual(TEXT("...nor is it cancelled a second time"), Log.Cancelled->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDragDropReleaseOverNothingTest,
	"DreamGUI.DragDrop.LettingGoOverNoTargetCancelsTheDragOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDragDropReleaseOverNothingTest, "DreamGUI.DragDrop.LettingGoOverNoTargetCancelsTheDragOnce", "[Pointer][Animated]")

/*
 * The drag carried to empty ground and let go there: nobody handled the drop, so the operation is cancelled (FUMGDragDropOp::
 * OnDrop) -- once -- its drop is not handled, the slot accepted nothing, and the drag is over.
 */
bool FDreamDragDropReleaseOverNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDragDropGestureTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FDragScene Scene = MakeScene(*this, Rig);
	if (!Scene.IsReady())
	{
		return false;
	}
	FDragLog Log(Scene.Target);
	if (!Log.ListenTo(BeginDrag(*this, Rig, Scene)))
	{
		return false;
	}

	TestTrue(TEXT("Carrying the drag to empty ground and letting go completes"),
		Rig.Driver()->Sequence().MoveToPixel(Scene.OverNothing).WaitFrames(1).Release().WaitFrames(1).Perform());
	TestEqual(TEXT("A drop nobody took cancels the drag, once"), Log.Cancelled->CallCount, 1);
	TestEqual(TEXT("...its drop is not handled"), Log.Handled->CallCount, 0);
	TestEqual(TEXT("...the slot accepted nothing"), Log.Accepted->CallCount, 0);
	TestFalse(TEXT("...and the drag is over"), Scene.DragDrop->IsDragInProgress());
	return true;
}

#endif
