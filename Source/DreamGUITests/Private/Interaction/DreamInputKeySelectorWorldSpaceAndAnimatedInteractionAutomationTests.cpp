// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamInputKeySelector.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamTextInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * THE KEY BINDER IN THE WORLD, AND A FINGER AND THE PAD ON IT AS SHIPPED.
 *
 * UMG's InputKeySelector is a button that arms on a click (SInputKeySelector::OnClicked starts selecting) and takes the next
 * key as its binding (OnKeySelected), then disarms. Being a button, it arms on whatever presses it: a click through a widget
 * component's interaction, a finger -- FSlateApplication hands an unanswered touch on as a mouse press -- or the pad's accept
 * once the focus is on it (SButton presses on the accept key). DreamGUI's D-pad reaches it like any stop, on a panel in the
 * level by its directional scan among the world-space widgets.
 *
 * The keys go through the player's controller (the standalone input actor's host), where an armed selector's capture agent
 * listens on top of the input stack, so the key pressed after arming is heard where a game hears it.
 */
namespace DreamInputKeySelectorWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	const FVector2D SelectorSize(200.0, 50.0);
	const FVector2D ButtonSize(120.0, 50.0);

	/** The selector, its two events going to InListener, under InParent (the rig's root when null), bound to G to begin with. */
	UDreamInputKeySelector* MakeObservedSelector(FDreamDriverRig& InRig, UDreamWidget* InParent, UDreamTextInteractionListener* InListener)
	{
		UDreamInputKeySelector* Selector = InRig.MakeControl<UDreamInputKeySelector>(TEXT("Jump"), InParent, SelectorSize);
		if (Selector != nullptr && InListener != nullptr)
		{
			Selector->SetSelectedKey(EKeys::G);
			Selector->OnKeySelected.AddDynamic(InListener, &UDreamTextInteractionListener::HandleKeySelected);
			Selector->OnIsListeningChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleListeningChanged);
		}
		return Selector;
	}

	/** The scene: a rig with the standalone input actor's host, and -- on a panel when bInOnPanel -- the selector. */
	struct FSelectorScene
	{
		UDreamWidget* Parent = nullptr;
		UDreamInputKeySelector* Selector = nullptr;
	};

	bool MakeScene(FAutomationTestBase& InTest, FDreamDriverRig& InRig, bool bInOnPanel, UDreamTextInteractionListener* InListener, FSelectorScene& OutScene)
	{
		if (!InTest.TestTrue(TEXT("The rig came up"), InRig.IsUsable()))
		{
			return false;
		}
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, InRig);
			if (!Stage.IsReady())
			{
				return false;
			}
			OutScene.Parent = Stage.Panel;
		}
		OutScene.Selector = MakeObservedSelector(InRig, OutScene.Parent, InListener);
		if (!InTest.TestNotNull(TEXT("The selector came up"), OutScene.Selector))
		{
			return false;
		}
		InRig.PumpFrames(2);
		InListener->KeySelectedCount = 0;
		InListener->ListeningChangedCount = 0;
		return InTest.TestTrue(TEXT("The selector is somewhere the pointer can reach"), PixelOf(InRig, OutScene.Selector).IsSet());
	}

	/** F pressed through the controller, and what the selector made of it: bound, announced once, disarmed. */
	void PressFAndExpectItBound(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const UDreamInputKeySelector* InSelector,
		const UDreamTextInteractionListener* InListener)
	{
		InTest.TestTrue(TEXT("Pressing F completes"), PressKey(InRig, EKeys::F));
		InTest.TestTrue(TEXT("F is the new binding"), InSelector->GetSelectedKey() == EKeys::F);
		InTest.TestEqual(TEXT("...announced once"), InListener->KeySelectedCount, 1);
		InTest.TestFalse(TEXT("...and binding it disarmed the selector"), InSelector->GetIsListening());
	}

	/** A finger tapped on the selector arms it, and F pressed next becomes its binding. */
	bool RunTapArms(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions(EDreamRigInputHost::StandaloneActor));
		Rig.BindTest(&InTest);
		FSelectorScene Scene;
		if (!MakeScene(InTest, Rig, bInOnPanel, Listener.Get(), Scene))
		{
			return false;
		}

		InTest.TestTrue(TEXT("A finger tapped on the selector completes"), Rig.Driver()->Find(FDreamBy::Widget(Scene.Selector))->Tap(0));
		if (!InTest.TestTrue(TEXT("The tap armed it"), Scene.Selector->GetIsListening()))
		{
			return false;
		}
		InTest.TestEqual(TEXT("...reported once"), Listener->ListeningChangedCount, 1);
		InTest.TestTrue(TEXT("...and it still holds G until a key comes"), Scene.Selector->GetSelectedKey() == EKeys::G);
		PressFAndExpectItBound(InTest, Rig, Scene.Selector, Listener.Get());
		return true;
	}

	/**
	 * A button, the selector, a button, in a row. The focus on the first button, the D-pad right stops on the selector -- the
	 * nearer of the two that way -- the pad's confirm arms it, and F pressed next becomes its binding.
	 */
	bool RunPadArms(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions(EDreamRigInputHost::StandaloneActor));
		Rig.BindTest(&InTest);
		FSelectorScene Scene;
		if (!MakeScene(InTest, Rig, bInOnPanel, Listener.Get(), Scene))
		{
			return false;
		}
		UDreamButton* First = Rig.MakeControl<UDreamButton>(TEXT("First"), Scene.Parent, ButtonSize, FVector2D(-200.0, 0.0));
		UDreamButton* Last = Rig.MakeControl<UDreamButton>(TEXT("Last"), Scene.Parent, ButtonSize, FVector2D(200.0, 0.0));
		if (!InTest.TestTrue(TEXT("A button either side of the selector came up"), First != nullptr && Last != nullptr && First->FaceNode != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("Player 0's focus can be put on the first button"), PutFocusOn(Rig, First->FaceNode)))
		{
			return false;
		}

		InTest.TestTrue(TEXT("The D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		if (!InTest.TestTrue(TEXT("The D-pad right stopped on the selector, not on the button past it"), FocusIsIn(Rig, Scene.Selector)))
		{
			return false;
		}
		InTest.TestFalse(TEXT("Landing on it did not arm it"), Scene.Selector->GetIsListening());
		InTest.TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), PadConfirm(Rig));
		if (!InTest.TestTrue(TEXT("The confirm armed it"), Scene.Selector->GetIsListening()))
		{
			return false;
		}
		PressFAndExpectItBound(InTest, Rig, Scene.Selector, Listener.Get());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldKeySelectorClickTest,
	"DreamGUI.InputKeySelector.OnAWorldPanelAClickArmsItAndTheNextKeyPressedBecomesTheBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldKeySelectorClickTest, "DreamGUI.InputKeySelector.OnAWorldPanelAClickArmsItAndTheNextKeyPressedBecomesTheBinding", "[Pointer][Text][World]")

