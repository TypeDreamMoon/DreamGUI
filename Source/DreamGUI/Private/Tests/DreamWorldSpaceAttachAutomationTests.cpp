// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/*
 * UDreamUIBPLibrary::AttachWidgetToSceneComponent: the creation verb for a world-space root made in
 * code. Attaching is what takes the root out of the parked state, and the tree must keep following
 * the component afterwards -- a presenter used to do that from its own OnUpdateTransform, which left
 * any other host standing still.
 */
namespace DreamWorldSpaceAttachTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachWidgetToSceneComponentTest,
	"DreamGUI.World.AttachWidgetToSceneComponentUnparksAndFollows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachWidgetToSceneComponentTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSpaceAttachTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}
	USceneComponent* Host = MakeHost(TestWorld.World, FVector(100.0f, 200.0f, 300.0f));
	if (!TestNotNull(TEXT("host component"), Host))return false;

	// A root without a canvas is refused and left alone.
	UDreamWidget* NoCanvas = UDreamUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("NoCanvas"), nullptr);
	AddExpectedError(TEXT("has no DreamCanvas"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("refused without a canvas"), UDreamUIBPLibrary::AttachWidgetToSceneComponent(NoCanvas, Host));
	TestTrue(TEXT("still parked"), Manager->IsWidgetParked(NoCanvas));

	UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Root"), nullptr);
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("canvas"), Canvas))return false;
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	TestTrue(TEXT("parked before attach"), Manager->IsWidgetParked(Root));

	if (!TestTrue(TEXT("attach succeeds"), UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Host)))return false;
	TestFalse(TEXT("no longer parked"), Manager->IsWidgetParked(Root));
	TestTrue(TEXT("active in hierarchy"), Root->GetWidgetActiveInHierarchy());
	TestEqual(TEXT("canvas knows its host"), Canvas->GetAttachedRootSceneComponent(), Host);
	TestTrue(TEXT("placed at the host"), Root->GetWorldLocation().Equals(FVector(100.0f, 200.0f, 300.0f), 0.01f));

	// Move the host: the tree follows, with nobody calling anything on the widget.
	Host->SetWorldLocation(FVector(-50.0f, 10.0f, 900.0f));
	TestTrue(TEXT("follows the host"), Root->GetWorldLocation().Equals(FVector(-50.0f, 10.0f, 900.0f), 0.01f));

	// A child widget keeps its offset inside the moved tree.
	UDreamWidget* Child = UDreamUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Child"), nullptr);
	Root->AddChild(Child);
	Child->SetAnchoredPosition(FVector2D(30.0f, 0.0f));
	const FVector ChildBefore = Child->GetWorldLocation();
	Host->SetWorldLocation(FVector(-50.0f, 10.0f, 1000.0f));
	const FVector ChildAfter = Child->GetWorldLocation();
	TestTrue(TEXT("child moved by the host's delta"), (ChildAfter - ChildBefore).Equals(FVector(0.0f, 0.0f, 100.0f), 0.01f));

	// Re-attaching to another host unbinds the first one.
	USceneComponent* Other = MakeHost(TestWorld.World, FVector::ZeroVector);
	Canvas->AttachToSceneComponent(Other);
	Host->SetWorldLocation(FVector(5000.0f, 0.0f, 0.0f));
	TestTrue(TEXT("old host no longer drives the tree"), Root->GetWorldLocation().Equals(FVector::ZeroVector, 0.01f));
	Other->SetWorldLocation(FVector(0.0f, 0.0f, 42.0f));
	TestTrue(TEXT("new host does"), Root->GetWorldLocation().Equals(FVector(0.0f, 0.0f, 42.0f), 0.01f));
	return true;
}

#endif
