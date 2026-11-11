// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUIWidgetPicking.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Extensions/DreamRing.h"
#include "Extensions/Effects/DreamPixelSort.h"

#include "UObject/Package.h"

/*
 * Which of the widgets under the cursor a click is allowed to select.
 *
 * A .dui root often ends with a full-screen effect -- a pixel sort stretched over everything -- which is the front-most
 * rect of the whole page, so every click in the canvas selected it. A drop already looks through a layer that holds
 * nothing; a click now looks through one that nothing can hit and nothing shows. These hold the rule on widgets alone:
 * which ones count, not where the ray crosses them.
 */

namespace DreamDesignerClickPickingTestLocal
{
	/** A worldless authoring tree with one root widget of the given visual class, which is the state the designer builds in. */
	struct FLayerFixture
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Widget = nullptr;

		explicit FLayerFixture(UClass* InVisualClass)
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(TEXT("Layer")));
			if (Widget != nullptr)
			{
				Tree->RootWidget = Widget;
				Widget->SetWidth(200.0f);
				Widget->SetHeight(100.0f);
				if (InVisualClass != nullptr)
				{
					Widget->CreateNewVisual(InVisualClass);
				}
			}
		}

		~FLayerFixture()
		{
			if (Widget != nullptr)
			{
				Widget->DestroyWidget();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerClickLooksThroughASilentEffectTest,
	"DreamGUI.Designer.AClickLooksThroughAnEffectNothingCanHitAndNothingShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerClickLooksThroughASilentEffectTest::RunTest(const FString&)
{
	using namespace DreamDesignerClickPickingTestLocal;

	FLayerFixture Effect(UDreamPixelSort::StaticClass());
	UDreamPixelSort* Sort = Effect.Widget != nullptr ? Cast<UDreamPixelSort>(Effect.Widget->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("The effect was made"), Sort))
	{
		return false;
	}

	TestTrue(TEXT("An effect that can be hit is picked, as it always was"), DreamUIWidgetPicking::IsWorthPicking(Effect.Widget));

	Sort->SetRaycastTarget(false);
	TestTrue(TEXT("...and so is one that only draws"), DreamUIWidgetPicking::IsWorthPicking(Effect.Widget));

	Sort->SetSortStrength(0.0f);
	TestFalse(TEXT("One that cannot be hit and has strength zero is looked through"), DreamUIWidgetPicking::IsWorthPicking(Effect.Widget));

	Sort->SetRaycastTarget(true);
	TestTrue(TEXT("...unless it is a raycast target, which the run time would let a ray hit"),
		DreamUIWidgetPicking::IsWorthPicking(Effect.Widget));

	Sort->SetRaycastTarget(false);
	Sort->SetSortStrength(0.5f);
	TestTrue(TEXT("Strength back, it is picked again"), DreamUIWidgetPicking::IsWorthPicking(Effect.Widget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerClickKeepsGroupsAndShapesTest,
	"DreamGUI.Designer.AClickStillReachesPanelsGroupsAndWhatDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerClickKeepsGroupsAndShapesTest::RunTest(const FString&)
{
	using namespace DreamDesignerClickPickingTestLocal;

	{
		FLayerFixture Panel(nullptr);
		if (TestNotNull(TEXT("A widget with no visual was made"), Panel.Widget))
		{
			TestTrue(TEXT("A widget with no visual is how a layout is selected, and is always picked"),
				DreamUIWidgetPicking::IsWorthPicking(Panel.Widget));
		}
	}
	{
		FLayerFixture Shape(UDreamRing::StaticClass());
		UDreamVisual* Visual = Shape.Widget != nullptr ? Shape.Widget->GetVisual() : nullptr;
		if (TestNotNull(TEXT("A shape was made"), Visual))
		{
			Visual->SetRaycastTarget(false);
			TestTrue(TEXT("A shape that is not a raycast target but draws is picked"), DreamUIWidgetPicking::IsWorthPicking(Shape.Widget));
		}
	}
	{
		FLayerFixture Group(UDreamPixelSort::StaticClass());
		UDreamPixelSort* Sort = Group.Widget != nullptr ? Cast<UDreamPixelSort>(Group.Widget->GetVisual()) : nullptr;
		if (TestNotNull(TEXT("A silent effect was made"), Sort))
		{
			Sort->SetRaycastTarget(false);
			Sort->SetSortStrength(0.0f);
			TestFalse(TEXT("A silent effect on its own is looked through"), DreamUIWidgetPicking::IsWorthPicking(Group.Widget));

			UDreamWidget* Child = Group.Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(TEXT("Child")));
			if (TestNotNull(TEXT("A child was made"), Child))
			{
				// The authoring tree's own way to attach, which needs no world.
				Child->SetParentBeforeRegister(Group.Widget);
				TestTrue(TEXT("Holding children makes it a group the author must still reach"),
					DreamUIWidgetPicking::IsWorthPicking(Group.Widget));
			}
		}
	}
	return true;
}

#endif
