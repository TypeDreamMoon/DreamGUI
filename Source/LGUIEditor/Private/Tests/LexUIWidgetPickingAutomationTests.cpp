// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "LexUIWidgetPicking.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Interaction/LexContentWidget.h"
#include "Core/Components/LexImage.h"
#include "Engine/World.h"

// The designer used to ask ULexUIManagerWorldSubsystem::RaycastHitUI what was under the cursor, and
// that function starts from `if (auto Visual = Widget->GetVisual())`. Every UMG-family layout panel
// is registered without a visual, so under that question an Overlay does not exist -- it could not
// be clicked, and a drop aimed at it silently landed on the prefab root instead, which is why a
// Button dropped "into" an Overlay kept its authored size. These tests pin the other question.
namespace LexUIWidgetPickingTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, UObject* Outer, const TCHAR* Name, float Width, float Height)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Outer ? Outer : (UObject*)World);
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

	bool HitsContain(const TArray<FLexUIWidgetPickHit>& Hits, const ULexWidget* Widget)
	{
		return Hits.ContainsByPredicate([Widget](const FLexUIWidgetPickHit& Hit) { return Hit.Widget == Widget; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPickingHitsPanelWithNoVisualTest,
	"LGUI.Editor.Picking.PanelWithNoVisualIsStillUnderTheCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPickingHitsPanelWithNoVisualTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	TestNotNull(TEXT("overlay container"), (UObject*)Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>());

	// This is the whole bug in one assertion: an Overlay draws nothing, and the old hit test
	// required something drawn. If this ever starts returning a visual the test has stopped
	// covering the case it was written for.
	TestNull(TEXT("an overlay has no visual"), (UObject*)Root->GetVisual());

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FLexUIWidgetPickHit> Hits;
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);

	TestEqual(TEXT("the overlay is under the cursor"), Hits.Num(), 1);
	TestTrue(TEXT("and it is the overlay"), HitsContain(Hits, Root));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPickingMissesOutsideTheRectTest,
	"LGUI.Editor.Picking.RayOutsideTheRectHitsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPickingMissesOutsideTheRectTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();

	// Centre pivot, so the rect spans [-400, 400] x [-300, 300]. Just outside the right edge.
	FVector Start, End;
	RayAt(401.0f, 0.0f, Start, End);
	TArray<FLexUIWidgetPickHit> Hits;
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("outside the right edge"), Hits.Num(), 0);

	RayAt(0.0f, 301.0f, Start, End);
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("above the top edge"), Hits.Num(), 0);

	RayAt(399.0f, 299.0f, Start, End);
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("just inside the corner"), Hits.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPickingPrefersTheChildTest,
	"LGUI.Editor.Picking.ChildIsPickedBeforeItsPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPickingPrefersTheChildTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	ULexWidget* Leaf = MakeWidget(TestWorld.World, Root, TEXT("Leaf"), 100.0f, 50.0f);
	TestTrue(TEXT("attach"), Leaf->TrySetParent(Root, false));

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FLexUIWidgetPickHit> Hits;
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root, Leaf}, Start, End, Hits);

	TestEqual(TEXT("both rects contain the point"), Hits.Num(), 2);
	if (Hits.Num() == 2)
	{
		// Front-to-back means deepest first, so clicking a button inside a panel selects the button.
		TestEqual(TEXT("the child is in front"), (UObject*)Hits[0].Widget, (UObject*)Leaf);
		TestEqual(TEXT("the panel is behind it"), (UObject*)Hits[1].Widget, (UObject*)Root);
	}

	// Repeating the same pick walks deeper, so the panel underneath stays reachable.
	int32 CycleIndex = INDEX_NONE;
	TestEqual(TEXT("first click takes the child"), (UObject*)LexUIWidgetPicking::PickTopmostWidget(TestWorld.World, {Root, Leaf}, Start, End, CycleIndex), (UObject*)Leaf);
	TestEqual(TEXT("second click takes the panel"), (UObject*)LexUIWidgetPicking::PickTopmostWidget(TestWorld.World, {Root, Leaf}, Start, End, CycleIndex), (UObject*)Root);
	TestEqual(TEXT("third click wraps back to the child"), (UObject*)LexUIWidgetPicking::PickTopmostWidget(TestWorld.World, {Root, Leaf}, Start, End, CycleIndex), (UObject*)Leaf);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPickingSkipsHiddenInDesignerTest,
	"LGUI.Editor.Picking.HiddenInDesignerIsNotPickable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPickingSkipsHiddenInDesignerTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FLexUIWidgetPickHit> Hits;
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("visible to begin with"), Hits.Num(), 1);

	Root->SetHiddenInDesigner(true);
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root}, Start, End, Hits);
	TestEqual(TEXT("hidden in designer is not pickable"), Hits.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDropResolvesToNearestContainerTest,
	"LGUI.Editor.Picking.DropResolvesToTheNearestContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDropResolvesToNearestContainerTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	ULexWidget* Box = MakeWidget(TestWorld.World, Root, TEXT("Box"), 400.0f, 300.0f);
	Box->TrySetParent(Root, false);
	Box->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexWidget* Leaf = MakeWidget(TestWorld.World, Box, TEXT("Leaf"), 100.0f, 50.0f);
	Leaf->TrySetParent(Box, false);

	// Pointing at a Text means the box holding it, not the Text: dropping into a leaf would leave
	// the new widget somewhere nothing arranges it, which is exactly the reported symptom.
	TestEqual(TEXT("a leaf resolves to its box"), (UObject*)LexUIWidgetPicking::ResolveDropContainer(Leaf), (UObject*)Box);
	TestEqual(TEXT("a container resolves to itself"), (UObject*)LexUIWidgetPicking::ResolveDropContainer(Box), (UObject*)Box);
	TestEqual(TEXT("the root overlay resolves to itself"), (UObject*)LexUIWidgetPicking::ResolveDropContainer(Root), (UObject*)Root);
	TestNull(TEXT("nothing under the cursor resolves to nothing"), (UObject*)LexUIWidgetPicking::ResolveDropContainer(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDropSkipsFullContainerTest,
	"LGUI.Editor.Picking.DropSkipsAContainerWithNoRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDropSkipsFullContainerTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	ULexWidget* SizeBox = MakeWidget(TestWorld.World, Root, TEXT("SizeBox"), 400.0f, 300.0f);
	SizeBox->TrySetParent(Root, false);
	SizeBox->CreateNewLayoutContainer<ULexLayoutContainerSizeBox>();
	// A content widget is what caps a SizeBox/ScaleBox/SafeZone at one child.
	SizeBox->AddComponent<ULexContentWidget>();

	TestEqual(TEXT("an empty size box takes the drop"), (UObject*)LexUIWidgetPicking::ResolveDropContainer(SizeBox), (UObject*)SizeBox);

	ULexWidget* Occupant = MakeWidget(TestWorld.World, SizeBox, TEXT("Occupant"), 100.0f, 50.0f);
	Occupant->TrySetParent(SizeBox, false);
	if (!TestFalse(TEXT("the size box is now full"), SizeBox->CanAcceptAdditionalChildren()))
	{
		// Without the cap the next assertion would pass for the wrong reason.
		return false;
	}
	TestEqual(TEXT("a full container is skipped for the one above it"), (UObject*)LexUIWidgetPicking::ResolveDropContainer(Occupant), (UObject*)Root);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPickingHitsVisualWidgetsTooTest,
	"LGUI.Editor.Picking.WidgetsThatDoDrawAreStillPicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPickingHitsVisualWidgetsTooTest::RunTest(const FString& Parameters)
{
	using namespace LexUIWidgetPickingTestLocal;
	FScopedTestWorld TestWorld;

	// Rect picking replaced mesh picking wholesale, so the case that used to work has to keep
	// working -- and a widget that draws is now picked over its whole rect, the way Slate and uGUI
	// pick, rather than only where its triangles are opaque.
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Image = MakeWidget(TestWorld.World, Root, TEXT("Image"), 200.0f, 100.0f);
	Image->TrySetParent(Root, false);
	TestNotNull(TEXT("image visual"), (UObject*)Image->CreateNewVisual<ULexImage>());

	FVector Start, End;
	RayAt(0.0f, 0.0f, Start, End);
	TArray<FLexUIWidgetPickHit> Hits;
	LexUIWidgetPicking::RaycastWidgetRects(TestWorld.World, {Root, Image}, Start, End, Hits);
	TestTrue(TEXT("the image is picked"), HitsContain(Hits, Image));
	return true;
}

#endif
