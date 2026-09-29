// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUserWidgetTestTypes.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"

/*
 * UDreamWorldWidgetComponent: the host that puts a hierarchy in the world.
 *
 * What these pin is the set of answers the hierarchy itself cannot give. Its size in centimetres --
 * a screen stretches to a viewport and a world panel has nothing to stretch to; which renderer draws
 * it; what a pointer has to trace against to reach it; and which object the meshes belong to, which
 * decides whether the viewport can pick it at all.
 *
 * The fixture class is a NATIVE UDreamUserWidget subclass, so it never went through a designer and
 * FindDesignSize answers the default canvas for it. That is deliberate: the design size is being
 * tested as a contract between the class and the host, not as a number the compiler produced.
 */
namespace DreamWorldWidgetTestLocal
{
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		explicit FScopedWorld(EWorldType::Type InWorldType) { World = UWorld::CreateWorld(InWorldType, false); }
		~FScopedWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A host actor with InWidgetClass already loaded on it. */
	ADreamWorldWidgetActor* MakeHost(UWorld* World, UClass* InWidgetClass)
	{
		ADreamWorldWidgetActor* Actor = World->SpawnActor<ADreamWorldWidgetActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}
		UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
		if (Component == nullptr)
		{
			return nullptr;
		}
		// Naming the class is what builds the hierarchy: a game world has no BeginPlay here, and the
		// setter is the same path the actor factory uses when a class is dragged into a level.
		Component->SetWidgetClass(InWidgetClass);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetLoadsAtDesignSizeTest,
	"DreamGUI.WorldWidget.LoadsAtDesignSizeAndFollowsHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetLoadsAtDesignSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetTestLocal;
	FScopedWorld TestWorld(EWorldType::Game);
	ADreamWorldWidgetActor* Actor = MakeHost(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass());
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
	UDreamWidget* Root = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("the class loaded"), Root))return false;

	// The whole point of carrying the design size at runtime: a world-space root lands at the size it
	// was authored at rather than at nothing. A zero-sized root is the shape of "dragged it in and saw
	// no UI", and it is invisible in a way that looks like a rendering bug.
	TestEqual(TEXT("the root is as wide as the class was designed"), Root->GetWidth(), 1920.0f);
	TestEqual(TEXT("...and as tall"), Root->GetHeight(), 1080.0f);

	UDreamCanvas* Canvas = Component->GetLoadedCanvas();
	if (!TestNotNull(TEXT("the hierarchy has a root canvas"), Canvas))return false;
	TestEqual(TEXT("the default backend draws with DreamUI's renderer"),
		(int32)Canvas->GetRenderMode(), (int32)EDreamRenderMode::WorldSpace_DreamUI);
	TestEqual(TEXT("the canvas hangs from this component"),
		Canvas->GetAttachedRootSceneComponent(), static_cast<USceneComponent*>(Component));
	// Parked means created but never added to anything, which is where CreateDreamWidget leaves a
	// tree; attaching is what takes it out, and an un-attached tree draws nothing.
	TestFalse(TEXT("and it is no longer parked"), UDreamUIBPLibrary::IsWidgetParked(Root));

	Actor->SetActorLocation(FVector(120.0, -40.0, 900.0));
	TestTrue(TEXT("the tree follows the actor"),
		Root->GetWorldLocation().Equals(FVector(120.0, -40.0, 900.0), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetSettersApplyInPlaceTest,
	"DreamGUI.WorldWidget.SettersApplyInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetSettersApplyInPlaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetTestLocal;
	FScopedWorld TestWorld(EWorldType::Game);
	ADreamWorldWidgetActor* Actor = MakeHost(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass());
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
	UDreamWidget* Root = Component->GetLoadedWidget();
	UDreamCanvas* Canvas = Component->GetLoadedCanvas();
	if (!TestNotNull(TEXT("the class loaded"), Root) || !TestNotNull(TEXT("root canvas"), Canvas))return false;

	// Every property but the class applies to the tree that is standing. Rebuilding instead would
	// throw away everything the hierarchy holds -- scroll positions, animation state, whatever a
	// behaviour cached -- for a change of size.
	Component->SetDrawSize(FVector2D(400.0, 300.0));
	TestEqual(TEXT("an explicit size reaches the root"), Root->GetWidth(), 400.0f);
	TestEqual(TEXT("...on both axes"), Root->GetHeight(), 300.0f);
	TestFalse(TEXT("naming a size turns the design-size default off"), Component->GetUseDesignSize());
	TestEqual(TEXT("and the tree was not rebuilt"), Component->GetLoadedWidget(), Root);

	Component->SetBackend(EDreamWorldWidgetBackend::UERenderer);
	TestEqual(TEXT("the UE backend maps to the scene renderer"),
		(int32)Canvas->GetRenderMode(), (int32)EDreamRenderMode::WorldSpace);
	TestEqual(TEXT("still the same tree"), Component->GetLoadedWidget(), Root);

	Component->SetSortOrder(7);
	TestEqual(TEXT("sort order is written to the canvas"), Canvas->GetSortOrder(), 7);

	Component->SetTraceChannel(TraceTypeQuery2);
	TestEqual(TEXT("so is the trace channel a pointer has to use"),
		(int32)Canvas->GetTraceChannel().GetValue(), (int32)TraceTypeQuery2);

	Component->SetPivot(FVector2D(0.0, 0.0));
	TestTrue(TEXT("the pivot reaches the root"), Root->GetPivot().Equals(FVector2D(0.0, 0.0), 0.0001));

	Component->SetUseDesignSize(true);
	TestEqual(TEXT("handing size back to the class restores the design width"), Root->GetWidth(), 1920.0f);
	TestEqual(TEXT("...and height"), Root->GetHeight(), 1080.0f);
	TestEqual(TEXT("through all of it, one tree"), Component->GetLoadedWidget(), Root);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetMeshOwnerTest,
	"DreamGUI.WorldWidget.MeshIsOwnedByHostActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetMeshOwnerTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetTestLocal;
	FScopedWorld TestWorld(EWorldType::Game);
	ADreamWorldWidgetActor* Actor = MakeHost(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass());
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamCanvas* Canvas = Actor->GetWidgetComponent()->GetLoadedCanvas();
	if (!TestNotNull(TEXT("root canvas"), Canvas))return false;

	// A component's owner comes from its outer chain, and a widget tree's chain ends at the World --
	// so a mesh outered to the widget had no owner, and the viewport picks and the details panel both
	// go through the owner's component list. This is what makes a world panel clickable at all.
	UDreamUIMeshComponent* Mesh = Canvas->GetUIMesh();
	if (!TestNotNull(TEXT("the canvas has a mesh"), Mesh))return false;
	TestEqual(TEXT("the mesh belongs to the host actor"), Mesh->GetOwner(), static_cast<AActor*>(Actor));
	// Through the actor's own component list, which is the list both the viewport and Details walk.
	TestEqual(TEXT("and the actor lists it among its components"),
		Actor->FindComponentByClass<UDreamUIMeshComponent>(), Mesh);
	TestTrue(TEXT("transient, so a saved level does not carry it"), Mesh->HasAnyFlags(RF_Transient));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetReloadAndReregisterTest,
	"DreamGUI.WorldWidget.ReloadIsIdempotentAndReregisterKeepsTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetReloadAndReregisterTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetTestLocal;
	// An editor world, because that is where both halves of this are decided: it is the only world
	// that loads on register, and the only one where a reregister happens for reasons that are not a
	// teardown.
	FScopedWorld TestWorld(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = MakeHost(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass());
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
	UDreamWidget* FirstTree = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("the class loaded"), FirstTree))return false;
	const TWeakObjectPtr<UDreamWidget> FirstTreeWatch = FirstTree;

	// Reload replaces the tree and takes the old one with it. A reload that left the previous tree
	// alive would leave it registered and drawing: two panels, one host.
	Component->ReloadWidget();
	UDreamWidget* SecondTree = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("reloading leaves a tree"), SecondTree))return false;
	TestTrue(TEXT("...a different one"), SecondTree != FirstTree);
	TestFalse(TEXT("and the previous one was destroyed"), FirstTreeWatch.IsValid());

	// Editing any property of an actor unregisters and registers every component on it. Rebuilding
	// there is what made a nudge of the transform behave like reopening the asset.
	Component->UnregisterComponent();
	TestEqual(TEXT("unregistering is not a teardown"), Component->GetLoadedWidget(), SecondTree);
	Component->RegisterComponent();
	TestEqual(TEXT("and registering again keeps the same tree"), Component->GetLoadedWidget(), SecondTree);
	UDreamCanvas* Canvas = Component->GetLoadedCanvas();
	if (!TestNotNull(TEXT("root canvas"), Canvas))return false;
	TestEqual(TEXT("re-seated on the component it came back on"),
		Canvas->GetAttachedRootSceneComponent(), static_cast<USceneComponent*>(Component));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetDestroyTearsDownTest,
	"DreamGUI.WorldWidget.DestroyingTheComponentDestroysTheTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetDestroyTearsDownTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetTestLocal;
	// An editor world, because that is the one without an EndPlay: the tree there is released by the
	// component's own teardown or not at all until garbage collection, and a tree torn down from inside
	// GC is the case to stay out of.
	FScopedWorld TestWorld(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = MakeHost(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass());
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
	const TWeakObjectPtr<UDreamWidget> TreeWatch = Component->GetLoadedWidget();
	if (!TestTrue(TEXT("the class loaded"), TreeWatch.IsValid()))return false;

	// Unregistering is not a teardown, but an unregister that comes from a destruction is: the tree has
	// to go with the component rather than keep drawing, registered with the manager, until GC.
	Component->DestroyComponent();
	TestFalse(TEXT("destroying the component destroyed its tree"), TreeWatch.IsValid());
	TestNull(TEXT("and the component forgot it"), Component->GetLoadedWidget());
	return true;
}

#endif
