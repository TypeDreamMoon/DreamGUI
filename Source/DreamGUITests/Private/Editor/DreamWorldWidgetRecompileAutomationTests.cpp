// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

/*
 * Recompiling the class of a widget placed in a level.
 *
 * The reinstancer replaces every live instance of a recompiled class with a property copy and marks the
 * original -- and everything outered to it, its canvas and the materials the canvas made among them -- as
 * garbage. A widget tree does not come through that: the copy is a husk, and the original's canvas mesh,
 * outered to the host actor, went on drawing the old tree with materials the compile's collection then
 * freed. That was a crash in the level viewport on the first frame after saving the Blueprint in its
 * designer.
 *
 * So the tree does not wait for the reinstancer. What this pins: the placed tree is let go before the
 * compile, its mesh with it and before any collection; the host holds nothing while the class compiles;
 * and a tick after the compile it holds a tree built fresh from the new class -- one tree, one mesh, no
 * copy of the old one -- through a collection as well.
 */
namespace DreamWorldWidgetRecompileTestLocal
{
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		explicit FScopedWorld(EWorldType::Type InWorldType) { World = UWorld::CreateWorld(InWorldType, false); }
		~FScopedWorld() { if (World) { World->DestroyWorld(false); } }
	};

	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		/** Without a collection, like the designer's own Compile: the test decides when one runs. */
		void Compile() const
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		}
	};

	/** Registered, live meshes in InWorld -- what the renderer is drawing there. */
	TArray<UDreamUIMeshComponent*> RegisteredMeshesIn(const UWorld* InWorld)
	{
		TArray<UDreamUIMeshComponent*> Meshes;
		for (TObjectIterator<UDreamUIMeshComponent> It; It; ++It)
		{
			if (IsValid(*It) && It->IsRegistered() && It->GetWorld() == InWorld)
			{
				Meshes.Add(*It);
			}
		}
		return Meshes;
	}

	/** Live instances of InClass in InWorld, registered or not -- a reinstancer's copy among them if there were one. */
	int32 CountLiveInstances(const UWorld* InWorld, const UClass* InClass, bool bRegisteredOnly)
	{
		int32 Count = 0;
		for (TObjectIterator<UDreamUserWidget> It; It; ++It)
		{
			if (IsValid(*It) && !It->IsTemplate() && It->GetClass() == InClass && It->GetWorld() == InWorld
				&& (!bRegisteredOnly || It->HasRegistered()))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetRecompileTest,
	"DreamGUI.WorldWidget.RecompilingThePlacedClassReplacesTheTreeAndTakesItsMeshWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetRecompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetRecompileTestLocal;

	FScopedBlueprint Class(TEXT("WorldWidgetRecompile"));
	if (!TestNotNull(TEXT("the widget Blueprint was created"), Class.Blueprint))return false;
	Class.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("Root"));
	Class.Compile();
	UClass* GeneratedClass = Class.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("and compiled"), GeneratedClass))return false;
	UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	if (!TestNotNull(TEXT("the editor's DreamGUI subsystem"), EditorSubsystem))return false;

	// An editor world: the level the class was dragged into, where the tree is built on register.
	FScopedWorld TestWorld(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = TestWorld.World->SpawnActor<ADreamWorldWidgetActor>();
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
	Component->SetWidgetClass(GeneratedClass);

	UDreamWidget* FirstTree = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("the placed class loaded"), FirstTree))return false;
	UDreamCanvas* FirstCanvas = Component->GetLoadedCanvas();
	if (!TestNotNull(TEXT("with a root canvas"), FirstCanvas))return false;
	UDreamUIMeshComponent* FirstMesh = FirstCanvas->GetUIMesh();
	if (!TestNotNull(TEXT("that has a mesh"), FirstMesh))return false;
	TestEqual(TEXT("owned by the host actor, which is why the tree has to take it down itself"), FirstMesh->GetOwner(), static_cast<AActor*>(Actor));
	const TWeakObjectPtr<UDreamUIMeshComponent> FirstMeshWatch = FirstMesh;

	Class.Compile();

	// Before any collection: the mesh has to be gone by the time one could free what it draws with.
	TestFalse(TEXT("the placed tree's mesh went with it"), FirstMeshWatch.IsValid());
	TestEqual(TEXT("so nothing is left drawing the old tree"), RegisteredMeshesIn(TestWorld.World).Num(), 0);
	TestFalse(TEXT("the placed tree was destroyed, not handed to the reinstancer"), IsValid(FirstTree));
	TestNull(TEXT("the host holds no tree while the class compiles"), Component->GetLoadedWidget());
	TestEqual(TEXT("and the reinstancer made no copy of it"), CountLiveInstances(TestWorld.World, GeneratedClass, /*bRegisteredOnly*/false), 0);
	TestFalse(TEXT("the compile is over"), EditorSubsystem->IsRecompiling());
	TestTrue(TEXT("and the host is waiting to build again"), EditorSubsystem->HasPendingRebuild());

	// What the tick after the compile does.
	EditorSubsystem->RebuildReleasedTrees();
	TestFalse(TEXT("nothing is left waiting"), EditorSubsystem->HasPendingRebuild());
	UDreamWidget* Rebuilt = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("the host built its tree again"), Rebuilt))return false;
	TestTrue(TEXT("a new one"), Rebuilt != FirstTree);
	TestTrue(TEXT("of the recompiled class"), Rebuilt->GetClass() == GeneratedClass);
	TestTrue(TEXT("registered in its place"), Rebuilt->HasRegistered());
	TestEqual(TEXT("and it is the only tree of the class there, registered or not"), CountLiveInstances(TestWorld.World, GeneratedClass, false), 1);
	UDreamCanvas* RebuiltCanvas = Component->GetLoadedCanvas();
	if (!TestNotNull(TEXT("the new tree has a root canvas"), RebuiltCanvas))return false;
	UDreamUIMeshComponent* RebuiltMesh = RebuiltCanvas->GetUIMesh();
	if (!TestNotNull(TEXT("with a mesh"), RebuiltMesh))return false;
	TestEqual(TEXT("owned by the same actor"), RebuiltMesh->GetOwner(), static_cast<AActor*>(Actor));
	TestEqual(TEXT("and it is the only mesh drawing there"), RegisteredMeshesIn(TestWorld.World).Num(), 1);

	// The moment the crash happened: the collection that frees the old tree and its materials.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	const TArray<UDreamUIMeshComponent*> Surviving = RegisteredMeshesIn(TestWorld.World);
	TestEqual(TEXT("after a collection one mesh is still drawing"), Surviving.Num(), 1);
	TestTrue(TEXT("the new tree's, whose canvas is alive"), Surviving.Num() == 1 && Surviving[0] == RebuiltMesh && IsValid(RebuiltCanvas));
	TestTrue(TEXT("and the host still holds that tree"), Component->GetLoadedWidget() == Rebuilt && IsValid(Rebuilt));
	return true;
}

