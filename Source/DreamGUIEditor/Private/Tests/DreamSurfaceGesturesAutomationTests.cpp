// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
#include "PrefabEditor/DreamUIPrefabEditorViewportClient.h"
#include "DreamUIControlRegistry.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// What the design surface decides before it touches anything.
//
// A multi-widget move is one gesture over one shape, so the grid may bend it only once. Snapping
// each widget onto its own nearest gridline instead pulls the selection apart as it travels, and
// two widgets less than a grid step apart end up stacked.
//
// A drag on the surface can now move a widget into a different container, which means it inherits
// the refusals the hierarchy tree has always applied -- a widget onto itself, onto its own
// descendant, into a parent with no room -- and a widget dropped back where it already lives is
// not a reparent at all.
//
// The Wrap With menu named its four containers by hand while the registry held ten; the panels a
// menu offers have to be the panels that exist, or the two lists drift the moment one is extended.
namespace DreamSurfaceGestureTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UObject* Outer, const TCHAR* Name, float Width, float Height)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(Width);
		Widget->SetHeight(Height);
		return Widget;
	}

	/** One target in an unrotated, unscaled parent, so the arithmetic under test is the snap alone. */
	FDreamUIPrefabEditorViewportClient::FMoveDragTarget MakeTarget(const FVector2D& InStartPosition, const FVector& InTravel)
	{
		FDreamUIPrefabEditorViewportClient::FMoveDragTarget Target;
		Target.PlaneTransform = FTransform::Identity;
		Target.StartPlanePoint = FVector::ZeroVector;
		Target.CurrentPlanePoint = InTravel;
		Target.StartPosition = InStartPosition;
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceMoveDragKeepsSelectionRigidTest,
	"DreamGUI.Editor.SurfaceGestures.MoveDragSnapsTheSelectionOnceNotWidgetByWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSurfaceMoveDragKeepsSelectionRigidTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurfaceGestureTestLocal;

	// Two widgets in one parent, three units apart, which is less than the grid step. Dragged five
	// units on a grid of eight, the leader lands on 8; the follower has to keep its three units.
	TArray<FDreamUIPrefabEditorViewportClient::FMoveDragTarget> Targets;
	Targets.Add(MakeTarget(FVector2D(0.0, 0.0), FVector(0.0, 5.0, 0.0)));
	Targets.Add(MakeTarget(FVector2D(3.0, 0.0), FVector(0.0, 5.0, 0.0)));

	TArray<FDreamUIPrefabEditorViewportClient::FMoveDragResult> Results;
	FDreamUIPrefabEditorViewportClient::ResolveMoveDrag(Targets, 8.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;
	TestTrue(TEXT("the widget the gesture is anchored on lands on the gridline"), Results[0].Position.Equals(FVector2D(8.0, 0.0)));
	// Snapped on its own, this one reads 8 as well, and the two widgets end up on top of each other.
	TestTrue(TEXT("and the one beside it keeps the distance it started with"), Results[1].Position.Equals(FVector2D(11.0, 0.0)));

	TestTrue(TEXT("the grid moved X, so X has a guide to draw"), Results[0].bSnappedHorizontal);
	TestTrue(TEXT("for every widget the correction was applied to"), Results[1].bSnappedHorizontal);
	TestFalse(TEXT("Y never left the gridline it started on"), Results[0].bSnappedVertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceMoveDragGuideFollowsWritableAxesTest,
	"DreamGUI.Editor.SurfaceGestures.MoveDragClaimsAGuideOnlyOnAnAxisItMayWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSurfaceMoveDragGuideFollowsWritableAxesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurfaceGestureTestLocal;

	// A real grid, so the correction on both axes is non-zero: without one the axis guard under
	// test is unreachable, because nothing snapped on either axis in the first place.
	FDreamUIPrefabEditorViewportClient::FMoveDragTarget Target = MakeTarget(FVector2D(100.0, 200.0), FVector(0.0, 10.0, 7.0));
	Target.bHorizontalFree = false;
	TArray<FDreamUIPrefabEditorViewportClient::FMoveDragTarget> Targets = { Target };

	TArray<FDreamUIPrefabEditorViewportClient::FMoveDragResult> Results;
	FDreamUIPrefabEditorViewportClient::ResolveMoveDrag(Targets, 8.0f, Results);
	if (!TestEqual(TEXT("one result"), Results.Num(), 1))return false;
	TestTrue(TEXT("the arranged axis is untouched and the free one snaps"),
		Results[0].Position.Equals(FVector2D(100.0, 208.0)));
	// The grid did move X -- it is the widget that may not.
	TestFalse(TEXT("an axis nothing may write cannot claim a guide"), Results[0].bSnappedHorizontal);
	TestTrue(TEXT("the axis that was actually written can"), Results[0].bSnappedVertical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceReparentRefusalsTest,
	"DreamGUI.Editor.SurfaceGestures.SurfaceReparentRefusesWhatTheTreeRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSurfaceReparentRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurfaceGestureTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f));
	Root->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TStrongObjectPtr<UDreamWidget> Panel(MakeWidget(Root.Get(), TEXT("Panel"), 200.0f, 200.0f));
	Panel->TrySetParent(Root.Get(), false);
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TStrongObjectPtr<UDreamWidget> Grandchild(MakeWidget(Panel.Get(), TEXT("Grandchild"), 50.0f, 50.0f));
	Grandchild->TrySetParent(Panel.Get(), false);
	Grandchild->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TStrongObjectPtr<UDreamWidget> Leaf(MakeWidget(Root.Get(), TEXT("Leaf"), 100.0f, 50.0f));
	Leaf->TrySetParent(Root.Get(), false);

	// A SizeBox holds one child, so an occupied one is the capacity refusal in its plainest form.
	TStrongObjectPtr<UDreamWidget> Full(MakeWidget(TestWorld.World, TEXT("Full"), 200.0f, 200.0f));
	Full->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	TStrongObjectPtr<UDreamWidget> Occupant(MakeWidget(Full.Get(), TEXT("Occupant"), 50.0f, 50.0f));
	Occupant->TrySetParent(Full.Get(), false);

	TArray<UDreamWidget*> OnlyLeaf = { Leaf.Get() };
	TArray<UDreamWidget*> OnlyPanel = { Panel.Get() };
	TArray<UDreamWidget*> Nothing;
	TestTrue(TEXT("a widget dropped into a sibling panel with room is a reparent"),
		FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(OnlyLeaf, Panel.Get()));
	TestFalse(TEXT("a widget cannot be dropped into itself"),
		FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(OnlyPanel, Panel.Get()));
	TestFalse(TEXT("nor into something it already contains"),
		FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(OnlyPanel, Grandchild.Get()));
	TestFalse(TEXT("nor into a parent with no room left"),
		FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(OnlyLeaf, Full.Get()));
	// Not a refusal so much as "there is nothing here to do": the drag stays the move it was.
	TestFalse(TEXT("a widget dropped where it already lives is not a reparent"),
		FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(OnlyLeaf, Root.Get()));
	TestFalse(TEXT("and neither an empty selection nor an empty container is one"),
		FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(Nothing, Panel.Get())
		|| FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(OnlyLeaf, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfacePanelMenuOffersEveryRegisteredPanelTest,
	"DreamGUI.Editor.SurfaceGestures.PanelMenusOfferEveryRegisteredPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSurfacePanelMenuOffersEveryRegisteredPanelTest::RunTest(const FString& Parameters)
{
	TArray<const FDreamUIControlDescriptor*> Panels;
	FDreamWidgetBlueprintEditor::CollectLayoutPanelDescriptors(nullptr, Panels);

	auto Contains = [&Panels](UClass* InClass)
	{
		return Panels.ContainsByPredicate([InClass](const FDreamUIControlDescriptor* Descriptor)
		{
			return Descriptor->LayoutContainerClass.Get() == InClass;
		});
	};
	// Named one by one, and counted against that list rather than against a second copy of the
	// collector's own filter: re-deriving the expectation from LayoutContainerClass && !Visual &&
	// !Behaviour makes the assertion agree with whatever the filter happens to say, including when
	// both sides are wrong together.
	const TArray<UClass*> Registered = {
		UDreamLayoutContainerCanvasPanel::StaticClass(),
		UDreamLayoutContainerOverlay::StaticClass(),
		UDreamLayoutContainerHorizontalBox::StaticClass(),
		UDreamLayoutContainerVerticalBox::StaticClass(),
		UDreamLayoutContainerStackBox::StaticClass(),
		UDreamLayoutContainerWrapBox::StaticClass(),
		UDreamLayoutContainerGridPanel::StaticClass(),
		UDreamLayoutContainerUniformGridPanel::StaticClass(),
		UDreamLayoutContainerWidgetSwitcher::StaticClass(),
		UDreamLayoutContainerScrollBox::StaticClass(),
		// Single-child panels are panels too (UMG's Wrap With offers Size Box); the ContentWidget they
		// need comes from the container's own required-behaviour rule, not from the recipe, so they no
		// longer read as "a control that uses a panel". Wrapping several widgets in one is refused
		// with a notification, not hidden from the menu.
		UDreamLayoutContainerSizeBox::StaticClass(),
		UDreamLayoutContainerScaleBox::StaticClass(),
		UDreamLayoutContainerSafeZone::StaticClass(),
	};
	for (UClass* PanelClass : Registered)
	{
		TestTrue(FString::Printf(TEXT("%s is offered"), *PanelClass->GetName()), Contains(PanelClass));
	}
	TestEqual(TEXT("and nothing else is"), Panels.Num(), Registered.Num());
	// A control that merely uses a panel is not one: Border is an Overlay with an image and a
	// ContentWidget in its recipe and stays out, even though the plain Overlay is offered.
	TestFalse(TEXT("a control that happens to hang off a panel is not offered as a panel"),
		Panels.ContainsByPredicate([](const FDreamUIControlDescriptor* Descriptor) { return Descriptor->Name == TEXT("Border"); }));

	for (int32 Index = 1; Index < Panels.Num(); ++Index)
	{
		if (Panels[Index - 1]->DisplayName.CompareTo(Panels[Index]->DisplayName) > 0)
		{
			AddError(FString::Printf(TEXT("the panel list is out of order at %s"), *Panels[Index]->DisplayName.ToString()));
			break;
		}
	}

	TArray<const FDreamUIControlDescriptor*> WithoutGrid;
	FDreamWidgetBlueprintEditor::CollectLayoutPanelDescriptors(UDreamLayoutContainerGridPanel::StaticClass(), WithoutGrid);
	TestEqual(TEXT("excluding the panel a widget already has drops exactly that one"), WithoutGrid.Num(), Panels.Num() - 1);
	TestFalse(TEXT("and it is the one that is gone"), WithoutGrid.ContainsByPredicate([](const FDreamUIControlDescriptor* Descriptor)
	{
		return Descriptor->LayoutContainerClass.Get() == UDreamLayoutContainerGridPanel::StaticClass();
	}));
	return true;
}

#endif
