// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Components/BoxComponent.h"
#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "GameFramework/Actor.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

#include "DreamScopedWorld.h"

/*
 * WHAT A WORLD POINTER CANNOT SEE PAST, IT CANNOT CLICK PAST -- WITHOUT ANYONE ASKING.
 *
 * UDreamWorldSpaceRaycaster::bOccludeByWorld traces the world along the pointer's ray as well as the
 * panels, on the collision channel the raycaster's TraceChannel stands for (TraceTypeQuery1, the
 * engine's Visibility channel, unless it is changed). The nearest blocking hit carries no widget and
 * wins on distance, so a solid object between the eye and a panel takes the pointer off the panel; and
 * the actor behind the hit is sent the pointer's events, which is the only road a render-target surface
 * has to its pointer. It is on by default, and these tests pin what "by default" covers: the raycaster
 * UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer makes for a player, and the driver's world
 * pointer left exactly as it was attached, both trace the world with nobody setting the flag. Turned
 * off, a click goes straight through the object to the panel -- the old default, and what a project
 * gets back by unticking it.
 *
 * The expected behaviour is UMG's: a UWidgetInteractionComponent traces on Visibility unless told
 * otherwise and stops at the first blocking hit that is not a widget component, whatever is behind it.
 *
 * The walls here are box components given a blocking response on purpose. A shape component's own
 * default profile, OverlapAllDynamic, only overlaps Visibility -- which is rightly not an occluder, the
 * trace being a single one -- so a box left at its defaults would prove nothing about occlusion.
 *
 * The geometry is the other world-space tests': the eye at the origin looking down +X with ninety
 * degrees across a 1280x720 viewport, a panel 300 cm ahead facing it, and here a metre-wide wall
 * halfway, its near face 100 cm out -- in front of every pixel of the button in the panel's middle.
 */
namespace DreamDriverWorldOcclusionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Where the wall stands: centred 150 cm down the view axis, 100 cm across, so its near face is at 100 cm. */
	const FVector WallCentre(150.0, 0.0, 0.0);
	/** The same wall lifted ten metres: out of every ray these tests cast, and still in the world. */
	const FVector WallOutOfTheWay(150.0, 0.0, 1000.0);

	/** Past the event system's double-click time (0.3 s) at the pump's 1/60 s frames, so a later press starts a click run of its own. */
	constexpr int32 FramesPastTheDoubleClickTime = 30;

	FMinimalViewInfo EyeAtTheOrigin()
	{
		return DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize);
	}

	/** 300 cm down the view axis, unturned: a panel or a surface here faces the eye. */
	FTransform AheadOfTheEye()
	{
		return FTransform(FVector(300.0, 0.0, 0.0));
	}

	/**
	 * A solid box in InWorld at InLocation: query only, a world-static object blocking every channel --
	 * the responses of the engine's BlockAll profile, without a physics body no query needs -- except
	 * InLetThrough when one is given, which it ignores. On its own actor, so it is never the actor the
	 * pointer rides on (the one RaycastWorld skips).
	 */
	UBoxComponent* MakeWall(UWorld* InWorld, const FVector& InLocation, TOptional<ECollisionChannel> InLetThrough = TOptional<ECollisionChannel>())
	{
		if (InWorld == nullptr)
		{
			return nullptr;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		AActor* WallActor = InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (WallActor == nullptr)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(WallActor, TEXT("Wall"));
		Box->SetBoxExtent(FVector(50.0));
		// Responses before registering, so the body is made with them rather than updated afterwards.
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		if (InLetThrough.IsSet())
		{
			Box->SetCollisionResponseToChannel(InLetThrough.GetValue(), ECR_Ignore);
		}
		WallActor->SetRootComponent(Box);
		WallActor->AddInstanceComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(InLocation);
		return Box;
	}

	/** The world pointer and one panel facing it with a button in its middle whose press, click and hover go to a listener. */
	struct FOcclusionStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;
		UDreamButton* Button = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr && Button != nullptr; }
	};

	FOcclusionStage SetUpStage(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FOcclusionStage Stage;
		// Attached the way every world-space test attaches it and then left alone: nothing in this file
		// writes bOccludeByWorld unless writing it is what the test is about.
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
		// No hit-testable background, so the button's parts are the only things on the panel to hit.
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), AheadOfTheEye(), FVector2D(400.0, 300.0));
		if (Stage.Panel != nullptr)
		{
			Stage.Button = InRig.MakeControl<UDreamButton>(TEXT("Play"), Stage.Panel, FVector2D(120.0, 60.0));
		}
		if (Stage.Button != nullptr && InListener != nullptr)
		{
			Stage.Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Stage.Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Stage.Button->OnHovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleHovered);
		}
		InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer);
		InTest.TestNotNull(TEXT("A world-space panel was built in front of it"), Stage.Panel);
		InTest.TestNotNull(TEXT("A button was made in the middle of the panel"), Stage.Button);
		return Stage;
	}

	/** The actor behind the world hit the rig's pointer is over: what the pointer points at when it is not a widget. */
	AActor* HoveredWorldTarget(const FDreamDriverRig& InRig)
	{
		const UDreamEventSystem* Events = InRig.EventSystem();
		return Events != nullptr ? Events->GetHoveredWorldTarget(0) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldOcclusionManagerDefaultTest,
	"DreamGUI.Driver.WorldSpace.Occlusion.TheWorldPointerTheManagerMakesForAPlayerTracesTheWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldOcclusionManagerDefaultTest::RunTest(const FString& Parameters)
{
	// The raycaster a project gets without placing one is the manager's, made with a bare NewObject that
	// sets nothing but the user index -- so the class default IS that raycaster's setting. Asserted on the
	// class first, then on the one the manager actually made, so that giving the manager an opinion of
	// its own later cannot go unnoticed.
	const UDreamWorldSpaceRaycaster* ClassDefault = GetDefault<UDreamWorldSpaceRaycaster>();
	TestTrue(TEXT("A world-space raycaster traces the world unless it is told not to"), ClassDefault->GetOccludeByWorld());
	TestEqual(TEXT("... on TraceTypeQuery1 unless it is told otherwise"),
		static_cast<int32>(ClassDefault->GetTraceChannel().GetValue()), static_cast<int32>(TraceTypeQuery1));
	TestEqual(TEXT("... which is the engine's Visibility channel"),
		static_cast<int32>(UEngineTypes::ConvertToCollisionChannel(TraceTypeQuery1)), static_cast<int32>(ECC_Visibility));

	DreamTests::FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world"), Scope.World))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("A manager for it"), Manager))
	{
		return false;
	}
	// An event system the player already has, so EnsureInteractionForPlayer has nothing to spawn: the one
	// in project settings is a content Blueprint, and loading it is not what this is about.
	AActor* EventSystemHost = Scope.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("An actor for the player's event system"), EventSystemHost))
	{
		return false;
	}
	UDreamEventSystem* EventSystem = NewObject<UDreamEventSystem>(EventSystemHost);
	EventSystem->SetUserIndex(0);
	EventSystemHost->AddInstanceComponent(EventSystem);
	EventSystem->RegisterComponent();

	// What a world widget component asks for when it begins play.
	Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::World);
	UDreamWorldSpaceRaycaster* Made = nullptr;
	if (AActor* InteractionHost = Manager->GetInteractionHost(0))
	{
		for (UActorComponent* Component : InteractionHost->GetComponents())
		{
			if (UDreamWorldSpaceRaycaster* Raycaster = Cast<UDreamWorldSpaceRaycaster>(Component))
			{
				Made = Raycaster;
				break;
			}
		}
	}
	if (!TestNotNull(TEXT("The manager made a world-space raycaster for player 0"), Made))
	{
		return false;
	}
	TestTrue(TEXT("... and it traces the world, with nobody having asked it to"), Made->GetOccludeByWorld());
	TestEqual(TEXT("... on TraceTypeQuery1"), static_cast<int32>(Made->GetTraceChannel().GetValue()), static_cast<int32>(TraceTypeQuery1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldOcclusionDefaultBlocksTest,
	"DreamGUI.Driver.WorldSpace.Occlusion.ByDefaultASolidObjectBetweenTheEyeAndAPanelTakesTheClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldOcclusionDefaultBlocksTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldOcclusionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const FOcclusionStage Stage = SetUpStage(*this, Rig, Listener.Get());
	UBoxComponent* Wall = MakeWall(Rig.GetWorld(), WallCentre);
	if (!Stage.IsReady() || !TestNotNull(TEXT("A wall stands between the eye and the panel"), Wall))
	{
		return false;
	}
	TestTrue(TEXT("The world pointer, attached and left alone, traces the world"), Stage.Pointer->GetOccludeByWorld());
	Rig.PumpFrames(2);

	// Aimed at the button: the ray through its pixel meets the wall's near face at 100 cm, long before
	// the panel at 300, so the nearest hit is the wall's and it carries no widget.
	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Widget(Stage.Button));
	TestTrue(TEXT("Clicking at the button's pixel completes"), Play->Click());
	TestEqual(TEXT("The button behind the wall was not pressed"), Listener->PressedCount, 0);
	TestEqual(TEXT("... nor clicked"), Listener->ClickedCount, 0);
	TestEqual(TEXT("... nor so much as hovered"), Listener->HoveredCount, 0);
	// What the pointer was on instead, so a red above says whether the wall was seen at all.
	const AActor* Hovered = HoveredWorldTarget(Rig);
	TestTrue(FString::Printf(TEXT("The pointer is over the wall (it is over %s)"), *GetNameSafe(Hovered)), Hovered == Wall->GetOwner());

	// The control: the wall lifted out of every ray, the same click is the button's -- so the button was
	// reachable at that pixel all along, and it was the wall that kept the click from it.
	Wall->SetWorldLocation(WallOutOfTheWay);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the button with the wall gone completes"), Play->Click());
	TestEqual(TEXT("With nothing in front of it the button is clicked"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldOcclusionTurnedOffTest,
	"DreamGUI.Driver.WorldSpace.Occlusion.WithOcclusionTurnedOffAClickPassesThroughTheObjectToThePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldOcclusionTurnedOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldOcclusionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const FOcclusionStage Stage = SetUpStage(*this, Rig, Listener.Get());
	UBoxComponent* Wall = MakeWall(Rig.GetWorld(), WallCentre);
	if (!Stage.IsReady() || !TestNotNull(TEXT("A wall stands between the eye and the panel"), Wall))
	{
		return false;
	}
	// Off: the behaviour every world pointer had before occlusion was the default, and the one a project
	// gets back by unticking it. The panels are traced and the world is not.
	Stage.Pointer->SetOccludeByWorld(false);
	Rig.PumpFrames(2);

	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Widget(Stage.Button));
	TestTrue(TEXT("Clicking at the button's pixel completes"), Play->Click());
	TestEqual(TEXT("With occlusion off the click goes through the wall to the button"), Listener->ClickedCount, 1);
	TestEqual(TEXT("... having pressed it"), Listener->PressedCount, 1);
	const AActor* HoveredWhileOff = HoveredWorldTarget(Rig);
	TestTrue(FString::Printf(TEXT("... and the pointer is over nothing in the world (it is over %s)"), *GetNameSafe(HoveredWhileOff)),
		HoveredWhileOff == nullptr);

	// The control: the same wall, the same pixel, occlusion back on. The wall was solid to the trace all
	// along -- it was the switch that let the click through, not a wall the trace could not see.
	Stage.Pointer->SetOccludeByWorld(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking again with occlusion back on completes"), Play->Click());
	TestEqual(TEXT("With it on, the wall takes the click and the button is not clicked again"), Listener->ClickedCount, 1);
	const AActor* HoveredWhileOn = HoveredWorldTarget(Rig);
	TestTrue(FString::Printf(TEXT("... because the pointer is over the wall (it is over %s)"), *GetNameSafe(HoveredWhileOn)),
		HoveredWhileOn == Wall->GetOwner());
	return true;
}

/**
 * The reason the default changed. A render-target surface is a mesh in the world, and the only way a
 * world pointer's hit ever reaches the surface's UDreamUIRenderTargetInteraction is the world trace:
 * with occlusion off, the pointer the manager gives a player could never click anything shown on one.
 * DreamDriverRenderTargetMeshAutomationTests clicks the same button with the flag set by hand; this is
 * that click with nothing set at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldOcclusionRenderTargetDefaultsTest,
	"DreamGUI.Driver.WorldSpace.Occlusion.AWorldPointerLeftAtItsDefaultsClicksAButtonShownOnARenderTargetSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldOcclusionRenderTargetDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldOcclusionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig, EyeAtTheOrigin(), EDreamWorldPointerSource::Mouse);
	// One texel per canvas unit per centimetre: a 400 x 300 cm surface, three metres ahead.
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), AheadOfTheEye(), FIntPoint(400, 300));
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestTrue(TEXT("The render-target canvas and its surface were built"), Screen.IsComplete()))
	{
		return false;
	}
	TestTrue(TEXT("The world pointer, attached and left alone, traces the world"), Pointer->GetOccludeByWorld());

	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), Screen.CanvasRoot, FVector2D(160.0, 60.0));
	if (!TestNotNull(TEXT("A button on the render-target canvas"), Button))
	{
		return false;
	}
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Button->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Rig.PumpFrames(2);

	const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(Button, Rig.Context().Camera.Get());
	if (!TestTrue(TEXT("The button shown on the surface has a pixel"), Pixel.IsSet()))
	{
		return false;
	}

	// Move, press, release, one frame each, and the interaction ticked after every frame: a game's tick
	// manager ticks it, the headless pump does not (see DreamDriverWorld::TickLikeAnEngineFrame).
	UDreamUIRenderTargetInteraction* Interaction = Screen.Interaction;
	const TFunction<void(FDreamDriverContext&)> Tick = [Interaction](FDreamDriverContext& InContext)
	{
		DreamDriverWorld::TickLikeAnEngineFrame(Interaction, InContext.FrameSeconds);
	};
	TestTrue(TEXT("The click at the button's pixel completes"), Rig.Driver()->Sequence()
		.MoveToPixel(Pixel.GetValue())
		.Then(Tick)
		.Press()
		.Then(Tick)
		.Release()
		.Then(Tick)
		.WaitFrames(1)
		.Then(Tick)
		.Perform());
	TestEqual(TEXT("The button shown on the surface was pressed once"), Listener->PressedCount, 1);
	TestEqual(TEXT("... and clicked once"), Listener->ClickedCount, 1);
	// The first half of the road, so a red above says which half failed: the world trace found the
	// surface and the pointer is over the actor that shows it.
	const AActor* Hovered = HoveredWorldTarget(Rig);
	TestTrue(FString::Printf(TEXT("The pointer is over the surface's actor (it is over %s)"), *GetNameSafe(Hovered)), Hovered == Screen.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverWorldOcclusionChannelTest,
	"DreamGUI.Driver.WorldSpace.Occlusion.WhatStopsAPointerIsWhatBlocksItsTraceChannelVisibilityByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverWorldOcclusionChannelTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverWorldOcclusionTestLocal;
	const ECollisionChannel DefaultChannel = UEngineTypes::ConvertToCollisionChannel(TraceTypeQuery1);
	const ECollisionChannel OtherChannel = UEngineTypes::ConvertToCollisionChannel(TraceTypeQuery2);
	if (!TestNotEqual(TEXT("TraceTypeQuery1 and TraceTypeQuery2 are two different collision channels"),
		static_cast<int32>(DefaultChannel), static_cast<int32>(OtherChannel)))
	{
		return false;
	}

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const FOcclusionStage Stage = SetUpStage(*this, Rig, Listener.Get());
	// Solid to everything but the pointer's default channel -- on Visibility, what the engine's
	// InvisibleWall profile is: something a player cannot walk through and can see through.
	UBoxComponent* Wall = MakeWall(Rig.GetWorld(), WallCentre, DefaultChannel);
	UDreamCanvas* PanelCanvas = Stage.Panel != nullptr ? Stage.Panel->GetComponent<UDreamCanvas>() : nullptr;
	if (!Stage.IsReady() || !TestNotNull(TEXT("A wall stands between the eye and the panel"), Wall)
		|| !TestNotNull(TEXT("The panel has a canvas"), PanelCanvas))
	{
		return false;
	}
	Rig.PumpFrames(2);

	FDreamElementRef Play = Rig.Driver()->Find(FDreamBy::Widget(Stage.Button));
	TestTrue(TEXT("Clicking at the button's pixel completes"), Play->Click());
	TestEqual(TEXT("A wall that lets the pointer's channel through does not stop the click"), Listener->ClickedCount, 1);
	const AActor* HoveredOnDefault = HoveredWorldTarget(Rig);
	TestTrue(FString::Printf(TEXT("... and is nothing the pointer is over (it is over %s)"), *GetNameSafe(HoveredOnDefault)),
		HoveredOnDefault == nullptr);

	// The pointer moved to TraceTypeQuery2, which the wall blocks. The panel has to move with it: the
	// channel is one setting for which canvases the pointer answers for and what its world trace can be
	// stopped by, so a pointer on another channel no longer sees a panel left on the first.
	Stage.Pointer->SetTraceChannel(TraceTypeQuery2);
	PanelCanvas->SetTraceChannel(TraceTypeQuery2);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking again on the other channel completes"), Play->Click());
	TestEqual(TEXT("On a channel the wall blocks, the wall takes the click"), Listener->ClickedCount, 1);
	const AActor* HoveredOnOther = HoveredWorldTarget(Rig);
	TestTrue(FString::Printf(TEXT("... because the pointer is over the wall (it is over %s)"), *GetNameSafe(HoveredOnOther)),
		HoveredOnOther == Wall->GetOwner());

	// The control for that half: with the wall lifted away, the panel on the other channel takes the
	// click -- so moving the channel kept the panel reachable, and it was the wall that stopped it.
	// Past the double-click time first, so this press is a plain press whatever the click on the wall
	// did to the pointer's click run; how runs cross targets is not what this test is about.
	Wall->SetWorldLocation(WallOutOfTheWay);
	Rig.PumpFrames(FramesPastTheDoubleClickTime);
	TestTrue(TEXT("Clicking the button on the other channel with the wall gone completes"), Play->Click());
	TestEqual(TEXT("With the wall gone the button is clicked on the other channel too"), Listener->ClickedCount, 2);
	return true;
}

#endif
