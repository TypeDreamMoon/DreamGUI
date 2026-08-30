// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUITooltip.h"

/*
 * The tooltip's pure half: which widget on the hover path owns the tooltip, and where the bubble
 * goes. Both are free functions on purpose (the DreamPointerPolicy convention) so they are testable
 * without a world, an event system, or a screen root.
 */

namespace DreamUITooltipTestLocal
{
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

#endif
