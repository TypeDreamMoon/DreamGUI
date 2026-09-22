// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

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
 * The reinstancer replaces the placed tree with a copy and marks the original -- and everything outered
 * to it, its canvas and the materials the canvas made among them -- as garbage while it is still
 * registered. The copy takes over the host and the compile's repair brings it up. The canvas's mesh is
 * outered to the host actor instead, so nothing swept it along: it went on drawing the old tree beside
 * the new one, and the collection that ends the compile freed the materials its sections still pointed
 * at. That was a crash in the level viewport on the first frame after saving the Blueprint in its
 * designer.
 *
 * What this pins: the replaced tree is unregistered before any collection, its mesh with it, and what is
 * left afterwards is the copy alone -- one tree, one mesh -- through a collection as well.
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

	/** Registered, live instances of InClass in InWorld -- the trees that are on screen there. */
	int32 CountRegisteredInstances(const UWorld* InWorld, const UClass* InClass)
	{
		int32 Count = 0;
		for (TObjectIterator<UDreamUserWidget> It; It; ++It)
		{
			if (IsValid(*It) && It->GetClass() == InClass && It->HasRegistered() && It->GetWorld() == InWorld)
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
	TestEqual(TEXT("owned by the host actor, which is why nothing else takes it down"), FirstMesh->GetOwner(), static_cast<AActor*>(Actor));
	const TWeakObjectPtr<UDreamUIMeshComponent> FirstMeshWatch = FirstMesh;

	Class.Compile();

	// Before any collection: the mesh has to be gone by the time one could free what it draws with.
	TestFalse(TEXT("the replaced tree's mesh went with it"), FirstMeshWatch.IsValid());
	TestEqual(TEXT("so nothing is left drawing the old tree"), RegisteredMeshesIn(TestWorld.World).Num(), 0);
	UDreamWidget* Copy = Component->GetLoadedWidget();
	if (!TestNotNull(TEXT("the host holds the reinstancer's copy"), Copy))return false;
	TestTrue(TEXT("a different object"), Copy != FirstTree);
	TestTrue(TEXT("of the recompiled class"), Copy->GetClass() == GeneratedClass);

	// The compile's repair runs a tick later, on the editor manager's queue, and brings the copy up.
	if (UDreamUIManagerObject* Manager = UDreamUIManagerObject::GetInstance(/*CreateIfNotValid*/ true))
	{
		Manager->Tick(0.0f);
		Manager->Tick(0.0f);
	}
	TestTrue(TEXT("the copy is registered in its place"), Copy->HasRegistered());
	TestEqual(TEXT("and it is the only tree of the class on screen"), CountRegisteredInstances(TestWorld.World, GeneratedClass), 1);
	UDreamCanvas* CopyCanvas = Component->GetLoadedCanvas();
	if (!TestNotNull(TEXT("the copy has a root canvas"), CopyCanvas))return false;
	UDreamUIMeshComponent* CopyMesh = CopyCanvas->GetUIMesh();
	if (!TestNotNull(TEXT("with a mesh"), CopyMesh))return false;
	TestEqual(TEXT("owned by the same actor"), CopyMesh->GetOwner(), static_cast<AActor*>(Actor));
	TestEqual(TEXT("and it is the only mesh drawing there"), RegisteredMeshesIn(TestWorld.World).Num(), 1);

	// The moment the crash happened: the collection that frees the replaced tree and its materials.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	const TArray<UDreamUIMeshComponent*> Surviving = RegisteredMeshesIn(TestWorld.World);
	TestEqual(TEXT("after a collection one mesh is still drawing"), Surviving.Num(), 1);
	TestTrue(TEXT("the copy's, whose canvas is alive"), Surviving.Num() == 1 && Surviving[0] == CopyMesh && IsValid(CopyCanvas));
	TestTrue(TEXT("and the host still holds a live tree"), IsValid(Component->GetLoadedWidget()));
	return true;
}

#endif
