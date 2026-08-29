// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// Two answers the drag gestures reach through a widget that is not the answer.
//
// A drop resolves UP from the pixel to the nearest ancestor holding a container, so the widget the
// lock is asked about has to be that ancestor: asking only the hit leaves every locked panel open
// through any unlocked child it holds. And when nothing above the cursor holds a container -- the
// container-less prefab -- the drop has the same answer the palette's own drop has always had, the
// prefab root, or the two drags mean different things behind one orange outline.
//
// A snapped multi-widget move shares one correction so the selection keeps its shape, but a widget
// whose X an arranger owns is never handed the X measured for it, so a correction read off that X
// bends everyone else towards a gridline nothing was going to sit on.
namespace DreamSurfaceAuditTestLocal
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
	FDreamWidgetDesignerViewportClient::FMoveDragTarget MakeTarget(const FVector2D& InStartPosition, const FVector& InTravel)
	{
		FDreamWidgetDesignerViewportClient::FMoveDragTarget Target;
		Target.PlaneTransform = FTransform::Identity;
		Target.StartPlanePoint = FVector::ZeroVector;
		Target.CurrentPlanePoint = InTravel;
		Target.StartPosition = InStartPosition;
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceAuditDropContainerTest,
	"DreamGUI.Editor.SurfaceAudit.DragDropAsksTheContainerThatReceivesNotThePixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSurfaceAuditDropContainerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurfaceAuditTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f));
	Root->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TStrongObjectPtr<UDreamWidget> Panel(MakeWidget(Root.Get(), TEXT("Panel"), 200.0f, 200.0f));
	Panel->TrySetParent(Root.Get(), false);
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	// A plain widget with no container of its own: the pixel a cursor over the panel's contents
	// actually hits, and never the widget a drop can land in.
	TStrongObjectPtr<UDreamWidget> Label(MakeWidget(Panel.Get(), TEXT("Label"), 50.0f, 20.0f));
	Label->TrySetParent(Panel.Get(), false);
	TStrongObjectPtr<UDreamWidget> Mover(MakeWidget(Root.Get(), TEXT("Mover"), 100.0f, 50.0f));
	Mover->TrySetParent(Root.Get(), false);

	TArray<UDreamWidget*> Dragged = { Mover.Get() };
	TSet<const UDreamWidget*> Locked;
	auto IsLocked = [&Locked](const UDreamWidget* InWidget) { return Locked.Contains(InWidget); };

	TestSamePtr(TEXT("a hit inside a panel resolves to the panel that will arrange the drop"),
		FDreamWidgetDesignerViewportClient::ResolveDragDropContainer(Label.Get(), Root.Get(), Dragged, IsLocked), Panel.Get());

	Locked.Add(Panel.Get());
	TestNull(TEXT("and a locked panel stays shut when the cursor is over an unlocked child of it"),
		FDreamWidgetDesignerViewportClient::ResolveDragDropContainer(Label.Get(), Root.Get(), Dragged, IsLocked));
	TestNull(TEXT("as it does when the cursor is on the panel itself"),
		FDreamWidgetDesignerViewportClient::ResolveDragDropContainer(Panel.Get(), Root.Get(), Dragged, IsLocked));
	Locked.Reset();

	// A prefab whose root carries no container at all: the resolve walks the whole chain and finds
	// nothing, which is the case the palette drop has always answered with the root.
	TStrongObjectPtr<UDreamWidget> BareRoot(MakeWidget(TestWorld.World, TEXT("BareRoot"), 800.0f, 600.0f));
	TStrongObjectPtr<UDreamWidget> BareChild(MakeWidget(BareRoot.Get(), TEXT("BareChild"), 100.0f, 100.0f));
	BareChild->TrySetParent(BareRoot.Get(), false);

	TestSamePtr(TEXT("a chain with no container anywhere drops on the prefab root"),
		FDreamWidgetDesignerViewportClient::ResolveDragDropContainer(BareChild.Get(), BareRoot.Get(), Dragged, IsLocked), BareRoot.Get());
	Locked.Add(BareRoot.Get());
	TestNull(TEXT("unless the root is locked too"),
		FDreamWidgetDesignerViewportClient::ResolveDragDropContainer(BareChild.Get(), BareRoot.Get(), Dragged, IsLocked));
	Locked.Reset();

	// The hierarchy's own refusals still have the last word over whatever was resolved.
	TArray<UDreamWidget*> DraggingThePanel = { Panel.Get() };
	TestNull(TEXT("and a widget is not dropped into itself however the container was reached"),
		FDreamWidgetDesignerViewportClient::ResolveDragDropContainer(Label.Get(), Root.Get(), DraggingThePanel, IsLocked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceAuditMoveDragAxisLeaderTest,
	"DreamGUI.Editor.SurfaceAudit.MoveDragSnapsOffAWidgetThatMayHaveTheAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSurfaceAuditMoveDragAxisLeaderTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurfaceAuditTestLocal;

	// Two widgets dragged five units on a grid of eight. The first is arranged in X, so its own X
	// lands nowhere; the second starts two units along, and is the one whose X the grid may bend.
	TArray<FDreamWidgetDesignerViewportClient::FMoveDragTarget> Horizontal;
	Horizontal.Add(MakeTarget(FVector2D(0.0, 0.0), FVector(0.0, 5.0, 0.0)));
	Horizontal[0].bHorizontalFree = false;
	Horizontal.Add(MakeTarget(FVector2D(2.0, 0.0), FVector(0.0, 5.0, 0.0)));

	TArray<FDreamWidgetDesignerViewportClient::FMoveDragResult> Results;
	FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Horizontal, 8.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;
	TestTrue(TEXT("the arranged widget keeps the X it was given"), Results[0].Position.Equals(FVector2D(0.0, 0.0)));
	// Correcting by the arranged widget's three units instead puts this one on 10, between gridlines.
	TestTrue(TEXT("and the widget that may move lands on the gridline"), Results[1].Position.Equals(FVector2D(8.0, 0.0)));

	TArray<FDreamWidgetDesignerViewportClient::FMoveDragTarget> Vertical;
	Vertical.Add(MakeTarget(FVector2D(0.0, 0.0), FVector(0.0, 0.0, 5.0)));
	Vertical[0].bVerticalFree = false;
	Vertical.Add(MakeTarget(FVector2D(0.0, 2.0), FVector(0.0, 0.0, 5.0)));

	FDreamWidgetDesignerViewportClient::ResolveMoveDrag(Vertical, 8.0f, Results);
	if (!TestEqual(TEXT("one result per target"), Results.Num(), 2))return false;
	TestTrue(TEXT("the same on the other axis, which is decided separately"), Results[0].Position.Equals(FVector2D(0.0, 0.0)));
	TestTrue(TEXT("so a widget arranged in Y cannot bend anyone else's Y"), Results[1].Position.Equals(FVector2D(0.0, 8.0)));
	return true;
}

#endif
