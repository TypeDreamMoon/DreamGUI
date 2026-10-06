// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIEventTrigger.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "WaitUntil.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Pie/DreamPieTestTypes.h"

/*
 * THE POINTER ON THE FRAME AFTER A RECOMPILE.
 *
 * A class recompiled while its trees are alive takes them down before the compile and builds them again from the new
 * class a tick after (DreamGUI.Lifecycle.RecompilingAClassWhileItsTreesAreAliveBuildsEachOfThemAgainFromTheNewClass): a
 * page on the player's screen comes back where it was. What those tests do not do is point at it. UMG's own recompile in
 * a running session reinstances the widgets and Slate works out what is under a resting cursor again on the next frame
 * (FSlateUser::SynthesizeCursorMoveIfNeeded), so the cursor that rested on the old widget is over the new one, and the
 * next click is the new widget's. These hold DreamGUI to that: the pointer resting on a page while its class compiles is
 * over the rebuilt page on the next frame, the click after it is the rebuilt page's, and the tree the compile replaced --
 * taken down -- hears nothing more.
 */
namespace DreamLifecycleRecompilePointerTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FName PageName(TEXT("RecompilePointerPage"));

	/** Clicks counted on a page, by a trigger component put on it; shared so a lambda bound on a widget can count into it. */
	struct FClicks
	{
		int32 OnOldPage = 0;
		int32 OnNewPage = 0;
	};

	/** A trigger on InPage counting every click it hears into InCount. */
	void CountClicks(UDreamWidget* InPage, const TSharedRef<FClicks>& InClicks, bool bInOldPage)
	{
		UUIEventTrigger* Trigger = InPage != nullptr ? InPage->AddComponent<UUIEventTrigger>() : nullptr;
		if (Trigger != nullptr)
		{
			Trigger->GetOnPointerClickEvent().AddLambda([InClicks, bInOldPage](UDreamPointerEventData*)
			{
				++(bInOldPage ? InClicks->OnOldPage : InClicks->OnNewPage);
			});
		}
	}

	/** Whether InWidget is InAncestor or somewhere inside it. */
	bool IsWithin(const UDreamWidget* InWidget, const UDreamWidget* InAncestor)
	{
		for (const UDreamWidget* Walk = InWidget; Walk != nullptr; Walk = Walk->GetParent())
		{
			if (Walk == InAncestor)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRecompilePointerTest,
	"DreamGUI.Lifecycle.APageRebuiltByARecompileUnderTheRestingPointerIsOverItOnTheNextFrameAndTakesTheClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A page of a panel class on the player's screen, the pointer resting on it. The class compiles and the page is built
 * again, as the tick after a compile builds it. On the next frame the pointer is over the new page and nothing of the old
 * one is left in its state; the press and release that follow click the new page once, and the old page's trigger never
 * hears a click.
 */
bool FDreamLifecycleRecompilePointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleRecompilePointerTestLocal;
	DreamTests::Lifecycle::FScopedPanelClass Panel(TEXT("RecompilePointer"));
	UClass* PanelClass = Panel.GetClass();
	UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	if (!TestNotNull(TEXT("The panel class compiled"), PanelClass) || !TestNotNull(TEXT("The editor's DreamGUI subsystem exists"), EditorSubsystem))
	{
		return false;
	}
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	UDreamScreenUISubsystem* Screen = Rig.IsUsable() ? UDreamScreenUISubsystem::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig and its screen UI came up"), Screen))
	{
		return false;
	}
	Screen->ShowWidgetOfClass(PageName, PanelClass);
	Rig.PumpFrames(2);
	UDreamWidget* OldPage = Screen->GetUI(PageName);
	if (!TestNotNull(TEXT("A page of the class is on the player's screen"), OldPage))
	{
		return false;
	}
	const TSharedRef<FClicks> Clicks = MakeShared<FClicks>();
	CountClicks(OldPage, Clicks, /*bInOldPage*/ true);
	TestTrue(TEXT("Resting the pointer on the page completes"),
		Rig.Driver()->Sequence().MoveToPixel(FVector2D(ViewportSize.X * 0.5, ViewportSize.Y * 0.5)).WaitFrames(1).Perform());
	const UDreamPointerEventData* Mouse = Rig.Context().GetPointerEventData(0);
	if (!TestTrue(TEXT("...and it is over the page"), Mouse != nullptr && IsWithin(Mouse->EnterWidget.Get(), OldPage)))
	{
		return false;
	}
	const TWeakObjectPtr<UDreamWidget> OldPageWeak = OldPage;

	FKismetEditorUtilities::CompileBlueprint(Panel.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	TestFalse(TEXT("The compile is over"), EditorSubsystem->IsRecompiling());
	TestFalse(TEXT("The page went before the compile"), OldPageWeak.IsValid() && OldPageWeak->HasRegistered());
	// What the tick after the compile does.
	EditorSubsystem->RebuildReleasedTrees();
	UDreamWidget* NewPage = Screen->GetUI(PageName);
	if (!TestTrue(TEXT("The page is back, built from the new class"), NewPage != nullptr && NewPage != OldPageWeak.Get() && NewPage->GetClass() == PanelClass))
	{
		return false;
	}
	CountClicks(NewPage, Clicks, /*bInOldPage*/ false);

	Rig.PumpFrames(1);
	Mouse = Rig.Context().GetPointerEventData(0);
	TestTrue(TEXT("On the next frame the resting pointer is over the rebuilt page"), Mouse != nullptr && IsWithin(Mouse->EnterWidget.Get(), NewPage));
	TestFalse(TEXT("...and holds nothing of the page the compile replaced"),
		Mouse != nullptr && OldPageWeak.IsValid() && (IsWithin(Mouse->EnterWidget.Get(), OldPageWeak.Get()) || Mouse->HoverComponentArray.Contains(OldPageWeak.Get())));

	TestTrue(TEXT("Pressing and letting go where the pointer rests completes"), Rig.Driver()->Sequence().Press().Release().Perform());
	TestEqual(TEXT("The rebuilt page took the click, once"), Clicks->OnNewPage, 1);
	TestEqual(TEXT("...and the page it replaced heard none"), Clicks->OnOldPage, 0);
	Screen->RemoveUI(PageName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecyclePieRecompilePointerTest,
	"DreamGUI.Pie.APageRebuiltByARecompileWhileItPlaysTakesTheNextClickThroughThePlayerController",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The same in a play session: the page on the player's screen, the pointer resting on it, the class compiled and the page
 * built again in the play world; the next press and release through the player controller click the rebuilt page once, and
 * the old one hears nothing.
 */
bool FDreamLifecyclePieRecompilePointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleRecompilePointerTestLocal;
	const TSharedRef<DreamTests::Lifecycle::FScopedPanelClass> Panel = MakeShared<DreamTests::Lifecycle::FScopedPanelClass>(TEXT("PieRecompilePointer"));
	if (!TestNotNull(TEXT("The panel class compiled"), Panel->GetClass()))
	{
		return false;
	}
	const TSharedRef<FClicks> Clicks = MakeShared<FClicks>();
	const TSharedRef<TWeakObjectPtr<UDreamWidget>> Page = MakeShared<TWeakObjectPtr<UDreamWidget>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Panel, Clicks, Page](FDreamDriverPieRig& InRig)
	{
		if (UDreamScreenUISubsystem* Screen = InRig.GetWorld()->GetSubsystem<UDreamScreenUISubsystem>())
		{
			*Page = Screen->ShowWidgetOfClass(PageName, Panel->GetClass());
			CountClicks(Page->Get(), Clicks, /*bInOldPage*/ true);
		}
	});
	const FWaitTimeout Limit = FWaitTimeout::InSeconds(2.0);
	Rig->Sequence()
		.MoveTo(DreamPieTests::Live([Page]() { return Page->Get(); }, TEXT("the page on the player's screen")))
		.WaitFrames(1)
		.Then([this, Panel, Page, Clicks](FDreamDriverContext& InContext)
		{
			UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
			UDreamScreenUISubsystem* Screen = InContext.World != nullptr ? InContext.World->GetSubsystem<UDreamScreenUISubsystem>() : nullptr;
			if (!TestNotNull(TEXT("The editor's DreamGUI subsystem"), EditorSubsystem) || !TestNotNull(TEXT("The play world's screen"), Screen)
				|| !TestTrue(TEXT("The page is on the screen"), Page->IsValid()))
			{
				return;
			}
			const TWeakObjectPtr<UDreamWidget> OldPage = *Page;
			FKismetEditorUtilities::CompileBlueprint(Panel->Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			EditorSubsystem->RebuildReleasedTrees();
			UDreamWidget* NewPage = Screen->GetUI(PageName);
			TestTrue(TEXT("The page is back, built from the new class"), NewPage != nullptr && NewPage != OldPage.Get() && NewPage->GetClass() == Panel->GetClass());
			*Page = NewPage;
			CountClicks(NewPage, Clicks, /*bInOldPage*/ false);
		})
		.WaitFrames(1)
		.Press()
		.Release()
		.Wait(FDreamUntil::Condition([Clicks]() { return Clicks->OnNewPage > 0; }, Limit), FWaitTimeout::InSeconds(3.0),
			TEXT("the rebuilt page to take the click"))
		.Then([this, Clicks](FDreamDriverContext& InContext)
		{
			TestEqual(TEXT("The rebuilt page took the click, once"), Clicks->OnNewPage, 1);
			TestEqual(TEXT("...and the page it replaced heard none"), Clicks->OnOldPage, 0);
			if (UDreamScreenUISubsystem* Screen = InContext.World != nullptr ? InContext.World->GetSubsystem<UDreamScreenUISubsystem>() : nullptr)
			{
				Screen->RemoveUI(PageName);
			}
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
