// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A RECT BLOCK'S CORNERRADIUS RUNS BOTTOM-RIGHT, TOP-RIGHT, TOP-LEFT, BOTTOM-LEFT.
 *
 * Not clockwise from the top left, which is what an image brush's RoundedBox takes and what the brush's comment
 * once said the rect block takes too. The order is the one UDreamWidget::ClippingCornerRadius uses, and the shader,
 * the corner-aware hit test and the clip underlay all read it so. Projects have been drawn with it, so it stays; this
 * pins it, through the hit test, which takes no hit in a corner its radius cuts off.
 */
namespace DreamRectBlockCornerOrderTestLocal
{
	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, FVector2D InSize)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Widget->SetWidth(static_cast<float>(InSize.X));
		Widget->SetHeight(static_cast<float>(InSize.Y));
		Widget->OnRegister();
		if (InParent != nullptr)
		{
			Widget->TrySetParent(InParent, false);
			Widget->SetAnchoredPosition(FVector2D::ZeroVector);
		}
		return Widget;
	}

	/** A ray through the block's plane at (Right, Up) from its centre, as a pointer over it would cast. */
	bool Hits(const UDreamRectBlock* InBlock, FVector2D InPoint)
	{
		const UDreamWidget* Widget = InBlock->GetWidget();
		const FVector Point = Widget->GetWorldTransform().TransformPosition(FVector(0.0, InPoint.X, InPoint.Y));
		const FVector Normal = Widget->GetWorldTransform().TransformVectorNoScale(FVector(1.0, 0.0, 0.0));
		FDreamUIHitResult Hit;
		return InBlock->LineTraceUI(Hit, Point + Normal * 100.0, Point - Normal * 100.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockCornerOrderTest,
	"DreamGUI.RectBlock.CornerRadiusRunsFromTheBottomRightCounterClockwise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockCornerOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockCornerOrderTestLocal;
	DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
	UDreamWidget* Root = MakeWidget(World.World, nullptr, FVector2D(400.0, 400.0));
	UDreamCanvas* Canvas = Root != nullptr ? Root->AddComponent<UDreamCanvas>() : nullptr;
	if (!TestTrue(TEXT("The stage is set up"), Manager != nullptr && Canvas != nullptr))
	{
		return false;
	}
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Widget = MakeWidget(World.World, Root, FVector2D(100.0, 100.0));
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	if (!TestNotNull(TEXT("A rect block"), Block))
	{
		return false;
	}
	Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
	Block->SetRaycastTarget(true);
	Manager->Tick(1.0f / 30.0f);

	// Three pixels in from each corner of a 100 x 100 block: inside the square, outside a 40 px rounding.
	struct FCorner { const TCHAR* Name; FVector2D Point; };
	const FCorner Corners[] = {
		{ TEXT("bottom-right"), FVector2D(47.0, -47.0) },
		{ TEXT("top-right"), FVector2D(47.0, 47.0) },
		{ TEXT("top-left"), FVector2D(-47.0, 47.0) },
		{ TEXT("bottom-left"), FVector2D(-47.0, -47.0) },
	};
	for (int32 Component = 0; Component < 4; ++Component)
	{
		FVector4 Radius(0.0, 0.0, 0.0, 0.0);
		Radius[Component] = 40.0;
		Block->SetCornerRadius(Radius);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const bool bRounded = Corner == Component;
			TestEqual(FString::Printf(TEXT("CornerRadius[%d] = 40 %s the %s corner"), Component, bRounded ? TEXT("rounds") : TEXT("leaves square"), Corners[Corner].Name),
				Hits(Block, Corners[Corner].Point), !bRounded);
		}
	}
	TestTrue(TEXT("...and the middle is always on it"), Hits(Block, FVector2D::ZeroVector));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
