// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamInputKeySelector.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * THE KEY BINDER DISABLED, AND ON A SCALED CANVAS.
 *
 * UMG's InputKeySelector is a button that arms on a click (SInputKeySelector::OnClicked starts selecting) and takes the next
 * key as its binding. Disabled, it is a disabled button: Slate's hit test cuts a click's or a finger's path at it
 * (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp), so nothing arms it and the key pressed next binds
 * nothing; the D-pad passes over it (FHittestGrid::FindNextFocusableWidget skips a widget that is not enabled), so the pad's
 * confirm presses whatever it landed on instead. Enabled again, each of them arms it.
 *
 * On a canvas scaled to half and to double, the click, the finger and the D-pad find it where it is drawn.
 *
 * The keys go through the player's controller (the standalone input actor's host), which is where an armed selector's
 * capture agent listens on top of the input stack.
 */
namespace DreamInputKeySelectorCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };
	const FVector2D SelectorSize(200.0, 50.0);
	const FVector2D ButtonSize(160.0, 50.0);

	FDreamRigOptions ActorHostOptions()
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = EDreamRigInputHost::StandaloneActor;
		return Options;
	}

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options = ActorHostOptions();
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

	UDreamInputKeySelector* MakeObservedSelector(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener, const FVector2D& InPosition)
	{
		UDreamInputKeySelector* Selector = InRig.MakeControl<UDreamInputKeySelector>(TEXT("Jump"), nullptr, SelectorSize, InPosition);
		if (Selector != nullptr && InListener != nullptr)
		{
			Selector->OnKeySelected.AddDynamic(InListener, &UDreamTextInteractionListener::HandleKeySelected);
			Selector->OnIsListeningChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleListeningChanged);
		}
		return Selector;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	/** Player 0's focus put on InButton's face the way a directional move would. */
	bool FocusOn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton, const FString& InWhat)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(InWhat, Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0));
	}

	float DoubleClickTimeOf(const FDreamDriverRig& InRig)
	{
		return InRig.EventSystem() != nullptr ? InRig.EventSystem()->GetDoubleClickTime() : 0.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageKeySelectorDisabledClickTest,
	"DreamGUI.InputKeySelector.ADisabledSelectorIsNotArmedByAClickAndBindsNoKeyUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageKeySelectorDisabledClickTest, "DreamGUI.InputKeySelector.ADisabledSelectorIsNotArmedByAClickAndBindsNoKeyUntilItIsEnabled", "[Pointer][Text][Disabled]")

/*
 * Bound to G and disabled: a click does not arm it, and F pressed after binds nothing -- G stays, nothing is announced.
 * Enabled again, the same click arms it and F becomes the binding.
 */
bool FDreamCoverageKeySelectorDisabledClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ActorHostOptions());
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamInputKeySelector* Selector = Rig.IsUsable() ? MakeObservedSelector(Rig, Listener.Get(), FVector2D::ZeroVector) : nullptr;
	if (!TestNotNull(TEXT("The rig and the selector came up"), Selector))
	{
		return false;
	}
	Selector->SetSelectedKey(EKeys::G);
	Selector->SetIsEnabled(false);
	Rig.PumpFrames(1);
	Listener->KeySelectedCount = 0;
	Listener->ListeningChangedCount = 0;

	FDreamElementRef SelectorElement = Rig.Driver()->Find(FDreamBy::Widget(Selector));
	TestTrue(TEXT("Clicking the disabled selector completes"), SelectorElement->Click());
	TestFalse(TEXT("The click did not arm it"), Selector->GetIsListening());
	TestEqual(TEXT("...and no armed state was reported"), Listener->ListeningChangedCount, 0);
	TestTrue(TEXT("Pressing F completes"), Rig.Driver()->Sequence().Key(EKeys::F).Perform());
	TestTrue(TEXT("F bound nothing: the selector still holds G"), Selector->GetSelectedKey() == EKeys::G);
	TestEqual(TEXT("...and announced nothing"), Listener->KeySelectedCount, 0);

	Selector->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking it again once it is enabled completes"),
		Rig.Driver()->Sequence().WaitSeconds(DoubleClickTimeOf(Rig) + 0.1f).Click(FDreamBy::Widget(Selector)).Perform());
	if (!TestTrue(TEXT("Enabled, the click armed it"), Selector->GetIsListening()))
	{
		return false;
	}
	TestTrue(TEXT("Pressing F completes"), Rig.Driver()->Sequence().Key(EKeys::F).Perform());
	TestTrue(TEXT("F is the new binding"), Selector->GetSelectedKey() == EKeys::F);
	TestEqual(TEXT("...announced once"), Listener->KeySelectedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageKeySelectorDisabledTapTest,
	"DreamGUI.InputKeySelector.AFingerTapOnADisabledSelectorDoesNotArmItUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageKeySelectorDisabledTapTest, "DreamGUI.InputKeySelector.AFingerTapOnADisabledSelectorDoesNotArmItUntilItIsEnabled", "[Touch][Disabled]")

/* A tap is the button's press and release (Slate hands an unanswered touch on as a mouse press), cut off a disabled one. */
bool FDreamCoverageKeySelectorDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ActorHostOptions());
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamInputKeySelector* Selector = Rig.IsUsable() ? MakeObservedSelector(Rig, Listener.Get(), FVector2D::ZeroVector) : nullptr;
	if (!TestNotNull(TEXT("The rig and the selector came up"), Selector))
	{
		return false;
	}
	Selector->SetIsEnabled(false);
	Rig.PumpFrames(1);
	Listener->ListeningChangedCount = 0;

	FDreamElementRef SelectorElement = Rig.Driver()->Find(FDreamBy::Widget(Selector));
	TestTrue(TEXT("A finger tapped on the disabled selector completes"), SelectorElement->Tap(0));
	TestFalse(TEXT("The tap did not arm it"), Selector->GetIsListening());
	TestEqual(TEXT("...and no armed state was reported"), Listener->ListeningChangedCount, 0);

	Selector->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Waiting out the double-click time completes"), Rig.Driver()->Sequence().WaitSeconds(DoubleClickTimeOf(Rig) + 0.1f).Perform());
	TestTrue(TEXT("A finger tapped on it once it is enabled completes"), SelectorElement->Tap(1));
	TestTrue(TEXT("Enabled, the tap armed it"), Selector->GetIsListening());
	TestEqual(TEXT("...reported once"), Listener->ListeningChangedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageKeySelectorDisabledPadTest,
	"DreamGUI.InputKeySelector.TheDPadPassesOverADisabledSelectorAndConfirmArmsItOnceItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageKeySelectorDisabledPadTest, "DreamGUI.InputKeySelector.TheDPadPassesOverADisabledSelectorAndConfirmArmsItOnceItIsEnabled", "[Nav][Disabled]")

/*
 * A button, the selector, a button, in a row. Disabled, the selector is passed over: the D-pad right from the first button
 * lands on the last, and the confirm there arms nothing. Enabled again, the D-pad right from the first button stops on the
 * selector and the confirm arms it.
 */
bool FDreamCoverageKeySelectorDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ActorHostOptions());
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamButton* First = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("First"), nullptr, ButtonSize, FVector2D(-300.0, 0.0)) : nullptr;
	UDreamInputKeySelector* Selector = First != nullptr ? MakeObservedSelector(Rig, Listener.Get(), FVector2D::ZeroVector) : nullptr;
	UDreamButton* Last = Selector != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("Last"), nullptr, ButtonSize, FVector2D(300.0, 0.0)) : nullptr;
	if (!TestTrue(TEXT("The rig, two buttons and the selector between them came up"), Last != nullptr && First->FaceNode != nullptr))
	{
		return false;
	}
	Selector->SetIsEnabled(false);
	Rig.PumpFrames(1);

	if (!FocusOn(*this, Rig, First, TEXT("Player 0's focus can be put on the first button")))
	{
		return false;
	}
	TestTrue(TEXT("The D-pad right completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
	TestTrue(TEXT("The D-pad passed over the disabled selector to the last button"), IsPartOf(FocusOf(Rig), Last));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	TestFalse(TEXT("The confirm armed nothing"), Selector->GetIsListening());

	Selector->SetIsEnabled(true);
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, First, TEXT("Player 0's focus can be put back on the first button")))
	{
		return false;
	}
	TestTrue(TEXT("The D-pad right completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
	if (!TestTrue(TEXT("Enabled, the selector is the next stop"), IsPartOf(FocusOf(Rig), Selector)))
	{
		return false;
	}
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	TestTrue(TEXT("The confirm armed it"), Selector->GetIsListening());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageKeySelectorScaledClickTest,
	"DreamGUI.InputKeySelector.OnACanvasScaledToHalfAndToDoubleAClickArmsTheSelectorWhereItIsDrawnAndTheNextKeyIsBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageKeySelectorScaledClickTest, "DreamGUI.InputKeySelector.OnACanvasScaledToHalfAndToDoubleAClickArmsTheSelectorWhereItIsDrawnAndTheNextKeyIsBound", "[Pointer][Text][Scaled]")

/*
 * The selector off the canvas's centre, a button beside it. A click where the selector is drawn arms it -- the button beside
 * it is not clicked -- and F pressed next is bound, at half and at double.
 */
bool FDreamCoverageKeySelectorScaledClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		UDreamInputKeySelector* Selector = MakeObservedSelector(Rig, Listener.Get(), FVector2D(-120.0, 60.0));
		UDreamButton* Beside = Rig.MakeControl<UDreamButton>(TEXT("Beside"), nullptr, ButtonSize, FVector2D(120.0, 60.0));
		if (!TestTrue(At(Scale, TEXT("The selector and a button beside it came up")), Selector != nullptr && Beside != nullptr))
		{
			return false;
		}
		Beside->OnClicked.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleClicked);
		Rig.PumpFrames(1);

		TestTrue(At(Scale, TEXT("Clicking the selector completes")), Rig.Driver()->Find(FDreamBy::Widget(Selector))->Click());
		TestTrue(At(Scale, TEXT("The click armed the selector")), Selector->GetIsListening());
		TestEqual(*At(Scale, TEXT("...and clicked nothing beside it")), Listener->ClickedCount, 0);
		TestTrue(At(Scale, TEXT("Pressing F completes")), Rig.Driver()->Sequence().Key(EKeys::F).Perform());
		TestTrue(At(Scale, TEXT("F is the new binding")), Selector->GetSelectedKey() == EKeys::F);
		TestEqual(*At(Scale, TEXT("...announced once")), Listener->KeySelectedCount, 1);
		TestFalse(At(Scale, TEXT("...and the selector disarmed")), Selector->GetIsListening());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageKeySelectorScaledTapTest,
	"DreamGUI.InputKeySelector.OnACanvasScaledToHalfAndToDoubleAFingerTapArmsTheSelectorWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageKeySelectorScaledTapTest, "DreamGUI.InputKeySelector.OnACanvasScaledToHalfAndToDoubleAFingerTapArmsTheSelectorWhereItIsDrawn", "[Touch][Scaled]")

/* The same layout, a finger: the tap where the selector is drawn arms it and taps nothing beside it, at half and at double. */
bool FDreamCoverageKeySelectorScaledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		UDreamInputKeySelector* Selector = MakeObservedSelector(Rig, Listener.Get(), FVector2D(-120.0, 60.0));
		UDreamButton* Beside = Rig.MakeControl<UDreamButton>(TEXT("Beside"), nullptr, ButtonSize, FVector2D(120.0, 60.0));
		if (!TestTrue(At(Scale, TEXT("The selector and a button beside it came up")), Selector != nullptr && Beside != nullptr))
		{
			return false;
		}
		Beside->OnClicked.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleClicked);
		Rig.PumpFrames(1);

		TestTrue(At(Scale, TEXT("A finger tapped on the selector completes")), Rig.Driver()->Find(FDreamBy::Widget(Selector))->Tap(0));
		TestTrue(At(Scale, TEXT("The tap armed the selector")), Selector->GetIsListening());
		TestEqual(*At(Scale, TEXT("...reported once")), Listener->ListeningChangedCount, 1);
		TestEqual(*At(Scale, TEXT("...and tapped nothing beside it")), Listener->ClickedCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageKeySelectorScaledPadTest,
	"DreamGUI.InputKeySelector.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheSelectorAndConfirmArmsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageKeySelectorScaledPadTest, "DreamGUI.InputKeySelector.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheSelectorAndConfirmArmsIt", "[Nav][Scaled]")

/*
 * A button, the selector below it, and another button further below. The D-pad down from the upper button stops on the
 * selector -- the nearer one that way as drawn -- and the confirm arms it, at half and at double.
 */
bool FDreamCoverageKeySelectorScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, ButtonSize, FVector2D(0.0, 110.0));
		UDreamInputKeySelector* Selector = Above != nullptr ? MakeObservedSelector(Rig, Listener.Get(), FVector2D(0.0, 10.0)) : nullptr;
		UDreamButton* Far = Selector != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("Far"), nullptr, ButtonSize, FVector2D(0.0, -140.0)) : nullptr;
		if (!TestTrue(At(Scale, TEXT("Two buttons and the selector between them came up")), Far != nullptr && Above->FaceNode != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!FocusOn(*this, Rig, Above, At(Scale, TEXT("Player 0's focus can be put on the upper button"))))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("The D-pad down completes")), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform());
		if (!TestTrue(At(Scale, TEXT("The D-pad down stopped on the selector, not the button further below")), IsPartOf(FocusOf(Rig), Selector)))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("Pressing and releasing the pad's confirm completes")),
			Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
		TestTrue(At(Scale, TEXT("The confirm armed the selector")), Selector->GetIsListening());
	}
	return true;
}

#endif