/*
 * The same compile, for a panel in a world that has no UI manager.
 *
 * The release before a compile walks the worlds that have a manager -- the level editor's, a preview's, a play
 * session's -- and asks each host there for its tree. A world without one (a game preview world, which the manager
 * leaves out: UDreamUIManagerWorldSubsystem::DoesSupportWorldType) builds its panels all the same, and nothing walks
 * it before the compile. What stands between such a tree and the reinstancer is the editor subsystem's handler for
 * the replacement itself: the original instance is taken out of its world -- every widget of it unregistered, its
 * canvas's mesh with it -- while its memory is still live, before any collection can free what it was drawing with.
 * Left registered, it is the orphan the level viewport crashed on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWidgetRecompileWithoutManagerTest,
	"DreamGUI.WorldWidget.RecompilingAClassPlacedInAWorldWithNoUIManagerTakesTheReplacedTreeOutOfThatWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldWidgetRecompileWithoutManagerTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldWidgetRecompileTestLocal;

	FScopedBlueprint Class(TEXT("WorldWidgetRecompileNoManager"));
	if (!TestNotNull(TEXT("the widget Blueprint was created"), Class.Blueprint))return false;
	Class.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("Root"));
	Class.Compile();
	UClass* GeneratedClass = Class.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("and compiled"), GeneratedClass))return false;

	FScopedWorld TestWorld(EWorldType::GamePreview);
	if (!TestNull(TEXT("a game preview world has no UI manager"), UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World)))
	{
		return false;
	}
	ADreamWorldWidgetActor* Actor = TestWorld.World->SpawnActor<ADreamWorldWidgetActor>();
	if (!TestNotNull(TEXT("host actor"), Actor))return false;
	UDreamWorldWidgetComponent* Component = Actor->GetWidgetComponent();
	Component->SetWidgetClass(GeneratedClass);

	UDreamWidget* FirstTree = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("the placed class loaded there all the same"), FirstTree))return false;
	if (!TestTrue(TEXT("and its tree is registered in that world"), FirstTree->HasRegistered()))return false;
	// Raw pointers, read again after the compile: the reinstancer only marks the original, and no collection runs
	// before the checks below, so they stay readable for exactly as long as this test needs them.
	TArray<UDreamWidget*> FirstWidgets;
	UDreamWidget::CollectChildrenWidgets(FirstTree, FirstWidgets, /*IncludeTarget*/true);
	const UDreamCanvas* FirstCanvas = Component->GetLoadedCanvas();
	const TWeakObjectPtr<UDreamUIMeshComponent> FirstMeshWatch = FirstCanvas != nullptr ? FirstCanvas->GetUIMesh() : nullptr;
	AddInfo(FString::Printf(TEXT("The placed tree has %d widgets and %s mesh."), FirstWidgets.Num(), FirstMeshWatch.IsValid() ? TEXT("a") : TEXT("no")));

	Class.Compile();

	TArray<FString> StillRegistered;
	for (const UDreamWidget* Widget : FirstWidgets)
	{
		if (Widget->HasRegistered())
		{
			StillRegistered.Add(Widget->GetName());
		}
	}
	TestEqual(TEXT("after the compile no widget of the replaced tree is still registered in that world"),
		FString::Join(StillRegistered, TEXT(", ")), FString());
	TestFalse(TEXT("and the replaced tree is on its way out"), IsValid(FirstTree));
	TestFalse(TEXT("its mesh went with it, before any collection"), FirstMeshWatch.IsValid());
	TestEqual(TEXT("so nothing in that world is left drawing the old tree"), RegisteredMeshesIn(TestWorld.World).Num(), 0);
	return true;
}

#endif
