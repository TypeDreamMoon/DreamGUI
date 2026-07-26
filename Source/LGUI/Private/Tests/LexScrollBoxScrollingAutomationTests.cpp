// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"

/*
 * Scroll box sizing semantics: the scroll-axis desired size must exclude content. Reporting the
 * content extent let any Auto-measuring ancestor inflate the viewport to fit everything — which is
 * unscrollable by construction, and made the designer and PIE disagree wherever the surrounding
 * space differed (the sidebar band ballooning to 994px was exactly this).
 */

namespace LexScrollBoxScrollingTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexScrollBoxDesiredSizeExcludesContentTest,
	"LGUI.Layout.ScrollBox.DesiredSizeExcludesContentOnScrollAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxDesiredSizeExcludesContentTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Outer = MakeWidget(TestWorld.World, nullptr, TEXT("Outer"), 300.0f, 400.0f);
	ULexWidget* ScrollWidget = MakeWidget(TestWorld.World, Outer, TEXT("Scroll"), 200.0f, 120.0f);
	ULexPanelLayoutBase* OuterPanel = Outer->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>();
	TestNotNull(TEXT("Outer panel created"), OuterPanel);
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f);
	}
	Outer->OnRegister();
	ScrollWidget->OnRegister();
	ULexWidget::MarkLayoutForRebuild(Outer);
	ULexWidget::RebuildLayoutImmediately(Outer);

	// 3 x 100 of content must NOT leak into the scroll-axis desired size. With the panel reporting
	// only padding there, measurement falls back to the widget's AUTHORED height — the designer's
	// viewport size — so an Auto parent grants exactly what was authored instead of inflating the
	// viewport to fit all content. The cross axis measures content like a normal stack.
	const FVector2D Desired = OuterPanel->GetDesiredSize(ScrollWidget);
	TestEqual(TEXT("Scroll-axis desired size is the authored viewport, not content"), Desired.Y, 120.0);
	TestEqual(TEXT("Cross-axis desired size measures content"), Desired.X, 180.0);

	Outer->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexScrollBoxScrollRangeTest,
	"LGUI.Layout.ScrollBox.ScrollRangeAndClamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxScrollRangeTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
	FScopedGameWorld TestWorld;
	// Viewport authored at 120 tall, content 3 x 100: scrollable range must be exactly 180.
	ULexWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Scroll"), 200.0f, 120.0f);
	ULexLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>();
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f);
	}
	ScrollWidget->OnRegister();
	ULexWidget::MarkLayoutForRebuild(ScrollWidget);
	ULexWidget::RebuildLayoutImmediately(ScrollWidget);

	TestEqual(TEXT("Max scroll = content minus viewport"), ScrollBox->GetMaxScrollOffset(), 180.0f);
	ScrollBox->SetScrollOffset(10000.0f);
	TestEqual(TEXT("Offset clamps to max"), ScrollBox->GetScrollOffset(), 180.0f);
	TestTrue(TEXT("ScrollBy moves within range"), ScrollBox->ScrollBy(-140.0f));
	TestEqual(TEXT("ScrollBy lands exactly"), ScrollBox->GetScrollOffset(), 40.0f);
	ScrollBox->ScrollBy(-10000.0f);
	TestEqual(TEXT("Offset clamps to zero"), ScrollBox->GetScrollOffset(), 0.0f);
	TestFalse(TEXT("ScrollBy at the limit reports no movement"), ScrollBox->ScrollBy(-1.0f));

	ScrollWidget->DestroyWidget();
	return true;
}

#endif
