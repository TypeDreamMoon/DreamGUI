// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A CONTROL ON THE RIG IS A CONTROL IN A GAME.
 *
 * Every interaction test for a control starts by putting one on the rig, so the one thing that must
 * not differ from the runtime is how it got there. A control is a user widget with a tree of parts
 * under it, and a part that was never registered is a part the raycaster never walks over: the
 * control would be laid out, drawn, and deaf. These two pin the two halves of "got there properly" --
 * the control is live enough that a real click through the real pipeline reaches its public event,
 * and it hangs where it was told to hang.
 */
namespace DreamDriverRigTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRigMakeControlClickTest,
	"DreamGUI.Driver.Rig.AButtonMadeOnTheRigIsFoundByNameAndAClickReachesItsOnClicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRigMakeControlClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRigTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 80.0));
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button))
	{
		return false;
	}
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleClicked);
	Rig.PumpFrames(1);

	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Name(TEXT("Play")));
	if (!TestTrue(TEXT("The control is found by the name it was made with"), Play->Exists()))
	{
		return false;
	}
	TestSamePtr(TEXT("And what is found is the control itself, not one of its parts"),
		Play->GetWidget(), static_cast<UDreamWidget*>(Button));

	// The click lands on a PART -- the face the button draws -- and has to climb to the behaviour that
	// turns it into the control's event. Every link in that chain has to be registered for this to be
	// one rather than zero.
	TestTrue(TEXT("Clicking it completes"), Play->Click());
	TestEqual(TEXT("The button's own OnClicked fired exactly once"), Listener->ClickedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRigMakeControlParentTest,
	"DreamGUI.Driver.Rig.AControlMadeUnderAPanelHangsFromThatPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRigMakeControlParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRigTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Panel = Rig.MakeWidget(TEXT("Panel"), nullptr, FVector2D(600.0, 400.0));
	UDreamButton* Nested = Rig.MakeControl<UDreamButton>(TEXT("Nested"), Panel, FVector2D(200.0, 80.0));
	// The class-at-run-time road, which a Blueprint subclass would take, under the root this time.
	UDreamWidget* Loose = Rig.MakeControl(UDreamButton::StaticClass(), TEXT("Loose"), nullptr, FVector2D(200.0, 80.0));
	if (!TestNotNull(TEXT("A control can be made under a panel"), Nested)
		|| !TestNotNull(TEXT("A control can be made from a class chosen at run time"), Loose))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestSamePtr(TEXT("A control made under a panel has that panel as its parent"),
		Nested->GetParent(), Panel);
	TestSamePtr(TEXT("A control made with no parent hangs from the rig's root"),
		Loose->GetParent(), Rig.Root());
	// Found where the path says, which is the tree telling the same story as the parent pointer.
	TestSamePtr(TEXT("The nested control is where a path through its panel says it is"),
		Rig.Driver()->Find(FDreamBy::Path(TEXT("Panel/Nested")))->GetWidget(), static_cast<UDreamWidget*>(Nested));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRigGameInputHostTest,
	"DreamGUI.Driver.Rig.TheGameInputHostIsOneFindablePlayerControllerWithItsInputUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRigGameInputHostTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRigTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();

	Rig.EnsureGameInputHost();

	// FINDABLE is the whole point. A text field's key agent asks UGameplayStatics for player 0 and
	// enables its input on whatever comes back; a controller that exists but is not on the world's
	// list comes back as nothing, and the field then binds its keys through a null InputComponent.
	APlayerController* FirstController = World->GetFirstPlayerController();
	if (!TestNotNull(TEXT("The world has a first player controller"), FirstController))
	{
		return false;
	}
	TestSamePtr(TEXT("And it is the one handed to anything asking for player 0"),
		UGameplayStatics::GetPlayerController(World, 0), FirstController);
	TestNotNull(TEXT("With its input system up"), FirstController->PlayerInput.Get());
	TestSamePtr(TEXT("The event system is registered where the runtime looks it up"),
		UDreamEventSystem::GetDreamEventSystemInstance(World, 0), Rig.EventSystem());

	// Idempotent: every MakeControl asks for the host, and a second controller would be a second
	// player 0 candidate with an input stack nothing drives.
	Rig.EnsureGameInputHost();
	TestEqual(TEXT("Asking again leaves exactly one player controller"), World->GetNumPlayerControllers(), 1);
	TestSamePtr(TEXT("And it is still the same one"), World->GetFirstPlayerController(), FirstController);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverRigBeginsPlayTest,
	"DreamGUI.Driver.Rig.TheRigsWorldHasBegunPlaySoAControlsBehavioursStartOnTheNextFrameAndTickOncePerFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverRigBeginsPlayTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverRigTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has a UI manager"), Manager))
	{
		return false;
	}
	// The world a control is built into has already begun play, as a game's has by the time any screen
	// is made at runtime.
	TestTrue(TEXT("The rig's world has begun play before any control exists"), Manager->HasBegunPlay());

	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 80.0));
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button))
	{
		return false;
	}
	TestTrue(TEXT("A control made in a begun world has begun play itself"), Button->HasBegunPlay());

	// A behaviour on a live control wakes the moment it is added, and starts on the next frame: Start
	// is run by the UI manager's frame, not by whoever added it.
	UDreamTextLifecycleProbe* Probe = Button->AddComponent<UDreamTextLifecycleProbe>();
	if (!TestNotNull(TEXT("A behaviour can be added to the control"), Probe))
	{
		return false;
	}
	TestEqual(TEXT("It woke as it was added"), Probe->AwakeCount, 1);
	TestEqual(TEXT("But has not started before a frame has passed"), Probe->StartCount, 0);

	Rig.PumpFrames(1);
	TestEqual(TEXT("One frame later it has started"), Probe->StartCount, 1);
	TestEqual(TEXT("And ticked in that same frame"), Probe->TickCount, 1);

	// Once per frame and no more. The pump runs the UI manager's frame exactly once; anything else
	// that also drove behaviours would show up here as a count ahead of the frames.
	Rig.PumpFrames(2);
	TestEqual(TEXT("It starts only once"), Probe->StartCount, 1);
	TestEqual(TEXT("And ticks once per pumped frame"), Probe->TickCount, 3);

	return true;
}

#endif
