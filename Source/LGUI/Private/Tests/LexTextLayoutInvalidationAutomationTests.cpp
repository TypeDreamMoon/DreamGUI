// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexText.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"

/*
 * Every ULexText content setter used to wrap its MarkLayoutForRebuild in
 * `if (GetWidget()->GetLayoutSelf())`, and SetFont did not mark at all.
 *
 * That premise was true when ULexLayoutSelfFlexBox was the only thing that read
 * ULexVisual::GetPreferredWidth/Height. The panel layouts added a second reader -
 * ULexPanelLayoutBase::GetDesiredSize measures *any* child that has a Visual, with no LayoutSelf
 * anywhere in the picture - and from that point the gate silently dropped every text reflow. A plain
 * text child in a StackBox has no LayoutSelf (CreateNewLayoutSelf has two callers and the control
 * registry only sets LayoutSelfClass for Spacer), so the gate was closed in exactly the common case.
 *
 * It only bit at runtime: PostEditChangeProperty marks unconditionally, so changing the text in the
 * details panel always reflowed and the hole was invisible to whoever was authoring the UI.
 *
 * These tests assert the invalidation contract - that the setter dirties the parent layout - rather
 * than the measured extent. Font metrics headless under -nullrhi are not something to build an
 * assertion on, and the defect was never in the measurement: it was that the measurement was never
 * asked for.
 */

namespace LexTextLayoutInvalidationTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Root(counting overlay) -> Child(ULexText, deliberately no LayoutSelf). */
	struct FTextInPanelFixture
	{
		ULexWidget* Root = nullptr;
		ULexWidget* Child = nullptr;
		ULexText* Text = nullptr;
		ULexLayoutPassCountingOverlay* Overlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<ULexWidget>(World);
			Child = NewObject<ULexWidget>(Root);
			Root->SetWidth(320.0f);
			Root->SetHeight(180.0f);
			Child->SetWidth(40.0f);
			Child->SetHeight(20.0f);
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			Overlay = Cast<ULexLayoutPassCountingOverlay>(
				Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
			Text = Cast<ULexText>(Child->CreateNewVisual(ULexText::StaticClass()));
			if (!Overlay || !Text)
			{
				return false;
			}
			Root->OnRegister();
			Child->OnRegister();
			return true;
		}

		/** Drain the cold start, then zero the counter so each test measures one edit. */
		void Settle(ULexUIManagerWorldSubsystem* Manager)
		{
			ULexWidget::MarkLayoutForRebuild(Root);
			Manager->TickLexUI(0.016f);
			Manager->TickLexUI(0.016f);
			Overlay->PassCount = 0;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexTextSetTextReflowsPanelWithoutLayoutSelfTest,
	"LGUI.Layout.TextInvalidation.SetTextReflowsPanelWithoutLayoutSelf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexTextSetTextReflowsPanelWithoutLayoutSelfTest::RunTest(const FString& Parameters)
{
	using namespace LexTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	TestNull(TEXT("The text child deliberately has no LayoutSelf"), Fixture.Child->GetLayoutSelf());

	Fixture.Settle(Manager);
	TestEqual(TEXT("Settled before the edit"), Fixture.Overlay->PassCount, 0);

	Fixture.Text->SetText(FText::FromString(TEXT("a much longer run of text than before")));
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("SetText reflows the parent panel"), Fixture.Overlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexTextMetricSettersReflowPanelTest,
	"LGUI.Layout.TextInvalidation.MetricSettersReflowPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexTextMetricSettersReflowPanelTest::RunTest(const FString& Parameters)
{
	using namespace LexTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Text->SetText(FText::FromString(TEXT("measured text")));

	// Each of these changes the extent the panel would measure, so each has to dirty the panel.
	struct FCase { const TCHAR* Name; TFunction<void(ULexText*)> Apply; };
	const TArray<FCase> Cases = {
		{ TEXT("SetFontSize"),   [](ULexText* T) { T->SetFontSize(T->GetFontSize() + 13.0f); } },
		{ TEXT("SetFontSpace"),  [](ULexText* T) { T->SetFontSpace(T->GetFontSpace() + FVector2D(3.0, 2.0)); } },
		{ TEXT("SetUseKerning"), [](ULexText* T) { T->SetUseKerning(!T->GetUseKerning()); } },
	};

	for (const FCase& Case : Cases)
	{
		Fixture.Settle(Manager);
		Case.Apply(Fixture.Text);
		Manager->TickLexUI(0.016f);
		TestEqual(*FString::Printf(TEXT("%s reflows the parent panel"), Case.Name),
			Fixture.Overlay->PassCount, 1);
	}

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexTextSetFontReflowsPanelTest,
	"LGUI.Layout.TextInvalidation.SetFontReflowsPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexTextSetFontReflowsPanelTest::RunTest(const FString& Parameters)
{
	using namespace LexTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Text->SetText(FText::FromString(TEXT("measured text")));

	// SetFont was the worst of the family: it never marked layout at all, on any path, so swapping the
	// font left every glyph re-rasterised and every layout stale.
	ULexUIFontData_BaseObject* OriginalFont = Fixture.Text->GetFont();
	Fixture.Settle(Manager);
	Fixture.Text->SetFont(nullptr);
	Manager->TickLexUI(0.016f);
	TestNotEqual(TEXT("The font actually changed"), (void*)Fixture.Text->GetFont(), (void*)OriginalFont);
	TestEqual(TEXT("SetFont reflows the parent panel"), Fixture.Overlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

#endif
