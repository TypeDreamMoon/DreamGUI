// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "Interaction/DreamUITooltip.h"
#include "UObject/UnrealType.h"
#include "DreamScopedWorld.h"

#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"

/*
 * The tooltip's pure half: which widget on the hover path owns the tooltip, and where the bubble
 * goes. Both are free functions on purpose (the DreamPointerPolicy convention) so they are testable
 * without a world, an event system, or a screen root.
 */

namespace DreamUITooltipTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamWidget* MakeWidget(const TCHAR* InDisplayName, UDreamWidget* InParent)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
		Widget->SetDisplayName(InDisplayName);
		if (InParent != nullptr)
		{
			// Parent links only; nothing here needs registration or a world.
			Widget->SetParentBeforeRegister(InParent);
		}
		return Widget;
	}

	FVector2D CentreOf(const UDreamWidget* InWidget)
	{
		const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(InWidget);
		return Pixel.IsSet() ? Pixel.GetValue() : FVector2D::ZeroVector;
	}

	/** Frames until player 0's tooltip is up for InWidget, or a generous ten seconds of them: the dwell is the settings'. */
	bool PumpUntilTooltipOf(FDreamDriverRig& InRig, const UDreamUITooltipSubsystem* InTooltip, const UDreamWidget* InWidget)
	{
		for (int32 Frame = 0; Frame < 600 && InTooltip->GetShownForUser(0) != InWidget; ++Frame)
		{
			InRig.PumpFrames(1);
		}
		return InTooltip->GetShownForUser(0) == InWidget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITooltipSourceResolutionTest,
	"DreamGUI.Tooltip.Policy.NearestAncestorWithTextOwnsTheTooltip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITooltipSourceResolutionTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITooltipTestLocal;

	UDreamWidget* Root = MakeWidget(TEXT("Root"), nullptr);
	UDreamWidget* Button = MakeWidget(TEXT("Button"), Root);
	UDreamWidget* InnerText = MakeWidget(TEXT("InnerText"), Button);
	Button->SetToolTipText(FText::FromString(TEXT("Confirms the order")));

	// The pointer lands on the button's inner text, but the BUTTON owns the tooltip: the visual
	// pixels a pointer hits are almost never the widget the author annotated.
	TestEqual(TEXT("Hovering the inner text resolves to the button"),
		DreamUITooltipPolicy::ResolveTooltipSource(InnerText), Button);
	TestEqual(TEXT("Hovering the button resolves to itself"),
		DreamUITooltipPolicy::ResolveTooltipSource(Button), Button);
	TestTrue(TEXT("Hovering the bare root resolves to nothing"),
		DreamUITooltipPolicy::ResolveTooltipSource(Root) == nullptr);
	TestTrue(TEXT("Null resolves to nothing"),
		DreamUITooltipPolicy::ResolveTooltipSource(nullptr) == nullptr);

	// An empty ToolTipText is "no tooltip", not "empty tooltip": the walk keeps climbing.
	Root->SetToolTipText(FText::FromString(TEXT("Root help")));
	Button->SetToolTipText(FText::GetEmpty());
	TestEqual(TEXT("An emptied ancestor is skipped for the one above it"),
		DreamUITooltipPolicy::ResolveTooltipSource(InnerText), Root);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITooltipPlacementTest,
	"DreamGUI.Tooltip.Policy.BubbleFlipsRatherThanSlidesAtCanvasEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITooltipPlacementTest::RunTest(const FString& Parameters)
{
	const FVector2D CanvasMin(-500.0f, -500.0f);
	const FVector2D CanvasMax(500.0f, 500.0f);
	const FVector2D Bubble(100.0f, 40.0f);
	const FVector2D Offset(18.0f, -22.0f);

	// Room on the preferred side: pivot goes exactly to pointer + offset.
	TestEqual(TEXT("Prefers below-right of the pointer"),
		DreamUITooltipPolicy::ComputeTooltipTopLeft(CanvasMin, CanvasMax, Bubble, FVector2D::ZeroVector, Offset),
		FVector2D(18.0f, -22.0f));

	// Out of room on the right: flip to the pointer's LEFT, not a slide that would sit under it.
	TestEqual(TEXT("Flips to the left at the right edge"),
		DreamUITooltipPolicy::ComputeTooltipTopLeft(CanvasMin, CanvasMax, Bubble, FVector2D(460.0f, 0.0f), Offset),
		FVector2D(460.0f - 18.0f - 100.0f, -22.0f));

	// Out of room below: flip above the pointer.
	TestEqual(TEXT("Flips above at the bottom edge"),
		DreamUITooltipPolicy::ComputeTooltipTopLeft(CanvasMin, CanvasMax, Bubble, FVector2D(0.0f, -460.0f), Offset),
		FVector2D(18.0f, -460.0f + 22.0f + 40.0f));

	// A bubble bigger than the canvas cannot flip its way out; it clamps and stays on screen.
	const FVector2D Clamped = DreamUITooltipPolicy::ComputeTooltipTopLeft(
		CanvasMin, CanvasMax, FVector2D(2000.0f, 2000.0f), FVector2D::ZeroVector, Offset);
	TestEqual(TEXT("An oversized bubble pins to the left edge"), Clamped.X, CanvasMin.X);
	TestEqual(TEXT("An oversized bubble pins to the top edge"), Clamped.Y, CanvasMax.Y);
	return true;
}

/*
 * The bubble's lifetime, which used to be tied to the wrong thing.
 *
 * A tooltip is shown FOR a widget, and the subsystem kept that widget in a weak pointer. The tick
 * then asked "is the source still valid?" to decide whether it had a bubble to look after -- so the
 * moment the source went away, the answer became "no bubble", both branches of the tick returned
 * early, and nothing ever called HideTooltip. The bubble stayed parked on the screen root, at the
 * last position it had, describing a widget that no longer existed, until some LATER tooltip's
 * ShowFor happened to destroy it on its way past.
 *
 * Sources go away constantly. UIRecyclableScrollView recycles the hovered row out from under the
 * pointer; a screen closes while its button is hovered. Both leave a bubble on screen for good.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITooltipOutlivesItsSourceTest,
	"DreamGUI.Tooltip.ABubbleWhoseSourceIsDestroyedIsTakenDownWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITooltipOutlivesItsSourceTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;

	UDreamUITooltipSubsystem* Tooltip = TestWorld.World->GetSubsystem<UDreamUITooltipSubsystem>();
	if (!TestNotNull(TEXT("the tooltip subsystem exists in a game world"), Tooltip))
	{
		return false;
	}

	// The bubble itself: everything drawn hangs off it, and it is what hiding destroys. Asked for as the bubble, not
	// through GetShownFor -- a test that asserted only on GetShownFor would have passed all along, because ShownFor
	// going stale IS the bug rather than a symptom of it.
	auto Bubble = [Tooltip]() { return Tooltip->GetBubbleForUser(0); };

	UDreamWidget* Source = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	Source->SetDisplayName(TEXT("TooltipSource"));
	Source->SetWidth(120.0f);
	Source->SetHeight(40.0f);
	Source->SetToolTipText(FText::FromString(TEXT("Explains what this does")));
	Source->OnRegister();

	Tooltip->ShowTooltipFor(Source);
	TestEqual(TEXT("the bubble belongs to the widget it was shown for"), Tooltip->GetShownFor(), Source);
	if (!TestNotNull(TEXT("...and a bubble was actually built"), Bubble()))
	{
		Source->DestroyWidget();
		return false;
	}

	// A tick with the source still alive changes nothing: the bubble is re-measured and repositioned,
	// which is the state this test has to distinguish "tore it down" from.
	Tooltip->Tick(0.016f);
	TestNotNull(TEXT("an ordinary tick leaves a live tooltip alone"), Bubble());

	// The recycled row / closed screen. Both halves are needed to reproduce it: DestroyWidget tears
	// the widget down but leaves the object addressable, and it is the COLLECTION that follows -- a
	// recycled row dropping its last reference -- that turns the subsystem's weak ShownFor into a
	// null, which is the exact state both branches of the tick used to walk straight past.
	Source->DestroyWidget();
	Source->MarkAsGarbage();
	TestNull(TEXT("the source really is gone"), Tooltip->GetShownFor());

	Tooltip->Tick(0.016f);
	TestNull(TEXT("the bubble is destroyed rather than left parked on the screen root"), Bubble());

	// And the subsystem is left in a state a later tooltip can use, rather than one where it believes
	// a bubble is still up.
	Tooltip->Tick(0.016f);
	TestNull(TEXT("a second tick has nothing left to do"), Bubble());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITooltipWorldSpaceHostTest,
	"DreamGUI.Tooltip.Policy.AWorldSpacePanelsTooltipStaysOnItsOwnCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITooltipWorldSpaceHostTest::RunTest(const FString& Parameters)
{
	/*
	 * The screen overlay was the only host a tooltip ever had. For a panel welded to a machine in the
	 * level that put the bubble on the player's HUD instead -- and positioned it from a pointer
	 * position no world-space raycaster fills in meaningfully, so it landed wherever the last mouse
	 * event happened to have been. A tooltip belongs on the same canvas as the thing it is about.
	 */
	DreamTests::FScopedGameWorld TestWorld;

	UDreamWidget* ScreenRoot = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	ScreenRoot->SetDisplayName(TEXT("ScreenRootStandIn"));
	ScreenRoot->OnRegister();

	// No source at all is the screen root: that is what a tooltip with nothing to be about would get,
	// and the policy must not answer null for it.
	TestEqual(TEXT("with no source, the screen root hosts"),
		DreamUITooltipPolicy::ResolveTooltipHost(nullptr, ScreenRoot), ScreenRoot);

	UDreamWidget* WorldRoot = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	WorldRoot->SetDisplayName(TEXT("WorldCanvas"));
	WorldRoot->SetWidth(400.0f);
	WorldRoot->SetHeight(300.0f);
	WorldRoot->OnRegister();
	UDreamCanvas* Canvas = WorldRoot->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("a canvas for the world-space tree"), Canvas))
	{
		return false;
	}
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);

	UDreamWidget* Panel = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	Panel->SetDisplayName(TEXT("Panel"));
	Panel->SetWidth(120.0f);
	Panel->SetHeight(60.0f);
	Panel->TrySetParent(WorldRoot, false);

	// The precondition, asserted rather than assumed: the policy falls back to the screen root when a
	// source has no canvas, and a fixture that quietly had none would make the next line meaningless.
	if (!TestFalse(TEXT("the panel is not screen-space UI"), Panel->IsScreenSpaceOverlayUI())
		|| !TestNotNull(TEXT("...and it does have a root canvas"), Panel->GetRootCanvas()))
	{
		WorldRoot->DestroyWidget();
		ScreenRoot->DestroyWidget();
		return false;
	}

	TestEqual(TEXT("a world-space panel's tooltip is hosted by its own canvas"),
		DreamUITooltipPolicy::ResolveTooltipHost(Panel, ScreenRoot), WorldRoot);

	WorldRoot->DestroyWidget();
	ScreenRoot->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITooltipPointerLeavesTest,
	"DreamGUI.Tooltip.ABubbleGoesWhenThePointerLeavesItsWidgetForTheParentOrForEmptySpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamUITooltipPointerLeavesTest, "DreamGUI.Tooltip.ABubbleGoesWhenThePointerLeavesItsWidgetForTheParentOrForEmptySpace", "[Pointer][Animated]")

