// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIActionTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "DreamNavigationTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * BACK, A HELD BUTTON, THE VIRTUAL CURSOR AND A VIEWPORT THAT CHANGES SIZE -- EACH THROUGH REAL INPUT.
 *
 * CommonUI's activatable stack takes Back one screen at a time: the widget in front handles it
 * (UCommonActivatableWidget::NativeOnHandleBackAction deactivates it) and the one under it is in front for the next press.
 * A hold-to-confirm action (CommonUI's hold data on an input action) fires once its hold time has been held, while the key
 * is still down, and a key let go of earlier fires nothing. Slate's analog cursor (FAnalogCursor, Slate/Private/Framework/
 * Application/AnalogCursor.cpp) turns the pad's accept into the mouse's left button at the cursor (HandleKeyDownEvent) and
 * every move of the cursor into a mouse move (UpdateCursorPosition), so a held accept and a pushed stick drag what is under
 * the cursor. And a screen laid out against the viewport's corners is laid out again when the viewport changes size
 * (SGameLayerManager::UpdateLayout), so a click lands where the widget is drawn now.
 */
namespace DreamBackHoldCursorInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A panel with one button on it and a navigation scope that waits to be opened, as a page or a dialog has. */
	UDreamUINavigationScope* MakeScreen(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InSize)
	{
		UDreamWidget* Panel = InRig.MakeWidget(InName, nullptr, InSize);
		UDreamButton* Button = Panel != nullptr
			? InRig.MakeControl<UDreamButton>(FString::Printf(TEXT("%sButton"), InName), Panel, FVector2D(160.0, 40.0))
			: nullptr;
		UDreamUINavigationScope* Scope = Panel != nullptr ? Panel->AddComponent<UDreamUINavigationScope>() : nullptr;
		if (!InTest.TestTrue(*FString::Printf(TEXT("A screen '%s' with a button and a scope can be made"), InName), Button != nullptr && Scope != nullptr))
		{
			return nullptr;
		}
		// A widget in play enables what is added to it at once, and a scope enabled opens itself: taken off again, and told
		// to wait from then on, so each screen opens when the test opens it.
		Scope->SetActivateWhenEnabled(false);
		Scope->DeactivateScope();
		return Scope;
	}

	/** One hold-to-confirm action, held for InHoldTime, on a keyboard key and a pad button. */
	UDataTable* MakeHoldAction(FName InRowName, const FKey& InKeyboardKey, const FKey& InGamepadKey, float InHoldTime)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		FDreamUIInputActionData Row;
		Row.DisplayName = FText::FromName(InRowName);
		Row.KeyboardKey = InKeyboardKey;
		Row.GamepadKey = InGamepadKey;
		Row.HoldTime = InHoldTime;
		Table->AddRow(InRowName, Row);
		return Table;
	}

	/** How one of the hold tests is driven: under which host, with which key. */
	struct FHoldCase
	{
		EDreamRigInputHost Host;
		FKey Key;
		const TCHAR* Name;
	};

	TArray<FHoldCase> HoldCases()
	{
		return {
			{ EDreamRigInputHost::ModuleOnly, EKeys::X, TEXT("the keyboard key, through the module") },
			{ EDreamRigInputHost::StandaloneActor, EKeys::Gamepad_FaceButton_Left, TEXT("the pad button, through the player controller") },
		};
	}

	/** A rig for InCase, the hold action registered for every screen, and the counter it fires. False having said why. */
	struct FHoldRig
	{
		TStrongObjectPtr<UDataTable> Table;
		TStrongObjectPtr<UDreamActionCallCounter> Counter;
		FDreamUIActionHandle Handle;
		UDreamUIActionRouter* Router = nullptr;
	};

	bool RegisterHold(FAutomationTestBase& InTest, FDreamDriverRig& InRig, float InHoldTime, FHoldRig& OutHold)
	{
		OutHold.Router = InRig.GetWorld() != nullptr ? InRig.GetWorld()->GetSubsystem<UDreamUIActionRouter>() : nullptr;
		if (!InTest.TestNotNull(TEXT("The rig's world has an action router"), OutHold.Router))
		{
			return false;
		}
		const FName RowName(TEXT("HoldToDelete"));
		OutHold.Table.Reset(MakeHoldAction(RowName, EKeys::X, EKeys::Gamepad_FaceButton_Left, InHoldTime));
		OutHold.Counter.Reset(NewObject<UDreamActionCallCounter>());
		FDataTableRowHandle Row;
		Row.DataTable = OutHold.Table.Get();
		Row.RowName = RowName;
		FDreamUIActionExecutedDelegate Fire;
		Fire.BindUFunction(OutHold.Counter.Get(), TEXT("Fire"));
		OutHold.Handle = OutHold.Router->RegisterAction(nullptr, Row, Fire);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBackClosesOneScreenAtATimeTest,
	"DreamGUI.Navigation.Back.ThePadsBackButtonClosesTheDialogInFrontAndTheNextPressThePageUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamBackClosesOneScreenAtATimeTest, "DreamGUI.Navigation.Back.ThePadsBackButtonClosesTheDialogInFrontAndTheNextPressThePageUnderIt", "[Nav][Animated]")

/*
 * A page, and a dialog opened over it. The pad's Back button through the player controller closes the dialog and leaves
 * the page open and in front; the next press closes the page; a third finds nothing open and closes nothing.
 */
bool FDreamBackClosesOneScreenAtATimeTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackHoldCursorInteractionTestLocal;
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	UDreamUINavigationStack* Stack = Rig.IsUsable() ? UDreamUINavigationStack::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig came up with a navigation stack"), Stack))
	{
		return false;
	}
	UDreamUINavigationScope* Page = MakeScreen(*this, Rig, TEXT("Page"), FVector2D(800.0, 500.0));
	UDreamUINavigationScope* Dialog = MakeScreen(*this, Rig, TEXT("Dialog"), FVector2D(400.0, 240.0));
	if (Page == nullptr || Dialog == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	Page->ActivateScope();
	Dialog->ActivateScope();
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The dialog is in front of the page"), Stack->GetActiveScope(0) == Dialog))
	{
		return false;
	}

	TestTrue(TEXT("The pad's Back completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("Back closed the dialog"), Dialog->IsScopeActive());
	TestTrue(TEXT("...and only the dialog: the page is open and in front"), Page->IsScopeActive() && Stack->GetActiveScope(0) == Page);

	TestTrue(TEXT("The pad's Back again completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("The second Back closed the page"), Page->IsScopeActive());
	TestNull(TEXT("...and nothing is open now"), Stack->GetActiveScope(0));

	TestTrue(TEXT("A third Back completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("...and opens nothing back up"), Page->IsScopeActive() || Dialog->IsScopeActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHoldKeyFiresAtTheHoldTimeTest,
	"DreamGUI.Navigation.Actions.HoldingTheBoundKeyFiresItsActionWhenTheHoldTimeIsUpAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamHoldKeyFiresAtTheHoldTimeTest, "DreamGUI.Navigation.Actions.HoldingTheBoundKeyFiresItsActionWhenTheHoldTimeIsUpAndNotBefore", "[Nav][Animated]")

/*
 * Half a second to hold, under each host's own road for the key. Halfway through, nothing has fired and the progress a
 * filling ring is drawn from says about half; past the hold time, with the key still down, it has fired -- once, however
 * long the key stays down after.
 */
bool FDreamHoldKeyFiresAtTheHoldTimeTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackHoldCursorInteractionTestLocal;
	constexpr float HoldTime = 0.5f;
	for (const FHoldCase& Case : HoldCases())
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = Case.Host;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
		Rig.BindTest(this);
		FHoldRig Hold;
		if (!TestTrue(FString::Printf(TEXT("[%s] The rig came up"), Case.Name), Rig.IsUsable()) || !RegisterHold(*this, Rig, HoldTime, Hold))
		{
			continue;
		}
		UDreamActionCallCounter* Counter = Hold.Counter.Get();
		UDreamUIActionRouter* Router = Hold.Router;
		const FDreamUIActionHandle Handle = Hold.Handle;
		const FString Under(Case.Name);

		TestTrue(FString::Printf(TEXT("[%s] Holding the key past its hold time and letting go completes"), Case.Name),
			Rig.Driver()->Sequence()
				.KeyDown(Case.Key)
				.WaitSeconds(HoldTime * 0.5f)
				.Then([this, Under, Counter, Router, Handle](FDreamDriverContext&)
				{
					TestEqual(FString::Printf(TEXT("[%s] Halfway through the hold nothing has fired"), *Under), Counter->CallCount, 0);
					const float Progress = Router->GetHoldProgress(Handle);
					TestTrue(FString::Printf(TEXT("[%s] ...and the progress says about half (it says %.2f)"), *Under, Progress), Progress > 0.3f && Progress < 0.7f);
				})
				.WaitSeconds(HoldTime * 0.5f + 0.1f)
				.Then([this, Under, Counter](FDreamDriverContext&)
				{
					TestEqual(FString::Printf(TEXT("[%s] Past the hold time, with the key still down, the action has fired"), *Under), Counter->CallCount, 1);
				})
				.WaitSeconds(HoldTime)
				.KeyUp(Case.Key)
				.Perform());
		TestEqual(FString::Printf(TEXT("[%s] One hold fired once, held however long"), Case.Name), Counter->CallCount, 1);
		Router->UnregisterAction(Handle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHoldKeyLetGoEarlyTest,
	"DreamGUI.Navigation.Actions.LettingTheBoundKeyGoBeforeTheHoldTimeIsUpFiresNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamHoldKeyLetGoEarlyTest, "DreamGUI.Navigation.Actions.LettingTheBoundKeyGoBeforeTheHoldTimeIsUpFiresNothing", "[Nav][Animated]")

/*
 * Held for half its hold time and let go: a cancel. Nothing fires then or later, and the progress is back to none.
 */
bool FDreamHoldKeyLetGoEarlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackHoldCursorInteractionTestLocal;
	constexpr float HoldTime = 0.5f;
	for (const FHoldCase& Case : HoldCases())
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = Case.Host;
		FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
		Rig.BindTest(this);
		FHoldRig Hold;
		if (!TestTrue(FString::Printf(TEXT("[%s] The rig came up"), Case.Name), Rig.IsUsable()) || !RegisterHold(*this, Rig, HoldTime, Hold))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("[%s] Holding the key for half its hold time and letting go completes"), Case.Name),
			Rig.Driver()->Sequence()
				.KeyDown(Case.Key)
				.WaitSeconds(HoldTime * 0.5f)
				.KeyUp(Case.Key)
				.WaitSeconds(HoldTime)
				.Perform());
		TestEqual(FString::Printf(TEXT("[%s] Let go before the hold time, nothing fired"), Case.Name), Hold.Counter->CallCount, 0);
		TestEqual(FString::Printf(TEXT("[%s] ...and the progress is back to none"), Case.Name), Hold.Router->GetHoldProgress(Hold.Handle), 0.0f, 0.01f);
		Hold.Router->UnregisterAction(Hold.Handle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVirtualCursorDragTest,
	"DreamGUI.VirtualCursor.HoldingConfirmWhileTheStickMovesTheCursorDragsASlidersHandleAlong",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamVirtualCursorDragTest, "DreamGUI.VirtualCursor.HoldingConfirmWhileTheStickMovesTheCursorDragsASlidersHandleAlong", "[Nav][Animated]")

/*
 * The cursor put on a slider's handle at its left end, confirm held down, the stick pushed right, confirm let go: the
 * press under the cursor and the cursor's moves are a mouse's press and drag, so the handle went with the cursor and the
 * value with it, most of the way along.
 */
bool FDreamVirtualCursorDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackHoldCursorInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller.
	Rig.EnsureGameInputHost();
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("A slider with a handle"), Slider != nullptr && Slider->HandleNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("The slider starts at its left end"), Slider->GetValue(), 0.0f, 0.01f))
	{
		return false;
	}

	TestTrue(TEXT("The cursor on the handle, confirm held, the stick pushed right and confirm let go completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Slider->HandleNode.Get()))
			.ActivateVirtualCursor()
			.VirtualCursorPress()
			.VirtualCursorStick(FVector2D(1.0, 0.0), 0.25f)
			.VirtualCursorRelease()
			.Perform());
	TestTrue(FString::Printf(TEXT("The handle went with the cursor, the value most of the way along (it is %.2f)"), Slider->GetValue()),
		Slider->GetValue() > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewportResizeThenClickTest,
	"DreamGUI.Button.AfterTheViewportGrowsTheButtonAnchoredToItsCornerIsClickedWhereItIsDrawnNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamViewportResizeThenClickTest, "DreamGUI.Button.AfterTheViewportGrowsTheButtonAnchoredToItsCornerIsClickedWhereItIsDrawnNow", "[Pointer][Animated]")

/*
 * A button anchored to the screen's top-right corner, then the viewport made larger -- the size the canvas reads, put
 * through the same re-layout a resize of the game viewport causes (UDreamCanvas::CheckAndApplyViewportParameter). The
 * button is drawn further right; a click where it is drawn now clicks it, and a press where it used to be reaches nothing.
 */
bool FDreamViewportResizeThenClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackHoldCursorInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Button = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Corner"), nullptr, FVector2D(160.0, 40.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a button came up"), Button))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Button->SetAnchorMin(FVector2D(1.0, 1.0));
	Button->SetAnchorMax(FVector2D(1.0, 1.0));
	Button->SetAnchoredPosition(FVector2D(-120.0, -60.0));
	Rig.PumpFrames(2);
	FDreamElementRef ButtonElement = Rig.Driver()->Find(FDreamBy::Widget(Button));
	const TOptional<FVector2D> Before = ButtonElement->GetCentrePixel();
	if (!TestTrue(TEXT("The button is drawn near the top-right corner"), Before.IsSet() && Before->X > ViewportSize.X * 0.75))
	{
		return false;
	}

	const FIntPoint Larger(1600, 900);
	Rig.RootCanvas()->SetViewportSizeOverride(Larger);
	Rig.PumpFrames(2);
	const TOptional<FVector2D> After = ButtonElement->GetCentrePixel();
	if (!TestTrue(TEXT("After the viewport grew, the button is drawn as far from the new corner"),
		After.IsSet() && FMath::IsNearlyEqual(After->X, Before->X + (Larger.X - ViewportSize.X), 1.5) && FMath::IsNearlyEqual(After->Y, Before->Y, 1.5)))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the button where it is drawn now completes"), ButtonElement->Click());
	TestEqual(TEXT("...and clicks it once"), Listener->ClickedCount, 1);
	TestTrue(TEXT("A press and release where the button used to be completes"),
		Rig.Driver()->Sequence().WaitSeconds(1.0f).MoveToPixel(Before.GetValue()).Press().Release().Perform());
	TestEqual(TEXT("...and reaches nothing: the button was not clicked again"), Listener->ClickedCount, 1);
	return true;
}

#endif
