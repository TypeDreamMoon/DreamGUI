// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

/*
 * What goes down with a destroyed widget.
 *
 * A destroyed widget answers IsValid false at once; so must everything it owned. A weak pointer to its
 * canvas, its visual or a material its canvas made is held by delegates, tweens and the canvas's own
 * bookkeeping, and each of them asks "is it still there" -- of an object that was torn down and is only
 * waiting for the collector.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleDestroyTakesPartsDownTest,
	"DreamGUI.Lifecycle.DestroyingAPanelTakesItsCanvasAndWhatTheCanvasMadeDownWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleDestroyTakesPartsDownTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;
	// What the canvas drew with is to be its material instances, which it makes with its proxies switched off.
	const FScopedMaterialWrappers MaterialInstances(0);

	FScopedPanelClass Panel(TEXT("LifecycleTeardown"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWidget* Root = Actor->GetWidgetComponent()->GetLoadedWidget();
	UDreamCanvas* Canvas = Actor->GetWidgetComponent()->GetLoadedCanvas();
	if (!TestNotNull(TEXT("with its tree"), Root) || !TestNotNull(TEXT("and a canvas"), Canvas))return false;

	UDreamVisual* Visual = nullptr;
	TArray<UDreamWidget*> Widgets;
	UDreamWidget::CollectChildrenWidgets(Root, Widgets, true);
	for (UDreamWidget* Widget : Widgets)
	{
		if (Widget->GetVisual() != nullptr)
		{
			Visual = Widget->GetVisual();
			break;
		}
	}
	UMaterialInstanceDynamic* Material = FindMaterialReadingADynamicTexture(Canvas->GetUIMesh());
	UDreamUIDataAsTexture* PropertyData = Canvas->GetWidgetPropertyDataAsTexture();
	if (!TestNotNull(TEXT("a widget of it draws"), Visual)
		|| !TestNotNull(TEXT("through a material the canvas made"), Material)
		|| !TestNotNull(TEXT("with the canvas's widget property data"), PropertyData))
	{
		return false;
	}

	const TWeakObjectPtr<UDreamCanvas> WeakCanvas(Canvas);
	const TWeakObjectPtr<UDreamVisual> WeakVisual(Visual);
	const TWeakObjectPtr<UMaterialInstanceDynamic> WeakMaterial(Material);
	const TWeakObjectPtr<UDreamUIDataAsTexture> WeakPropertyData(PropertyData);

	Root->DestroyWidget();
	TestFalse(TEXT("the destroyed root is gone"), IsValid(Root));
	TestFalse(TEXT("and so is its canvas, before any collection"), WeakCanvas.IsValid());
	TestFalse(TEXT("and the visual of a widget under it"), WeakVisual.IsValid());
	TestFalse(TEXT("and a material the canvas made"), WeakMaterial.IsValid());
	TestFalse(TEXT("and the canvas's widget property data"), WeakPropertyData.IsValid());
	return true;
}

/*
 * A level that is gone stays gone.
 *
 * A torn-down world waits in memory for the collector with its components unregistered, and it still
 * passes IsValid: UWorld::DestroyWorld does not mark it garbage. A compile of a widget class ends with
 * the trees built from it being built again, so that they show the recompiled class -- and the reload
 * that used to do this took in the panels of levels already torn down. Each got a new tree,
 * registered in a world with no manager left in it, which nothing would ever tear down; the collector
 * reported it inside whatever test or edit collected next.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleCompileAfterTeardownTest,
	"DreamGUI.Lifecycle.ABlueprintCompiledAfterALevelIsTornDownBuildsNoTreeInThatLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleCompileAfterTeardownTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecycleCompileAfterTeardown"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	TWeakObjectPtr<UWorld> TornDown;
	{
		FScopedWorld Level(EWorldType::Editor);
		if (!TestNotNull(TEXT("the panel was placed"), PlacePanel(Level.World, Panel.GetClass())))return false;
		TornDown = Level.World;
	}
	if (!TestTrue(TEXT("the torn-down level is still in memory, waiting for the collector"), TornDown.IsValid()))return false;

	FKismetEditorUtilities::CompileBlueprint(Panel.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	TArray<UDreamWidget*> Built;
	for (TObjectIterator<UDreamWidget> It; It; ++It)
	{
		if (IsValid(*It) && It->HasRegistered() && It->GetTypedOuter<UWorld>() == TornDown.Get())
		{
			Built.Add(*It);
		}
	}
	TestEqual(TEXT("no widget is registered in the torn-down level after the compile"), Built.Num(), 0);
	// Whatever a failure built is taken down here, not left for a later test's collection to report.
	for (UDreamWidget* Widget : Built)
	{
		if (IsValid(Widget) && Widget->GetParent() == nullptr)
		{
			Widget->DestroyWidget();
		}
	}
	return true;
}

#endif
