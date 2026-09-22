// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "GameFramework/Actor.h"

#include "DreamWorldRaycastTestTypes.h"
#include "DreamScopedWorld.h"

/*
 * What a world-space raycaster answers with, and what a world object does to a pointer.
 *
 * The raycaster serves a PLAYER, not a panel: it asks the manager for every world-space root canvas
 * on its trace channel and hit-tests all of them, so which panels exist is never something it has to
 * be told. That makes ordering the whole of its answer -- the input module reads element 0 as the
 * hit -- and ordering across separate canvases can only be distance.
 *
 * The other half is what a hit on a wall MEANS when every event in this pipeline is dispatched to a
 * UDreamWidget and a wall has none. The answer is occlusion, and nothing else. A world hit carries a
 * distance and no widget, so a wall in front of a world-space panel takes the pointer off the panel
 * and the click never lands. That is a complete behaviour rather than a stub, and it is what a
 * trigger volume in front of a UI is actually for.
 */

namespace DreamWorldRaycastTestLocal
{
	using DreamTests::FScopedGameWorld;

	USceneComponent* MakeHost(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)return nullptr;
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Host"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Root->SetWorldLocation(Location);
		return Root;
	}

	/** A solid box in the world, the thing a pointer is supposed to be stopped by. */
	UBoxComponent* MakeBlocker(UWorld* World, const FVector& Location, float Extent = 50.0f)
	{
		AActor* BlockerActor = World->SpawnActor<AActor>();
		if (BlockerActor == nullptr)return nullptr;
		UBoxComponent* Box = NewObject<UBoxComponent>(BlockerActor);
		Box->SetBoxExtent(FVector(Extent));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		BlockerActor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		return Box;
	}

	/**
	 * A square world-space panel standing at Location, hit-testable and facing the +X ray.
	 *
	 * "Facing" is a widget's own local +X: UDreamVisual::LineTraceUIRect brings the ray into widget
	 * space and requires it to cross the local X=0 plane, then checks the crossing against the rect
	 * in local Y (left/right) and Z (bottom/top). An unrotated host therefore puts the panel's plane
	 * across a ray travelling down world +X, which is all a hit needs -- the test is two-sided, so
	 * which way the panel faces decides what it looks like, not whether it can be clicked.
	 *
	 * Order matters here: the canvas has to exist before the visual, because CreateNewVisual only
	 * enrols the visual with the widget's render canvas if there is one, and a visual that is not
	 * enrolled is one Raycast never walks over.
	 */
	UDreamWidget* MakeWorldPanel(UWorld* World, const FString& Name, const FVector& Location, float Size, UDreamCanvas*& OutCanvas)
	{
		OutCanvas = nullptr;
		USceneComponent* Host = MakeHost(World, Location);
		if (Host == nullptr)return nullptr;
		UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(World, Name, nullptr);
		if (Root == nullptr)return nullptr;
		Root->SetWidth(Size);
		Root->SetHeight(Size);
		OutCanvas = Root->AddComponent<UDreamCanvas>();
		if (OutCanvas == nullptr)return nullptr;
		OutCanvas->SetRenderMode(EDreamRenderMode::WorldSpace);
		UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Host);
		Root->CreateNewVisual<UDreamVisualEmpty>();
		return Root;
	}

	/** A raycaster with a known ray, on its own actor so it never traces into the panels' hosts. */
	UDreamWorldSpaceRaycasterFixedRay* MakeFixedRayRaycaster(UWorld* World)
	{
		AActor* Host = World->SpawnActor<AActor>();
		if (Host == nullptr)return nullptr;
		UDreamWorldSpaceRaycasterFixedRay* Raycaster = NewObject<UDreamWorldSpaceRaycasterFixedRay>(Host);
		Raycaster->RegisterComponent();
		return Raycaster;
	}

	/**
	 * An event system this player already has, so EnsureInteractionForPlayer has nothing to spawn.
	 *
	 * A bare test world has no player controller and the event system in project settings is a
	 * content blueprint; letting that path run would make the test depend on content and on a load
	 * that may log. The raycaster half, which is what these tests are about, is unaffected.
	 */
	UDreamEventSystem* PlaceEventSystem(UWorld* World, int32 UserIndex)
	{
		AActor* Host = World->SpawnActor<AActor>();
		if (Host == nullptr)return nullptr;
		UDreamEventSystem* EventSystem = NewObject<UDreamEventSystem>(Host);
		EventSystem->SetUserIndex(UserIndex);
		Host->AddInstanceComponent(EventSystem);
		EventSystem->RegisterComponent();
		return EventSystem;
	}

	int32 CountComponentsOfClass(const AActor* InActor, const UClass* InClass)
	{
		if (InActor == nullptr)return 0;
		int32 Count = 0;
		for (UActorComponent* Component : InActor->GetComponents())
		{
			if (Component != nullptr && Component->IsA(InClass))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastHitsWorldCanvasesTest,
	"DreamGUI.WorldRaycast.HitsWorldCanvasesSortedByDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastHitsWorldCanvasesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world to trace in"), Scope.World))
	{
		return false;
	}
	UDreamCanvas* NearCanvas = nullptr;
	UDreamCanvas* FarCanvas = nullptr;
	UDreamWidget* Near = MakeWorldPanel(Scope.World, TEXT("Near"), FVector(300.0, 0.0, 0.0), 200.0f, NearCanvas);
	UDreamWidget* Far = MakeWorldPanel(Scope.World, TEXT("Far"), FVector(800.0, 0.0, 0.0), 200.0f, FarCanvas);
	if (!TestNotNull(TEXT("The near panel"), Near) || !TestNotNull(TEXT("The far panel"), Far))
	{
		return false;
	}
	// The fixture's own claim, asserted rather than assumed: a panel whose visual never reached the
	// canvas is invisible to the hit test for a reason that has nothing to do with the raycaster.
	if (!TestEqual(TEXT("The near panel's visual is on its canvas"), NearCanvas->GetVisualArray().Num(), 1) ||
		!TestEqual(TEXT("The far panel's visual is on its canvas"), FarCanvas->GetVisualArray().Num(), 1))
	{
		return false;
	}

	UDreamWorldSpaceRaycasterFixedRay* Raycaster = MakeFixedRayRaycaster(Scope.World);
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>();
	if (!TestNotNull(TEXT("A raycaster"), Raycaster) || !TestNotNull(TEXT("A pointer"), EventData))
	{
		return false;
	}

	FVector RayOrigin = FVector::ZeroVector, RayDirection = FVector::ZeroVector, RayEnd = FVector::ZeroVector;
	TArray<FDreamUIHitResult> HitArray;
	Raycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);

	// Two panels, neither of which the raycaster was ever pointed at: it found them by asking the
	// manager which world-space roots share its trace channel. This is the whole change of shape --
	// the old raycaster answered for the one canvas on its own actor and nothing else.
	if (!TestEqual(TEXT("Both panels are hit"), HitArray.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("The nearer one is first"), HitArray[0].Widget.Get(), Near);
	TestEqual(TEXT("...and the farther one second"), HitArray[1].Widget.Get(), Far);
	TestTrue(TEXT("The near distance is the panel's own"), FMath::IsNearlyEqual(HitArray[0].Distance, 300.0f, 1.0f));
	TestTrue(TEXT("...and so is the far one's"), FMath::IsNearlyEqual(HitArray[1].Distance, 800.0f, 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastTraceChannelFiltersTest,
	"DreamGUI.WorldRaycast.TraceChannelFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastTraceChannelFiltersTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world to trace in"), Scope.World))
	{
		return false;
	}
	UDreamCanvas* NearCanvas = nullptr;
	UDreamCanvas* FarCanvas = nullptr;
	UDreamWidget* Near = MakeWorldPanel(Scope.World, TEXT("Near"), FVector(300.0, 0.0, 0.0), 200.0f, NearCanvas);
	UDreamWidget* Far = MakeWorldPanel(Scope.World, TEXT("Far"), FVector(800.0, 0.0, 0.0), 200.0f, FarCanvas);
	UDreamWorldSpaceRaycasterFixedRay* Raycaster = MakeFixedRayRaycaster(Scope.World);
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>();
	if (!TestNotNull(TEXT("The near panel"), Near) || !TestNotNull(TEXT("The far panel"), Far)
		|| !TestNotNull(TEXT("A raycaster"), Raycaster) || !TestNotNull(TEXT("A pointer"), EventData))
	{
		return false;
	}

	// The trace channel is what divides one pointer's world-space UI from another's -- a motion
	// controller answering for one set of panels while the mouse answers for a different set. Moving
	// a panel off the raycaster's channel has to take it out of that raycaster's reach entirely,
	// rather than merely changing what the engine collision trace would have said about it.
	FarCanvas->SetTraceChannel(TraceTypeQuery2);

	FVector RayOrigin = FVector::ZeroVector, RayDirection = FVector::ZeroVector, RayEnd = FVector::ZeroVector;
	TArray<FDreamUIHitResult> HitArray;
	Raycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	if (!TestEqual(TEXT("Only the panel on the raycaster's channel is hit"), HitArray.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("...and it is the near one"), HitArray[0].Widget.Get(), Near);

	// Follow the raycaster over to the other channel and the answer swaps, which is what makes this
	// a filter rather than "the far panel stopped working".
	Raycaster->SetTraceChannel(TraceTypeQuery2);
	HitArray.Reset();
	Raycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	if (TestEqual(TEXT("On the other channel, the other panel"), HitArray.Num(), 1))
	{
		TestEqual(TEXT("...which is the far one"), HitArray[0].Widget.Get(), Far);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastWorldBlockerOccludesTest,
	"DreamGUI.WorldRaycast.WorldBlockerOccludesWhenEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastWorldBlockerOccludesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world to trace in"), Scope.World))
	{
		return false;
	}
	UDreamCanvas* PanelCanvas = nullptr;
	UDreamWidget* Panel = MakeWorldPanel(Scope.World, TEXT("Panel"), FVector(300.0, 0.0, 0.0), 200.0f, PanelCanvas);
	UDreamWorldSpaceRaycasterFixedRay* Raycaster = MakeFixedRayRaycaster(Scope.World);
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>();
	if (!TestNotNull(TEXT("A panel"), Panel) || !TestNotNull(TEXT("A raycaster"), Raycaster)
		|| !TestNotNull(TEXT("A pointer"), EventData))
	{
		return false;
	}
	// A box 150 units down the ray, half a hundred across: its near face is at 100, in front of the
	// panel at 300.
	if (!TestNotNull(TEXT("A blocker between the pointer and the panel"),
		MakeBlocker(Scope.World, FVector(150.0, 0.0, 0.0))))
	{
		return false;
	}

	FVector RayOrigin = FVector::ZeroVector, RayDirection = FVector::ZeroVector, RayEnd = FVector::ZeroVector;
	TArray<FDreamUIHitResult> HitArray;
	Raycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	// Off by default: a panel floating in front of a wall is the ordinary case, and paying for a line
	// trace per pointer per frame to discover that is not.
	if (TestEqual(TEXT("With occlusion off the wall is not consulted"), HitArray.Num(), 1))
	{
		TestEqual(TEXT("...and the panel answers"), HitArray[0].Widget.Get(), Panel);
	}

	Raycaster->SetOccludeByWorld(true);
	HitArray.Reset();
	Raycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	if (!TestEqual(TEXT("With it on, the wall joins the answer"), HitArray.Num(), 2))
	{
		return false;
	}
	// The contract in one assertion: it is a hit, it is nearer, and it is not a widget. The input
	// module reads element 0, finds nothing to dispatch to, and the click does not reach the panel.
	TestNull(TEXT("The nearest hit carries no widget"), HitArray[0].Widget.Get());
	TestTrue(TEXT("...and its distance is the wall's near face"),
		FMath::IsNearlyEqual(HitArray[0].Distance, 100.0f, 1.0f));
	TestEqual(TEXT("The panel is still found, behind it"), HitArray[1].Widget.Get(), Panel);

	// An overlap-only volume is not a wall. A multi trace would have reported it as one, which is how
	// a trigger meant to detect a player walking through it would have started eating clicks.
	UBoxComponent* Overlapper = MakeBlocker(Scope.World, FVector(50.0, 0.0, 0.0), 10.0f);
	if (TestNotNull(TEXT("An overlap-only volume in front of everything"), Overlapper))
	{
		Overlapper->SetCollisionResponseToAllChannels(ECR_Overlap);
		HitArray.Reset();
		Raycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
		if (TestEqual(TEXT("It is not an occluder"), HitArray.Num(), 2))
		{
			TestTrue(TEXT("...and the wall behind it still is"),
				FMath::IsNearlyEqual(HitArray[0].Distance, 100.0f, 1.0f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastEnsureInteractionIsIdempotentTest,
	"DreamGUI.WorldRaycast.EnsureInteractionForPlayerIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastEnsureInteractionIsIdempotentTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world"), Scope.World))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("A manager for it"), Manager))
	{
		return false;
	}
	if (!TestNotNull(TEXT("An event system this player already has"), PlaceEventSystem(Scope.World, 0)))
	{
		return false;
	}

	// Called on every world-space host's BeginPlay, so "twice" is the ordinary case rather than a
	// pathological one: two panels in a level means two calls before the first frame is drawn.
	Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::World);
	Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::World);

	AActor* Host = Manager->GetInteractionHost(0);
	if (!TestNotNull(TEXT("A host actor was made for player 0"), Host))
	{
		return false;
	}
	TestTrue(TEXT("...named after the player it speaks for"),
		Host->GetName().StartsWith(TEXT("DreamInteractionHost_P0")));
	TestEqual(TEXT("Two calls leave one world raycaster, not two"),
		CountComponentsOfClass(Host, UDreamWorldSpaceRaycaster::StaticClass()), 1);

	// The other kind shares the host. A player pointing at both a screen UI and a world panel is one
	// player, so two actors that mean the same thing would be one too many.
	Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::Screen);
	TestEqual(TEXT("The screen raycaster joins the same host"), Manager->GetInteractionHost(0), Host);
	TestEqual(TEXT("...and the host now carries one of each"),
		CountComponentsOfClass(Host, UDreamScreenSpaceRaycaster::StaticClass()), 1);
	TestEqual(TEXT("...without a second world one appearing"),
		CountComponentsOfClass(Host, UDreamWorldSpaceRaycaster::StaticClass()), 1);

	// A second player gets a host of their own, because a raycaster carries exactly one UserIndex.
	// Their event system is placed by hand for the same reason as the first player's, and because a
	// non-first player never gets one spawned for them in any case -- two copies of the default actor
	// would carry the same index and make each player read the other's input.
	PlaceEventSystem(Scope.World, 1);
	Manager->EnsureInteractionForPlayer(1, EDreamInteractionKind::World);
	AActor* SecondHost = Manager->GetInteractionHost(1);
	if (TestNotNull(TEXT("A host actor for player 1"), SecondHost))
	{
		TestNotEqual(TEXT("...which is not player 0's"), SecondHost, Host);
		TestEqual(TEXT("...carrying its own world raycaster"),
			CountComponentsOfClass(SecondHost, UDreamWorldSpaceRaycaster::StaticClass()), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastAuthoredRaycasterWinsTest,
	"DreamGUI.WorldRaycast.AnAuthoredRaycasterIsNotDuplicated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastAuthoredRaycasterWinsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world"), Scope.World))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("A manager for it"), Manager))
	{
		return false;
	}
	if (!TestNotNull(TEXT("An event system this player already has"), PlaceEventSystem(Scope.World, 0)))
	{
		return false;
	}

	// Somebody put a world-space raycaster on their own actor -- a pawn, a motion controller -- and
	// tuned it. Adding a default one beside it would give that player two rays into the same UI, so
	// the placed one has to be recognised as the answer.
	AActor* PlacedOn = Scope.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("An actor to place it on"), PlacedOn))
	{
		return false;
	}
	UDreamWorldSpaceRaycaster* Placed = NewObject<UDreamWorldSpaceRaycaster>(PlacedOn);
	Placed->SetUserIndex(0);
	PlacedOn->AddInstanceComponent(Placed);
	Placed->RegisterComponent();
	// Enrolled by hand, because registering does not do it here. In a game world a component only
	// auto-activates on register once its owner is initialized; until then activation waits for
	// AActor::InitializeComponents. A bare test world never runs InitializeActorsForPlay, so the placed
	// raycaster would never activate and never reach AllRaycasterArray. In a real level
	// InitializeComponents activates it before any BeginPlay, which is the state this stands in for.
	Placed->ActivateRaycaster();

	Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::World);
	TestNull(TEXT("No host is made when the player already has a world raycaster"),
		Manager->GetInteractionHost(0));

	// It is the KIND that is matched, not "any raycaster": the same player still needs a screen one.
	Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::Screen);
	AActor* Host = Manager->GetInteractionHost(0);
	if (TestNotNull(TEXT("A host is made for the kind that is missing"), Host))
	{
		TestEqual(TEXT("...carrying the screen raycaster"),
			CountComponentsOfClass(Host, UDreamScreenSpaceRaycaster::StaticClass()), 1);
		TestEqual(TEXT("...and no world one, which was already answered"),
			CountComponentsOfClass(Host, UDreamWorldSpaceRaycaster::StaticClass()), 0);
	}
	return true;
}

#endif
