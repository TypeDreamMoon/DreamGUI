// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"
#include "Interaction/UIStandardControls.h"

/*
 * The legacy FlexBox measured a child through `Cast<ULexLayoutSelfFlexBox>(child->GetLayoutSelf())` and,
 * when that cast failed, fell back to the child's current arranged size with a comment reading
 * "child does not have LayoutSelf".
 *
 * The comment was wrong: the cast is to one specific LayoutSelf class, so every OTHER kind landed in the
 * fallback and had its declared size discarded - ULexLayoutSelfSpacer, whose entire reason to exist is
 * GetLayoutPreferredSize, along with AspectRatio and Grid. A child that is itself a container was
 * discarded too, so adding rows to a VerticalBox sitting inside a FlexBox row never re-flowed that row;
 * content overflowed or left a hole, and the only workaround was knowing to attach a second, redundant
 * ULexLayoutSelfFlexBox.
 *
 * The asymmetry was purely one-directional: ULexPanelLayoutBase::GetDesiredSize has always consulted a
 * child's LayoutSelf, then its LayoutContainer, then its Visual. FlexBox now asks the same three, in the
 * same order.
 */

namespace LexFlexBoxChildMeasurementTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Horizontal FlexBox root; caller adds children before calling Run. */
	struct FFlexRowFixture
	{
		ULexWidget* Root = nullptr;
		ULexLayoutContainerFlexBox* Flex = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<ULexWidget>(World);
			Root->SetWidth(600.0f);
			Root->SetHeight(200.0f);
			return true;
		}

		ULexWidget* AddChild(float W, float H)
		{
			ULexWidget* Child = NewObject<ULexWidget>(Root);
			Child->SetWidth(W);
			Child->SetHeight(H);
			return Child->TrySetParent(Root, false) ? Child : nullptr;
		}

		bool Finish()
		{
			Flex = Root->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>();
			if (!Flex)
			{
				return false;
			}
			Root->OnRegister();
			for (ULexWidget* Child : Root->GetChildren())
			{
				Child->OnRegister();
			}
			return true;
		}

		void Run(ULexUIManagerWorldSubsystem* Manager)
		{
			ULexWidget::MarkLayoutForRebuild(Root);
			Manager->TickLexUI(0.016f);
			Manager->TickLexUI(0.016f);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFlexBoxMeasuresNestedContainerTest,
	"LGUI.Layout.FlexBoxChildMeasurement.NestedContainerIsMeasured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxMeasuresNestedContainerTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxChildMeasurementTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FFlexRowFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	// A nested FlexBox column whose own content is far wider than the stale widget size it starts with.
	// The outer row has to measure it through its container, not through that stale size.
	ULexWidget* Nested = Fixture.AddChild(8.0f, 8.0f);
	ULexWidget* After = Fixture.AddChild(40.0f, 40.0f);
	if (!TestNotNull(TEXT("Nested child"), Nested) || !TestNotNull(TEXT("Trailing child"), After))
	{
		return false;
	}
	ULexWidget* Inner = NewObject<ULexWidget>(Nested);
	Inner->SetWidth(180.0f);
	Inner->SetHeight(30.0f);
	if (!TestTrue(TEXT("Inner parented"), Inner->TrySetParent(Nested, false)))
	{
		return false;
	}
	ULexLayoutSelfFlexBox* InnerSelf = Inner->CreateNewLayoutSelf<ULexLayoutSelfFlexBox>();
	ULexLayoutContainerFlexBox* NestedFlex = Nested->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>();
	if (!TestNotNull(TEXT("Inner LayoutSelf"), InnerSelf) || !TestNotNull(TEXT("Nested FlexBox"), NestedFlex))
	{
		return false;
	}
	InnerSelf->SetPreferredWidth(FLexLayoutSize(ELexLayoutSizeType::Fixed, 180.0f));
	InnerSelf->SetPreferredHeight(FLexLayoutSize(ELexLayoutSizeType::Fixed, 30.0f));
	NestedFlex->SetDirection(ELexLayoutFlexBoxDirectionType::Vertical);
	if (!Fixture.Finish())
	{
		return false;
	}
	Inner->OnRegister();
	Fixture.Run(Manager);

	// Centre-to-centre is (nested + trailing) / 2. Measured through the nested container that is
	// (180 + 40) / 2 = 110; measured through the nested widget's stale 8 it would be (8 + 40) / 2 = 24.
	const double Gap = After->GetAnchoredPosition().X - Nested->GetAnchoredPosition().X;
	TestTrue(TEXT("The outer row reserves space for the nested container's own content"),
		FMath::IsNearlyEqual(Gap, (180.0 + 40.0) * 0.5, 1.0));

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFlexBoxPlainChildStillUsesArrangedSizeTest,
	"LGUI.Layout.FlexBoxChildMeasurement.PlainChildStillUsesArrangedSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxPlainChildStillUsesArrangedSizeTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxChildMeasurementTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FFlexRowFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	// The half that must not regress: a child with nothing to declare still measures at its own size.
	ULexWidget* Plain = Fixture.AddChild(70.0f, 40.0f);
	ULexWidget* After = Fixture.AddChild(40.0f, 40.0f);
	if (!TestNotNull(TEXT("Plain child"), Plain) || !TestNotNull(TEXT("Trailing child"), After)
		|| !Fixture.Finish())
	{
		return false;
	}
	Fixture.Run(Manager);

	// Children are pivot-centred, so neighbouring centres sit (wA + wB) / 2 apart.
	const double Gap = After->GetAnchoredPosition().X - Plain->GetAnchoredPosition().X;
	TestTrue(TEXT("A plain child still occupies its arranged width"),
		FMath::IsNearlyEqual(Gap, (70.0 + 40.0) * 0.5, 0.5));

	Fixture.Root->DestroyWidget();
	return true;
}

#endif
