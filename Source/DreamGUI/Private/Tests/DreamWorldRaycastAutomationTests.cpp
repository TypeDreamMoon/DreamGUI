// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamWorldSpaceRaycasterForWorldTrigger.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/RaycasterSource/DreamWorldSpaceRaycasterSource_World.h"
#include "GameFramework/Actor.h"
#include "Interaction/UIEventTrigger.h"

/*
 * What a world object does to a pointer.
 *
 * UDreamWorldSpaceRaycasterForWorldTrigger has existed since the plugin did, and its Raycast forwarded
 * to a RaycastWorld whose entire body was a refusal -- first a check(0), then an ensure, then a
 * warning. The question that kept it unimplemented is not how to trace: it is what a hit on a wall
 * MEANS when every event in this pipeline is dispatched to a UDreamWidget and a wall has none.
 *
 * The answer implemented here is that it means occlusion, and nothing else. A world hit carries a
 * distance and no widget; the input module sorts every raycaster's hits together by distance, so a
 * wall in front of a world-space panel takes the pointer off the panel and the click never lands.
 * That is a complete behaviour rather than a stub, and it is what a trigger volume in front of a UI
 * is actually for.
 */

namespace DreamWorldRaycastTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

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

	/** A world-trigger raycaster firing from the origin down +X, with a ray source it can use. */
	UDreamWorldSpaceRaycasterForWorldTrigger* MakeWorldRaycaster(UWorld* World)
	{
		AActor* Host = World->SpawnActor<AActor>();
		if (Host == nullptr)return nullptr;
		UDreamWorldSpaceRaycasterSource_World* Source = NewObject<UDreamWorldSpaceRaycasterSource_World>(Host);
		Host->SetRootComponent(Source);
		Source->RegisterComponent();
		Source->SetWorldLocationAndRotation(FVector::ZeroVector, FQuat::Identity);

		UDreamWorldSpaceRaycasterForWorldTrigger* Raycaster = NewObject<UDreamWorldSpaceRaycasterForWorldTrigger>(Host);
		Raycaster->RegisterComponent();
		Raycaster->SetRaycasterSourceObject(Source);
		return Raycaster;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastFindsBlockersTest,
	"DreamGUI.Input.Raycast.AWorldObjectIsFoundAsAnOccluderWithNoWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastFindsBlockersTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to trace in"), Scope.World != nullptr))
	{
		return false;
	}
	UDreamWorldSpaceRaycasterForWorldTrigger* Raycaster = MakeWorldRaycaster(Scope.World);
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>();
	if (!TestTrue(TEXT("A raycaster and a pointer"), Raycaster != nullptr && EventData != nullptr))
	{
		return false;
	}
	//through the base class, which is where Raycast is public -- the override is protected
	UDreamBaseRaycaster* AsBaseRaycaster = Raycaster;

	FVector RayOrigin = FVector::ZeroVector, RayDirection = FVector::ZeroVector, RayEnd = FVector::ZeroVector;
	TArray<FDreamUIHitResult> HitArray;
	AsBaseRaycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	TestEqual(TEXT("With nothing in the way, nothing is hit"), HitArray.Num(), 0);
	TestTrue(TEXT("...but the ray was still generated"), RayDirection.Equals(FVector::ForwardVector, 0.01));

	// A box 500 units down the ray, half a hundred across: its near face is at 450.
	UBoxComponent* Blocker = MakeBlocker(Scope.World, FVector(500.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("A blocker in the world"), Blocker))
	{
		return false;
	}
	AsBaseRaycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	if (!TestEqual(TEXT("The blocker is hit"), HitArray.Num(), 1))
	{
		return false;
	}
	// The whole point of the contract: it is a hit, and it is not a widget. Anything downstream that
	// dereferenced Widget without asking would have crashed the moment this component was placed.
	TestNull(TEXT("A world hit carries no widget"), HitArray[0].Widget.Get());
	TestTrue(TEXT("...and the distance is the near face, not the centre"),
		FMath::IsNearlyEqual(HitArray[0].Distance, 450.0f, 1.0f));
	TestTrue(TEXT("...with an impact point on the ray"),
		FMath::IsNearlyEqual((float)HitArray[0].Location.X, 450.0f, 1.0f));

	// A second blocker, nearer. What occludes a pointer is the nearest thing that blocks it -- what is
	// behind that is behind a wall -- so the answer moves to the new one rather than accumulating.
	MakeBlocker(Scope.World, FVector(200.0, 0.0, 0.0));
	AsBaseRaycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
	if (TestEqual(TEXT("A second blocker does not add a second answer"), HitArray.Num(), 1))
	{
		TestTrue(TEXT("...the nearest one is what blocks the pointer"),
			FMath::IsNearlyEqual(HitArray[0].Distance, 150.0f, 1.0f));
	}

	// An overlap-only volume is not a wall. A multi trace would have reported it as one, which is how a
	// trigger meant to detect a player walking through it would have started eating clicks.
	UBoxComponent* Overlapper = MakeBlocker(Scope.World, FVector(100.0, 0.0, 0.0));
	if (TestNotNull(TEXT("An overlap-only volume in front of everything"), Overlapper))
	{
		Overlapper->SetCollisionResponseToAllChannels(ECR_Overlap);
		AsBaseRaycaster->Raycast(EventData, RayOrigin, RayDirection, RayEnd, HitArray);
		if (TestEqual(TEXT("It is not an occluder"), HitArray.Num(), 1))
		{
			TestTrue(TEXT("...and the blocker behind it still is"),
				FMath::IsNearlyEqual(HitArray[0].Distance, 150.0f, 1.0f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRaycastBlocksTheUITest,
	"DreamGUI.Input.Raycast.AWidgetlessHitTakesThePointerOffTheUI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldRaycastBlocksTheUITest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldRaycastTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the widgets"), Scope.World != nullptr))
	{
		return false;
	}
	AActor* Host = Scope.World->SpawnActor<AActor>();
	UDreamEventSystem* EventSystem = NewObject<UDreamEventSystem>(Host);
	EventSystem->RegisterComponent();
	UDreamPointerEventData* EventData = EventSystem->GetPointerEventData(0, true);

	UDreamWidget* Panel = NewObject<UDreamWidget>(Scope.World, NAME_None, RF_Public | RF_Transactional);
	Panel->SetDisplayName(TEXT("Panel"));
	Panel->SetWidth(100.0f);
	Panel->SetHeight(100.0f);
	Panel->OnRegister();
	UUIEventTrigger* Trigger = Panel->AddComponent<UUIEventTrigger>();
	if (!TestTrue(TEXT("A panel with an event trigger and a pointer"),
		EventData != nullptr && Trigger != nullptr))
	{
		return false;
	}
	int32 EnterCount = 0;
	int32 ExitCount = 0;
	int32 DownCount = 0;
	Trigger->GetOnPointerEnterEvent().AddLambda([&EnterCount](UDreamPointerEventData*) { ++EnterCount; });
	Trigger->GetOnPointerExitEvent().AddLambda([&ExitCount](UDreamPointerEventData*) { ++ExitCount; });
	Trigger->GetOnPointerDownEvent().AddLambda([&DownCount](UDreamPointerEventData*) { ++DownCount; });

	auto RunFrame = [&EventSystem, &EventData](UDreamWidget* HitWidget, bool bHitSomething)
	{
		FDreamUIHitResultContainer HitContainer;
		if (HitWidget != nullptr)
		{
			HitContainer.HitResult.Widget = HitWidget;
			HitContainer.HoverArray.Add(HitWidget);
		}
		bool bOutIsHitSomething = false;
		FDreamUIHitResult OutHitResult;
		UDreamPointerInputModule::ProcessPointerEvent(
			EventSystem, EventData, bHitSomething, HitContainer, bOutIsHitSomething, OutHitResult);
	};

	// The pointer is over the panel.
	RunFrame(Panel, true);
	TestEqual(TEXT("The pointer entered the panel"), EnterCount, 1);

	// Now a world blocker is in front of it: something WAS hit, and it has no widget. This is exactly
	// the shape UDreamBaseRaycaster::RaycastWorld produces, and the shape that used to be impossible.
	RunFrame(nullptr, true);
	TestEqual(TEXT("A widgetless hit exits the panel"), ExitCount, 1);
	TestTrue(TEXT("...and the pointer is over nothing"), EventData->EnterWidget == nullptr);

	// And a press while the blocker is in the way lands on nothing at all, which is what "the wall
	// stopped the click" has to mean.
	EventData->bNowIsTriggerPressed = true;
	RunFrame(nullptr, true);
	TestEqual(TEXT("A press through a blocker presses nothing"), DownCount, 0);
	TestTrue(TEXT("...and nothing is held"), EventData->PressWidget == nullptr);
	return true;
}

#endif
