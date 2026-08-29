// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamText.h"
#include "PrefabSystem/DreamUIObjectReaderAndWriter.h"
#include "Engine/World.h"
#include "UObject/Package.h"

/*
 * The hierarchy as an object graph.
 *
 * UDreamWidget::Children used to be Transient, so the parent-child structure existed nowhere the
 * engine could see it -- only inside the prefab blob, as FDreamUIPrefabSaveData::MapWidgetToParent,
 * rebuilt by hand at load. That is why a widget tree could not be duplicated, saved, or held as a
 * class template: there was no graph to walk.
 *
 * These tests pin the three properties that change buys, because none of them are visible from
 * reading the property declaration alone:
 *   - duplication carries the whole hierarchy (the same instanced-reference machinery a class
 *     template instances through, which is why this is the cheap proxy for it),
 *   - the transient Parent back-pointers can always be rebuilt from the persistent Children,
 *   - a tree with no world is inert rather than fatal, which is what a class template will be.
 *
 * The fourth test guards the other direction: the prefab blob must keep ignoring Children, or every
 * child gets attached twice on load -- once from the restored array, once from the attach pass.
 */

namespace DreamWidgetTreeObjectGraphTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Root / A / B, with C under A. Deep enough that a one-level-only copy shows up as a difference. */
	struct FSampleTree
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamWidget* A = nullptr;
		UDreamWidget* B = nullptr;
		UDreamWidget* C = nullptr;
	};

	FSampleTree BuildSampleTree(UObject* InOuter)
	{
		FSampleTree Sample;
		Sample.Tree = NewObject<UDreamWidgetTree>(InOuter);

		Sample.Root = Sample.Tree->ConstructWidget<UDreamWidget>();
		Sample.Root->SetDisplayName(TEXT("Root"));
		Sample.Tree->RootWidget = Sample.Root;

		Sample.A = Sample.Tree->ConstructWidget<UDreamWidget>();
		Sample.A->SetDisplayName(TEXT("A"));
		Sample.A->TrySetParent(Sample.Root, false);

		Sample.B = Sample.Tree->ConstructWidget<UDreamWidget>();
		Sample.B->SetDisplayName(TEXT("B"));
		Sample.B->TrySetParent(Sample.Root, false);

		Sample.C = Sample.Tree->ConstructWidget<UDreamWidget>();
		Sample.C->SetDisplayName(TEXT("C"));
		Sample.C->TrySetParent(Sample.A, false);

		return Sample;
	}

	/** "Root/A/C" style path, built from the persistent Children arrays alone. */
	void CollectPaths(const UDreamWidget* InWidget, const FString& InPrefix, TArray<FString>& OutPaths)
	{
		const FString Path = InPrefix.IsEmpty() ? InWidget->GetDisplayName() : InPrefix + TEXT("/") + InWidget->GetDisplayName();
		OutPaths.Add(Path);
		for (const UDreamWidget* Child : InWidget->GetChildren())
		{
			if (Child != nullptr)
			{
				CollectPaths(Child, Path, OutPaths);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTreeDuplicateCarriesHierarchyTest,
	"DreamGUI.WidgetTree.ObjectGraph.DuplicateCarriesTheWholeHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTreeDuplicateCarriesHierarchyTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTreeObjectGraphTestLocal;
	FScopedGameWorld TestWorld;

	const FSampleTree Source = BuildSampleTree(TestWorld.World);
	TArray<FString> SourcePaths;
	CollectPaths(Source.Root, FString(), SourcePaths);
	TestEqual(TEXT("the sample tree is four widgets deep enough to matter"), SourcePaths.Num(), 4);

	TStrongObjectPtr<UDreamWidgetTree> Copy(DuplicateObject<UDreamWidgetTree>(Source.Tree, GetTransientPackage()));
	if (!TestNotNull(TEXT("duplicating the tree produced a tree"), Copy.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the copy has a root"), Copy->RootWidget.Get()))
	{
		return false;
	}

	TArray<FString> CopyPaths;
	CollectPaths(Copy->RootWidget, FString(), CopyPaths);
	TestEqual(TEXT("the copy has the same hierarchy, read from Children alone"), CopyPaths, SourcePaths);

	// A shallow copy would leave the copy's Children pointing at the originals, which reads as a
	// perfect structural match above. Distinct identity is the half of the claim that test cannot see.
	TestNotEqual(TEXT("the copied root is a distinct object"), (const UDreamWidget*)Copy->RootWidget, (const UDreamWidget*)Source.Root);
	for (const UDreamWidget* CopiedChild : Copy->RootWidget->GetChildren())
	{
		TestTrue(TEXT("no copied child is one of the source widgets"),
			CopiedChild != Source.A && CopiedChild != Source.B && CopiedChild != Source.C);
		TestTrue(TEXT("every copied widget is owned by the copied tree"), CopiedChild->IsIn(Copy.Get()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTreeParentLinksRebuildTest,
	"DreamGUI.WidgetTree.ObjectGraph.ParentLinksRebuildFromChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTreeParentLinksRebuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTreeObjectGraphTestLocal;
	FScopedGameWorld TestWorld;

	const FSampleTree Source = BuildSampleTree(TestWorld.World);

	// A duplicate is the honest stand-in for a tree that arrived without going through the attach path.
	// Parent is DuplicateTransient precisely so this happens: the copy comes back with the structure
	// intact and every back-pointer empty -- the same state a package load leaves behind.
	TStrongObjectPtr<UDreamWidgetTree> Copy(DuplicateObject<UDreamWidgetTree>(Source.Tree, GetTransientPackage()));
	if (!TestNotNull(TEXT("the copy has a root"), Copy.IsValid() ? Copy->RootWidget.Get() : nullptr))
	{
		return false;
	}

	UDreamWidget* CopiedA = Copy->RootWidget->GetChildren().Num() > 0 ? Copy->RootWidget->GetChildren()[0] : nullptr;
	if (!TestNotNull(TEXT("the copy kept its first child"), CopiedA))
	{
		return false;
	}
	TestNull(TEXT("Parent does not survive duplication, which is what makes the rebuild necessary"), CopiedA->GetParent());

	Copy->RebuildParentLinks();

	TestEqual(TEXT("the child's parent is restored"), CopiedA->GetParent(), Copy->RootWidget.Get());
	UDreamWidget* CopiedC = CopiedA->GetChildren().Num() > 0 ? CopiedA->GetChildren()[0] : nullptr;
	if (TestNotNull(TEXT("the grandchild survived"), CopiedC))
	{
		TestEqual(TEXT("the rebuild reaches the whole subtree, not just the first level"), CopiedC->GetParent(), CopiedA);
	}

	// Running it on an already-linked tree must not disturb anything: the load path may call it more
	// than once (instancing, then a refresh), and a renumber or a detach here would be silent.
	Copy->RebuildParentLinks();
	TestEqual(TEXT("a second rebuild changes nothing"), CopiedA->GetParent(), Copy->RootWidget.Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTreeInstancesFromATemplateTest,
	"DreamGUI.WidgetTree.ObjectGraph.InstancingFromATemplateProducesAWholeTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTreeInstancesFromATemplateTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTreeObjectGraphTestLocal;
	FScopedGameWorld TestWorld;

	// This is the mechanism the class model will instantiate through, verbatim: UMG's
	// UUserWidget::DuplicateAndInitializeFromWidgetTree is NewObject with the class's tree as the
	// Template and an FObjectInstancingGraph to walk it. Duplication (tested above) is a good proxy,
	// but only this exercises the path a UDreamWidgetGeneratedClass will actually take -- and the
	// instancing graph follows Instanced properties, so it is the direct test of that flag.
	const FSampleTree Template = BuildSampleTree(GetTransientPackage());
	TArray<FString> TemplatePaths;
	CollectPaths(Template.Root, FString(), TemplatePaths);

	FObjectInstancingGraph InstancingGraph;
	TStrongObjectPtr<UDreamWidgetTree> Instance(NewObject<UDreamWidgetTree>(
		TestWorld.World, Template.Tree->GetClass(), NAME_None, RF_Transactional,
		Template.Tree, /*bCopyTransientsFromClassDefaults*/false, &InstancingGraph));

	if (!TestNotNull(TEXT("instancing produced a tree"), Instance.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the instanced tree has a root of its own"), Instance->RootWidget.Get()))
	{
		return false;
	}
	TestNotEqual(TEXT("and that root is not the template's"), (const UDreamWidget*)Instance->RootWidget, (const UDreamWidget*)Template.Root);

	Instance->RebuildParentLinks();
	TArray<FString> InstancePaths;
	CollectPaths(Instance->RootWidget, FString(), InstancePaths);
	TestEqual(TEXT("the instance has the template's whole hierarchy, not just its root"), InstancePaths, TemplatePaths);

	// The template must be left exactly as it was -- a class template gets instanced many times, and
	// an instancing pass that stole or renumbered its children would corrupt every later instance.
	TArray<FString> TemplatePathsAfter;
	CollectPaths(Template.Root, FString(), TemplatePathsAfter);
	TestEqual(TEXT("instancing did not disturb the template"), TemplatePathsAfter, TemplatePaths);

	// The instance lives in a world, so unlike the template its widgets can resolve one.
	TestEqual(TEXT("the instanced tree resolves the world it was outered into"), Instance->GetWorld(), TestWorld.World);
	TestEqual(TEXT("and so do the widgets inside it"), Instance->RootWidget->GetWorld(), TestWorld.World);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTreeTemplateHasNoWorldTest,
	"DreamGUI.WidgetTree.ObjectGraph.ATreeOutsideAWorldIsInertNotFatal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTreeTemplateHasNoWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTreeObjectGraphTestLocal;

	// The shape a class template will have: outered to a package, never to a world. UDreamWidget::GetWorld
	// is GetTypedOuter<UWorld>(), so everything in here answers null -- and must survive doing so, because
	// registration is what talks to the world subsystem and a template never registers.
	TStrongObjectPtr<UDreamWidgetTree> Tree(NewObject<UDreamWidgetTree>(GetTransientPackage()));
	UDreamWidget* Root = Tree->ConstructWidget<UDreamWidget>();
	Root->SetDisplayName(TEXT("TemplateRoot"));
	Tree->RootWidget = Root;

	TestNull(TEXT("a tree outside a world has no world"), Tree->GetWorld());
	TestNull(TEXT("a widget in it has no world either"), Root->GetWorld());
	TestFalse(TEXT("and it has not registered, because registration is an explicit call"), Root->HasRegistered());

	// The walk helpers have to tolerate it too -- they run over trees straight off disk.
	TestEqual(TEXT("the tree can still be counted"), Tree->CountWidgets(), 1);
	Tree->RebuildParentLinks();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPrefabBlobStillSkipsChildrenTest,
	"DreamGUI.WidgetTree.ObjectGraph.PrefabBlobStillIgnoresChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPrefabBlobStillSkipsChildrenTest::RunTest(const FString& Parameters)
{
	// Children used to be skipped for free, by being Transient. It no longer is, and the prefab format
	// still carries the hierarchy separately (MapWidgetToParent) and still replays it through
	// SetParentBeforeRegister. If the writer ever starts emitting Children, load attaches every child
	// twice and the damage shows up as duplicated widgets, not as an error.
	const FProperty* ChildrenProperty = UDreamWidget::StaticClass()->FindPropertyByName(UDreamWidget::GetPropertyName_Children());
	if (!TestNotNull(TEXT("UDreamWidget still has a Children property under that name"), ChildrenProperty))
	{
		return false;
	}
	TestFalse(TEXT("Children is no longer Transient -- it is the persistent hierarchy now"),
		ChildrenProperty->HasAnyPropertyFlags(CPF_Transient));
	// UPROPERTY(Instanced) on an array puts CPF_InstancedReference on the ELEMENT; the array itself
	// only gets CPF_ContainsInstancedReference. Asserting on the array was how this test first failed.
	const FArrayProperty* ChildrenArray = CastField<FArrayProperty>(ChildrenProperty);
	if (TestNotNull(TEXT("Children is still an array property"), ChildrenArray))
	{
		TestTrue(TEXT("its element is Instanced, which is what instancing and duplication follow"),
			ChildrenArray->Inner->HasAnyPropertyFlags(CPF_InstancedReference));
	}
	TestTrue(TEXT("and the prefab writer skips it anyway"),
		DreamUIPrefabSystem::DreamUIPrefab_ShouldSkipProperty(ChildrenProperty));
	// The duplicate and override archives roll their own property filter instead of calling
	// ShouldSkipProperty, so making Children persistent silently opened a second door. They share the
	// hierarchy predicate now; this is the assertion that catches it closing again.
	TestTrue(TEXT("the shared hierarchy predicate recognises Children"),
		DreamUIPrefabSystem::DreamUIPrefab_IsHierarchyProperty(ChildrenProperty));

	// A sibling property that must keep serializing, so the skip above cannot be a blanket refusal.
	const FProperty* SiblingIndexProperty = UDreamWidget::StaticClass()->FindPropertyByName(UDreamWidget::GetPropertyName_SiblingIndex());
	if (TestNotNull(TEXT("UDreamWidget still has SiblingIndex"), SiblingIndexProperty))
	{
		TestFalse(TEXT("SiblingIndex is still written to the prefab, so ordering survives"),
			DreamUIPrefabSystem::DreamUIPrefab_ShouldSkipProperty(SiblingIndexProperty));
		TestFalse(TEXT("and the hierarchy predicate does not over-reach onto it"),
			DreamUIPrefabSystem::DreamUIPrefab_IsHierarchyProperty(SiblingIndexProperty));
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetDuplicateHierarchyTest,
	"DreamGUI.WidgetTree.ObjectGraph.DuplicatingALiveSubtreeGivesItItsOwnEverything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetDuplicateHierarchyTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTreeObjectGraphTestLocal;
	FScopedGameWorld TestWorld;

	// A live, registered subtree -- what every caller of this actually passes.
	UDreamWidget* Host = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transactional);
	Host->SetDisplayName(TEXT("Host"));
	Host->OnRegister();

	UDreamWidget* Source = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transactional);
	Source->SetDisplayName(TEXT("Cell"));
	Source->TrySetParent(Host, false);
	UDreamText* SourceVisual = Source->CreateNewVisual<UDreamText>();
	UDreamWidget* SourceChild = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transactional);
	SourceChild->SetDisplayName(TEXT("Label"));
	SourceChild->TrySetParent(Source, false);
	RegisterDreamWidgetHierarchy(Source);
	if (!TestNotNull(TEXT("the source has a visual to share or not share"), SourceVisual))
	{
		return false;
	}

	UDreamWidget* Copy = DuplicateDreamWidgetHierarchy(Host->GetOuter(), Source, Host);
	if (!TestNotNull(TEXT("duplicating produced a widget"), Copy))
	{
		return false;
	}

	TestNotEqual(TEXT("the copy is a distinct object"), (const UDreamWidget*)Copy, (const UDreamWidget*)Source);
	TestEqual(TEXT("and carries the child with it"), Copy->GetChildrenCount(), 1);

	// Attached and alive. An unregistered copy never lays out or draws, and every caller of this
	// expects the widget it gets back to be usable immediately.
	TestEqual(TEXT("the copy is attached where it was asked to be"), Copy->GetParent(), Host);
	TestTrue(TEXT("the copy is registered"), Copy->HasRegistered());
	if (Copy->GetChildrenCount() == 1)
	{
		UDreamWidget* CopiedChild = Copy->GetChildren()[0];
		TestNotEqual(TEXT("the copied child is distinct too"), (const UDreamWidget*)CopiedChild, (const UDreamWidget*)SourceChild);
		TestTrue(TEXT("and registered"), CopiedChild->HasRegistered());
		// The back-pointer: Parent is DuplicateTransient, so it arrives empty and has to be rebuilt.
		// Without that pass the copy has children that do not know who their parent is, and every
		// structural check above still passes.
		TestEqual(TEXT("and knows its parent"), CopiedChild->GetParent(), Copy);
	}

	// The half no structural comparison can see: a shared visual writes through to the original.
	UDreamVisual* CopiedVisual = Copy->GetVisual();
	TestNotNull(TEXT("the copy has a visual"), CopiedVisual);
	TestNotEqual(TEXT("which is NOT the source's"), (const UObject*)CopiedVisual, (const UObject*)SourceVisual);

	Copy->DestroyWidget();
	Source->DestroyWidget();
	Host->DestroyWidget();
	return true;
}

#endif
