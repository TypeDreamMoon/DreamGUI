// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexVisualEmpty.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"

/*
 * The "created but not yet added" state, which a UMG-style CreateWidget hands back.
 *
 * UMG gets this state for free: a UUserWidget is a UObject and the thing that draws is a separate
 * SWidget that does not exist until something calls TakeWidget(). This fork has no such split --
 * ULexWidget IS the drawn thing -- so the state has to be built rather than inherited, and these
 * pin the properties it has to have. They are written to fail loudly if the state turns out to be
 * livelier than it looks, because everything downstream assumes it is inert.
 */

namespace LexDetachedWidgetTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** The sequence a CreateWidget-style verb is expected to run: object, subobjects, register, park. */
	ULexWidget* MakeParkedWidget(UWorld* World, const TCHAR* Name, bool bWithOwnCanvas = false)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		if (bWithOwnCanvas)
		{
			Widget->AddComponent<ULexCanvas>();
		}
		Widget->OnRegister();
		Widget->SetWidgetActive(false);
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedWidgetIsInertTest,
	"LGUI.Widget.Parked.RegisteredButInactiveIsInert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedWidgetIsInertTest::RunTest(const FString& Parameters)
{
	using namespace LexDetachedWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Parked = MakeParkedWidget(TestWorld.World, TEXT("Parked"));

	// Registration is what anchors the widget against GC (the manager's AllWidgetArray is the only
	// UPROPERTY holding it) and what makes OnAttachedToParent recompute anchors later, so the parked
	// state must keep it -- inertness has to come from somewhere else.
	TestTrue(TEXT("A parked widget is registered"), Parked->HasRegistered());
	if (ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World))
	{
		TestTrue(TEXT("A parked widget is anchored by the manager"),
			Manager->GetAllWidgetArray().Contains(Parked));
	}
	else
	{
		AddError(TEXT("No manager for the test world; the rest of this test would be vacuous."));
		return false;
	}

	// Inertness comes from the active flag: it is what ULexUIBehaviour::BeginPlay gates OnEnable and
	// ticking on. Awake still runs, which is the right analogue of UMG firing NativeOnInitialized at
	// CreateWidget time but withholding Construct until the widget is added.
	TestFalse(TEXT("A parked widget is not active in hierarchy"), Parked->GetWidgetActiveInHierarchy());
	TestNull(TEXT("A parked widget has no render canvas"), Parked->GetRenderCanvas());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedWidgetSubObjectsTest,
	"LGUI.Widget.Parked.SubObjectsCanStillBeBuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedWidgetSubObjectsTest::RunTest(const FString& Parameters)
{
	using namespace LexDetachedWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Parked = MakeParkedWidget(TestWorld.World, TEXT("Parked"));

	// The whole point of handing back a parked widget is that the caller configures it before adding
	// it. CreateNewLayoutContainer and AddComponent both Call_OnRegister their subobject
	// unconditionally, so this is the question that decides whether "park" can mean "registered".
	ULexLayoutContainer* Container = Parked->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	if (TestNotNull(TEXT("A parked widget accepts a layout container"), Container))
	{
		TestTrue(TEXT("The container registers"), Container->IsRegistered());
	}
	ULexVisual* Visual = Parked->CreateNewVisual<ULexVisualEmpty>();
	if (TestNotNull(TEXT("A parked widget accepts a visual"), Visual))
	{
		// No ancestor canvas means nothing collects this visual, which is exactly why a parked
		// widget draws nothing without anyone having to hide it.
		TestNull(TEXT("The visual has no canvas to render through"), Parked->GetRenderCanvas());
	}

	ULexWidget* Child = MakeParkedWidget(TestWorld.World, TEXT("Child"));
	TestTrue(TEXT("A parked widget accepts children"), Child->TrySetParent(Parked, false));
	TestNotNull(TEXT("The child gets a panel slot under a parked panel"), Child->GetPanelSlot());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedWidgetOwnCanvasTest,
	"LGUI.Widget.Parked.OwnCanvasDoesNotRenderWhileParked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedWidgetOwnCanvasTest::RunTest(const FString& Parameters)
{
	using namespace LexDetachedWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// The trap: OnHierarchyAttachmentChanged takes the widget's OWN canvas as its render canvas when
	// it has one, so a page prefab -- whose root almost always carries a ULexCanvas -- becomes a
	// render root the moment it exists, with no parent required. Creating one to show later would
	// otherwise put it on screen immediately, in its authored render mode.
	ULexWidget* Parked = MakeParkedWidget(TestWorld.World, TEXT("ParkedPage"), /*bWithOwnCanvas*/true);
	ULexCanvas* OwnCanvas = Parked->GetComponent<ULexCanvas>();
	if (!TestNotNull(TEXT("The parked page has its own canvas"), OwnCanvas))return false;

	ULexVisual* Visual = Parked->CreateNewVisual<ULexVisualEmpty>();
	if (!TestNotNull(TEXT("The parked page has a visual"), Visual))return false;

	// Being in the canvas's VisualList is only registration; the draw decision is made per frame,
	// and LexCanvas gates UpdateVisual on GetRenderVisibleInHierarchy -- which folds in the active
	// flag. That is the assertion that actually means "nothing appears on screen".
	TestFalse(TEXT("A parked page is not active"), Parked->GetWidgetActiveInHierarchy());
	TestFalse(TEXT("A parked page is not render-visible, so nothing draws it"),
		Parked->GetRenderVisibleInHierarchy());

	// The other half of the trap: the editor's "only one ScreenSpace UI in a world" check counts
	// root overlay canvases, and a parked page's canvas is a root canvas by virtue of having no
	// parent. It must not be counted as competing for a screen it is not on.
	OwnCanvas->SetRenderMode(ELexRenderMode::ScreenSpaceOverlay);
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}
	TestEqual(TEXT("A parked overlay page does not count as competing for the screen"),
		Manager->CountCompetingScreenSpaceOverlayCanvases(), 0);

	// Control: once activated it is on screen, draws, and does count.
	Parked->SetWidgetActive(true);
	TestTrue(TEXT("Once activated the page is render-visible"), Parked->GetRenderVisibleInHierarchy());
	TestEqual(TEXT("Once activated it does count as competing"),
		Manager->CountCompetingScreenSpaceOverlayCanvases(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetRegisterIsIdempotentTest,
	"LGUI.Widget.Parked.RegisterTwiceDoesNotLeakAPropertySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetRegisterIsIdempotentTest::RunTest(const FString& Parameters)
{
	using namespace LexDetachedWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// A creation verb cannot know whether its caller already registered the widget, so OnRegister has
	// to tolerate a second call. Most of it already does -- AddWidget is an AddUnique, Call_OnRegister
	// early-outs -- but ULexCanvas::RegisterVisual calls RegisterBuffer() unconditionally and
	// overwrites the visual's recorded position, so the slot it held is never returned to the free
	// list. One leaked row of the property-data texture per redundant registration.
	ULexWidget* Root = MakeParkedWidget(TestWorld.World, TEXT("Root"), /*bWithOwnCanvas*/true);
	Root->SetWidgetActive(true);

	ULexWidget* Child = MakeParkedWidget(TestWorld.World, TEXT("Child"));
	ULexVisual* Visual = Child->CreateNewVisual<ULexVisualEmpty>();
	if (!TestNotNull(TEXT("The child has a visual"), Visual))return false;
	if (!TestTrue(TEXT("The child attaches under the canvas"), Child->TrySetParent(Root, false)))return false;
	if (!TestNotNull(TEXT("The child now renders through the root canvas"), Child->GetRenderCanvas()))return false;

	const int32 PositionBefore = Visual->GetWidgetPropertyDataStartPosition();
	TestTrue(TEXT("The visual holds a property slot"), PositionBefore > INDEX_NONE);

	Child->OnRegister();

	TestEqual(TEXT("A second OnRegister does not hand the visual a different slot"),
		Visual->GetWidgetPropertyDataStartPosition(), PositionBefore);
	TestTrue(TEXT("The widget is still registered exactly once"),
		Child->GetRenderCanvas()->GetVisualArray().Num() == 1);
	return true;
}

#endif