/*
 * Bound to G, on a panel: a click through the world pointer arms it, and F pressed through the controller becomes the
 * binding, announced once; the armed state went on and came off.
 */
bool FDreamWorldKeySelectorClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorWorldTestLocal;
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions(EDreamRigInputHost::StandaloneActor));
	Rig.BindTest(this);
	FSelectorScene Scene;
	if (!MakeScene(*this, Rig, true, Listener.Get(), Scene))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the selector on the panel completes"), Rig.Driver()->Find(FDreamBy::Widget(Scene.Selector))->Click());
	if (!TestTrue(TEXT("The click through the world pointer armed it"), Scene.Selector->GetIsListening()))
	{
		return false;
	}
	PressFAndExpectItBound(*this, Rig, Scene.Selector, Listener.Get());
	TestEqual(TEXT("The armed state was reported going on and coming off"), Listener->ListeningChangedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldKeySelectorTapTest,
	"DreamGUI.InputKeySelector.OnAWorldPanelAFingerTapArmsItAndTheNextKeyPressedBecomesTheBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldKeySelectorTapTest, "DreamGUI.InputKeySelector.OnAWorldPanelAFingerTapArmsItAndTheNextKeyPressedBecomesTheBinding", "[Touch][Text][World]")

/* The finger's ray from the camera onto the panel lands on the selector, and the tap is the button's press and release. */
bool FDreamWorldKeySelectorTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorWorldTestLocal;
	return RunTapArms(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldKeySelectorPadTest,
	"DreamGUI.InputKeySelector.OnAWorldPanelTheDPadReachesTheSelectorAndThePadsConfirmArmsItForTheNextKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldKeySelectorPadTest, "DreamGUI.InputKeySelector.OnAWorldPanelTheDPadReachesTheSelectorAndThePadsConfirmArmsItForTheNextKey", "[Nav][Text][World]")

bool FDreamWorldKeySelectorPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorWorldTestLocal;
	return RunPadArms(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedKeySelectorTapTest,
	"DreamGUI.InputKeySelector.AFingerTapArmsItAndTheNextKeyPressedBecomesTheBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedKeySelectorTapTest, "DreamGUI.InputKeySelector.AFingerTapArmsItAndTheNextKeyPressedBecomesTheBinding", "[Touch][Text][Animated]")

/* As shipped on the screen: the tap arms it like the click, and the next key is the binding. */
bool FDreamAnimatedKeySelectorTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorWorldTestLocal;
	return RunTapArms(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedKeySelectorPadTest,
	"DreamGUI.InputKeySelector.TheDPadReachesTheSelectorAndThePadsConfirmArmsItForTheNextKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedKeySelectorPadTest, "DreamGUI.InputKeySelector.TheDPadReachesTheSelectorAndThePadsConfirmArmsItForTheNextKey", "[Nav][Text][Animated]")

/* As shipped on the screen: a focus stop like any button, armed by the pad's accept. */
bool FDreamAnimatedKeySelectorPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorWorldTestLocal;
	return RunPadArms(*this, false);
}

#endif
