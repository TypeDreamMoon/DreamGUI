// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Materials/MaterialInstanceDynamic.h"

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

#endif