/*
 * Which widget the tooltip is for was read off the pointer as each Enter and Exit went out. An Exit goes out while the
 * pointer still names the widget being left, and a pointer that leaves for empty space -- or for the parent it was inside
 * all along -- is sent no Enter afterwards to say where it went: the widget it had left stayed the one the tooltip was
 * for, and its bubble stayed open over nothing. What the pointer is over is now read once the frame's exits and enters
 * are all out. Checked with the driver's mouse on a child with a tooltip, inside a panel with one of its own: left for
 * the panel, then for empty space.
 */
bool FDreamUITooltipPointerLeavesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITooltipTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable() && Rig.InputModule() != nullptr))
	{
		return false;
	}
	UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
	UDreamWidget* Panel = Rig.MakeWidget(TEXT("Panel"), nullptr, FVector2D(400.0, 300.0), FVector2D(-200.0, 0.0));
	UDreamWidget* Child = IsValid(Panel) ? Rig.MakeWidget(TEXT("Child"), Panel, FVector2D(120.0, 60.0)) : nullptr;
	if (!TestTrue(TEXT("A tooltip service, and a panel with a child in it"), Tooltip != nullptr && IsValid(Child)))
	{
		return false;
	}
	Panel->SetToolTipText(FText::FromString(TEXT("About the panel")));
	Child->SetToolTipText(FText::FromString(TEXT("About the child")));
	Rig.PumpFrames(2);
	UDreamDriverInputModule* Module = Rig.InputModule();
	const FVector2D OnChild = CentreOf(Child);
	const FVector2D OnPanelOnly = OnChild + FVector2D(0.0, 100.0);

	Module->MoveTo(OnChild);
	if (!TestTrue(TEXT("Resting on the child brings its tooltip up"), PumpUntilTooltipOf(Rig, Tooltip, Child)))
	{
		return false;
	}

	// Onto the panel around it: the child is left, and nothing is entered -- the pointer was in the panel all along.
	Module->MoveTo(OnPanelOnly);
	Rig.PumpFrames(2);
	TestTrue(TEXT("Moving off the child onto its panel takes the child's tooltip down"), Tooltip->GetShownForUser(0) != Child);
	TestTrue(TEXT("...and the panel's comes up after its own dwell"), PumpUntilTooltipOf(Rig, Tooltip, Panel));

	// Out to empty space.
	Module->MoveTo(FVector2D(ViewportSize.X - 40.0, ViewportSize.Y - 40.0));
	Rig.PumpFrames(2);
	TestNull(TEXT("Moving off the panel onto nothing takes its tooltip down"), Tooltip->GetShownForUser(0));
	TestNull(TEXT("...bubble and all"), Tooltip->GetBubbleForUser(0));
	Rig.PumpFrames(60);
	TestNull(TEXT("...and nothing comes back while the pointer rests on nothing"), Tooltip->GetBubbleForUser(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITooltipOtherPointerTest,
	"DreamGUI.Tooltip.AnotherPointerOfThePlayersLeavesTheMousesTooltipUpUntilItReachesATooltipOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamUITooltipOtherPointerTest, "DreamGUI.Tooltip.AnotherPointerOfThePlayersLeavesTheMousesTooltipUpUntilItReachesATooltipOfItsOwn", "[Pointer][Animated]")

/*
 * A player has one tooltip, and every pointer of the player's moved it: a second pointer -- a finger, a second laser, a
 * pointer of a script's own -- arriving at a widget with no tooltip, or leaving one, took down the bubble the mouse had
 * brought up. The bubble now follows one pointer, the one that last arrived at something with a tooltip; another pointer
 * takes it over by arriving at a tooltip of its own, and only so. Checked with the mouse resting on a widget with a
 * tooltip and a pointer of the test's own moving about the player's screen.
 */
bool FDreamUITooltipOtherPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITooltipTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable() && Rig.InputModule() != nullptr))
	{
		return false;
	}
	UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	UDreamWidget* Help = Rig.MakeWidget(TEXT("Help"), nullptr, FVector2D(200.0, 100.0), FVector2D(-300.0, 0.0));
	UDreamWidget* Plain = Rig.MakeWidget(TEXT("Plain"), nullptr, FVector2D(200.0, 100.0), FVector2D(300.0, 0.0));
	UDreamWidget* Other = Rig.MakeWidget(TEXT("Other"), nullptr, FVector2D(200.0, 100.0), FVector2D(0.0, -200.0));
	if (!TestTrue(TEXT("A tooltip service, a player, and three widgets"),
		Tooltip != nullptr && User != nullptr && IsValid(Help) && IsValid(Plain) && IsValid(Other)))
	{
		return false;
	}
	Help->SetToolTipText(FText::FromString(TEXT("About the help")));
	Other->SetToolTipText(FText::FromString(TEXT("About the other")));
	Rig.PumpFrames(2);

	Rig.InputModule()->MoveTo(CentreOf(Help));
	if (!TestTrue(TEXT("The mouse resting on a widget brings its tooltip up"), PumpUntilTooltipOf(Rig, Tooltip, Help)))
	{
		return false;
	}

	const int32 SecondPointer = DreamUIPointerIds::ScriptBase;
	User->MovePointer(SecondPointer, FVector(CentreOf(Plain), 0.0));
	Rig.PumpFrames(2);
	TestEqual(TEXT("Another pointer arriving at a widget with no tooltip leaves the mouse's up"), Tooltip->GetShownForUser(0), Help);
	User->MovePointer(SecondPointer, FVector(ViewportSize.X - 40.0, ViewportSize.Y - 40.0, 0.0));
	Rig.PumpFrames(2);
	TestEqual(TEXT("...and so does its leaving that widget for nothing"), Tooltip->GetShownForUser(0), Help);
	TestNotNull(TEXT("...bubble and all"), Tooltip->GetBubbleForUser(0));

	User->MovePointer(SecondPointer, FVector(CentreOf(Other), 0.0));
	TestTrue(TEXT("Arriving at a tooltip of its own, the other pointer takes the tooltip over"), PumpUntilTooltipOf(Rig, Tooltip, Other));
	User->RetirePointer(SecondPointer);
	Rig.PumpFrames(1);
	return true;
}

#endif
