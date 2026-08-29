// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamUIWidgetPicking.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/DreamContentWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Engine/World.h"

// The designer used to ask UDreamUIManagerWorldSubsystem::RaycastHitUI what was under the cursor, and
// that function starts from `if (auto Visual = Widget->GetVisual())`. Every UMG-family layout panel
// is registered without a visual, so under that question an Overlay does not exist -- it could not
// be clicked, and a drop aimed at it silently landed on the prefab root instead, which is why a
// Button dropped "into" an Overlay kept its authored size. These tests pin the other question.
namespace DreamUIWidgetPickingTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UObject* Outer, const TCHAR* Name, float Width, float Height)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer ? Outer : (UObject*)World);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(Width);
		Widget->SetHeight(Height);
		return Widget;
	}

	/** A ray straight down -X through the world point (0, Y, Z), which is where widget rects live. */
	void RayAt(float Y, float Z, FVector& OutStart, FVector& OutEnd)
	{
		OutStart = FVector(1000.0f, Y, Z);
		OutEnd = FVector(-1000.0f, Y, Z);
	}

	bool HitsContain(const TArray<FDreamUIWidgetPickHit>& Hits, const UDreamWidget* Widget)
	{
		return Hits.ContainsByPredicate([Widget](const FDreamUIWidgetPickHit& Hit) { return Hit.Widget == Widget; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPickingHitsPanelWithNoVisualTest,
	"DreamGUI.Editor.Picking.PanelWithNoVisualIsStillUnderTheCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPickingHitsPanelWithNoVisualTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	TestNotNull(TEXT("overlay container"), (UObject*)Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>());

	// This is the whole bug in one assertion: an Overlay draws nothing, and the old hit test
	// required something drawn. If this ever starts returning a visual the test has stopped
	// covering the case it was written for.
	TestNull(TEXT("an overlay has no visual"), (UObject*)Root->GetVisual());

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FDreamUIWidgetPickHit> Hits;
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);

	TestEqual(TEXT("the overlay is under the cursor"), Hits.Num(), 1);
	TestTrue(TEXT("and it is the overlay"), HitsContain(Hits, Root));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPickingMissesOutsideTheRectTest,
	"DreamGUI.Editor.Picking.RayOutsideTheRectHitsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPickingMissesOutsideTheRectTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();

	// Centre pivot, so the rect spans [-400, 400] x [-300, 300]. Just outside the right edge.
	FVector Start, End;
	RayAt(401.0f, 0.0f, Start, End);
	TArray<FDreamUIWidgetPickHit> Hits;
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("outside the right edge"), Hits.Num(), 0);

	RayAt(0.0f, 301.0f, Start, End);
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("above the top edge"), Hits.Num(), 0);

	RayAt(399.0f, 299.0f, Start, End);
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("just inside the corner"), Hits.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPickingPrefersTheChildTest,
	"DreamGUI.Editor.Picking.ChildIsPickedBeforeItsPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPickingPrefersTheChildTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* Leaf = MakeWidget(TestWorld.World, Root, TEXT("Leaf"), 100.0f, 50.0f);
	TestTrue(TEXT("attach"), Leaf->TrySetParent(Root, false));

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FDreamUIWidgetPickHit> Hits;
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root, Leaf}, Start, End, Hits);

	TestEqual(TEXT("both rects contain the point"), Hits.Num(), 2);
	if (Hits.Num() == 2)
	{
		// Front-to-back means deepest first, so clicking a button inside a panel selects the button.
		TestEqual(TEXT("the child is in front"), (UObject*)Hits[0].Widget, (UObject*)Leaf);
		TestEqual(TEXT("the panel is behind it"), (UObject*)Hits[1].Widget, (UObject*)Root);
	}

	// Repeating the same pick walks deeper, so the panel underneath stays reachable.
	int32 CycleIndex = INDEX_NONE;
	TestEqual(TEXT("first click takes the child"), (UObject*)DreamUIWidgetPicking::PickTopmostWidget(TestWorld.World, {Root, Leaf}, Start, End, CycleIndex), (UObject*)Leaf);
	TestEqual(TEXT("second click takes the panel"), (UObject*)DreamUIWidgetPicking::PickTopmostWidget(TestWorld.World, {Root, Leaf}, Start, End, CycleIndex), (UObject*)Root);
	TestEqual(TEXT("third click wraps back to the child"), (UObject*)DreamUIWidgetPicking::PickTopmostWidget(TestWorld.World, {Root, Leaf}, Start, End, CycleIndex), (UObject*)Leaf);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPickingSkipsHiddenInDesignerTest,
	"DreamGUI.Editor.Picking.HiddenInDesignerIsNotPickable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPickingSkipsHiddenInDesignerTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FDreamUIWidgetPickHit> Hits;
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("visible to begin with"), Hits.Num(), 1);

	Root->SetHiddenInDesigner(true);
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("hidden in designer is not pickable"), Hits.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropResolvesToNearestContainerTest,
	"DreamGUI.Editor.Picking.DropResolvesToTheNearestContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDropResolvesToNearestContainerTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* Box = MakeWidget(TestWorld.World, Root, TEXT("Box"), 400.0f, 300.0f);
	Box->TrySetParent(Root, false);
	Box->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamWidget* Leaf = MakeWidget(TestWorld.World, Box, TEXT("Leaf"), 100.0f, 50.0f);
	Leaf->TrySetParent(Box, false);

	// Pointing at a Text means the box holding it, not the Text: dropping into a leaf would leave
	// the new widget somewhere nothing arranges it, which is exactly the reported symptom.
	TestEqual(TEXT("a leaf resolves to its box"), (UObject*)DreamUIWidgetPicking::ResolveDropContainer(Leaf), (UObject*)Box);
	TestEqual(TEXT("a container resolves to itself"), (UObject*)DreamUIWidgetPicking::ResolveDropContainer(Box), (UObject*)Box);
	TestEqual(TEXT("the root overlay resolves to itself"), (UObject*)DreamUIWidgetPicking::ResolveDropContainer(Root), (UObject*)Root);
	TestNull(TEXT("nothing under the cursor resolves to nothing"), (UObject*)DreamUIWidgetPicking::ResolveDropContainer(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDropSkipsFullContainerTest,
	"DreamGUI.Editor.Picking.DropSkipsAContainerWithNoRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDropSkipsFullContainerTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* SizeBox = MakeWidget(TestWorld.World, Root, TEXT("SizeBox"), 400.0f, 300.0f);
	SizeBox->TrySetParent(Root, false);
	SizeBox->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	// A content widget is what caps a SizeBox/ScaleBox/SafeZone at one child.
	SizeBox->AddComponent<UDreamContentWidget>();

	TestEqual(TEXT("an empty size box takes the drop"), (UObject*)DreamUIWidgetPicking::ResolveDropContainer(SizeBox), (UObject*)SizeBox);

	UDreamWidget* Occupant = MakeWidget(TestWorld.World, SizeBox, TEXT("Occupant"), 100.0f, 50.0f);
	Occupant->TrySetParent(SizeBox, false);
	if (!TestFalse(TEXT("the size box is now full"), SizeBox->CanAcceptAdditionalChildren()))
	{
		// Without the cap the next assertion would pass for the wrong reason.
		return false;
	}
	TestEqual(TEXT("a full container is skipped for the one above it"), (UObject*)DreamUIWidgetPicking::ResolveDropContainer(Occupant), (UObject*)Root);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPickingHitsVisualWidgetsTooTest,
	"DreamGUI.Editor.Picking.WidgetsThatDoDrawAreStillPicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPickingHitsVisualWidgetsTooTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	// Rect picking replaced mesh picking wholesale, so the case that used to work has to keep
	// working -- and a widget that draws is now picked over its whole rect, the way Slate and uGUI
	// pick, rather than only where its triangles are opaque.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Image = MakeWidget(TestWorld.World, Root, TEXT("Image"), 200.0f, 100.0f);
	Image->TrySetParent(Root, false);
	TestNotNull(TEXT("image visual"), (UObject*)Image->CreateNewVisual<UDreamImage>());

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FDreamUIWidgetPickHit> Hits;
	DreamUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root, Image}, Start, End, Hits);
	TestTrue(TEXT("the image is picked"), HitsContain(Hits, Image));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPickingStopsAtNestedInstanceTest,
	"DreamGUI.Editor.Picking.NothingInsideANestedInstanceIsPickable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPickingStopsAtNestedInstanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	// A canvas root, the host screen under it, and a nested widget blueprint instance with contents.
	// A bare UDreamUserWidget stands in for a compiled one: the boundary is decided by class and
	// ancestry, and nothing here asks what class the instance came from.
	UDreamWidget* CanvasRoot = MakeWidget(TestWorld.World, nullptr, TEXT("CanvasRoot"), 1920.0f, 1080.0f);
	CanvasRoot->AddComponent<UDreamCanvas>();
	UDreamUserWidget* HostScreen = NewObject<UDreamUserWidget>(TestWorld.World);
	HostScreen->SetDisplayName(TEXT("HostScreen"));
	HostScreen->SetParentBeforeRegister(CanvasRoot);
	UDreamWidget* HostPanel = MakeWidget(TestWorld.World, nullptr, TEXT("HostPanel"), 200.0f, 200.0f);
	HostPanel->SetParentBeforeRegister(HostScreen);
	UDreamUserWidget* Nested = NewObject<UDreamUserWidget>(TestWorld.World);
	Nested->SetDisplayName(TEXT("NestedControl"));
	Nested->SetParentBeforeRegister(HostPanel);
	UDreamWidget* InsideNested = MakeWidget(TestWorld.World, nullptr, TEXT("InsideNested"), 50.0f, 50.0f);
	InsideNested->SetParentBeforeRegister(Nested);
	RegisterDreamWidgetHierarchy(CanvasRoot);

	TArray<UDreamWidget*> Pickable;
	DreamUIWidgetPicking::CollectPickableWidgets(TestWorld.World, Pickable);

	TestTrue(TEXT("the canvas root is pickable"), Pickable.Contains(CanvasRoot));
	TestTrue(TEXT("the host screen is"), Pickable.Contains(HostScreen));
	TestTrue(TEXT("a widget the host owns is"), Pickable.Contains(HostPanel));
	TestTrue(TEXT("the nested instance itself is -- it is a widget of the host"), Pickable.Contains(Nested));
	// The half that makes folding real. If the panel shows a nested control as one row and the
	// viewport still selects the Text inside it, that selection has no row to highlight and no
	// template to migrate an edit onto: it looks like the details panel simply stopped working.
	TestFalse(TEXT("but nothing inside it is"), Pickable.Contains(InsideNested));

	CanvasRoot->DestroyWidget();
	return true;
}


#endif
