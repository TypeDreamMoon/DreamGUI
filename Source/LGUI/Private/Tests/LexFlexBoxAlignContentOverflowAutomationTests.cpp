// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"

/*
 * The whole align-content switch sat inside `if (SecondarySurplusSpace > 0)`, so as soon as the wrapped
 * lines were taller than the container every mode silently collapsed to Start - the cross offset was left
 * at bare padding. The primary axis has always handled the mirror case explicitly, subtracting the deficit
 * for Center and End, so the two axes simply disagreed.
 *
 * Center and End are perfectly well defined when the content overflows: the block moves the other way.
 * The distributing modes are the ones with nothing to distribute below zero, and CSS stretch only grows,
 * so those stay gated.
 *
 * Symptom this produced: "it was centred until I added one more row."
 */

namespace LexFlexBoxAlignContentOverflowTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/**
	 * Horizontal wrapping FlexBox, deliberately too short on the cross axis for the rows it will produce.
	 * Each child is 120 wide inside a 260-wide container, so children wrap 2 per row.
	 */
	struct FOverflowingWrapFixture
	{
		ULexWidget* Root = nullptr;
		TArray<ULexWidget*> Kids;
		ULexLayoutContainerFlexBox* Flex = nullptr;

		bool Build(UWorld* World, int32 NumChildren, float RootHeight)
		{
			Root = NewObject<ULexWidget>(World);
			Root->SetWidth(260.0f);
			Root->SetHeight(RootHeight);
			for (int32 I = 0; I < NumChildren; ++I)
			{
				ULexWidget* Kid = NewObject<ULexWidget>(Root);
				Kid->SetWidth(120.0f);
				Kid->SetHeight(60.0f);
				if (!Kid->TrySetParent(Root, false))
				{
					return false;
				}
				ULexLayoutSelfFlexBox* Self = Kid->CreateNewLayoutSelf<ULexLayoutSelfFlexBox>();
				if (!Self)
				{
					return false;
				}
				Self->SetPreferredWidth(FLexLayoutSize(ELexLayoutSizeType::Fixed, 120.0f));
				Self->SetPreferredHeight(FLexLayoutSize(ELexLayoutSizeType::Fixed, 60.0f));
				Kids.Add(Kid);
			}
			Flex = Root->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>();
			if (!Flex)
			{
				return false;
			}
			Flex->SetWrap(ELexLayoutFlexBoxWrapType::Wrap);
			Root->OnRegister();
			for (ULexWidget* Kid : Kids)
			{
				Kid->OnRegister();
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
	FLexFlexBoxAlignContentCenterSurvivesOverflowTest,
	"LGUI.Layout.FlexBoxAlignContent.CenterSurvivesOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxAlignContentCenterSurvivesOverflowTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxAlignContentOverflowTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// 6 children at 120 wide wrap 2-per-row inside 260 => 3 rows of 60 = 180 > 100, so the cross axis
	// overflows. Centring must push the block up, not fall back to top-aligned.
	FOverflowingWrapFixture Overflowing;
	if (!Overflowing.Build(TestWorld.World, 6, 100.0f))
	{
		return false;
	}
	Overflowing.Flex->SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment::Center);
	Overflowing.Run(Manager);
	const double CenteredFirstRowY = Overflowing.Kids[0]->GetAnchoredPosition().Y;

	// Same fixture, Start. If Center had degraded to Start these would be identical.
	FOverflowingWrapFixture Started;
	if (!Started.Build(TestWorld.World, 6, 100.0f))
	{
		return false;
	}
	Started.Flex->SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment::Start);
	Started.Run(Manager);
	const double StartedFirstRowY = Started.Kids[0]->GetAnchoredPosition().Y;

	TestTrue(TEXT("Centring still differs from Start when the cross axis overflows"),
		!FMath::IsNearlyEqual(CenteredFirstRowY, StartedFirstRowY, 0.01));
	// Overflowing content centred means the first row starts ABOVE the container's top edge.
	TestTrue(TEXT("The overflowing block is centred, so the first row hangs off the top"),
		CenteredFirstRowY > StartedFirstRowY);

	Overflowing.Root->DestroyWidget();
	Started.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFlexBoxAlignContentCenterUnchangedWithSurplusTest,
	"LGUI.Layout.FlexBoxAlignContent.CenterUnchangedWithSurplus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxAlignContentCenterUnchangedWithSurplusTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxAlignContentOverflowTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// The half that must not regress: with room to spare, Center still centres downward from the top and
	// still differs from Start in the other direction.
	FOverflowingWrapFixture Roomy;
	if (!Roomy.Build(TestWorld.World, 4, 400.0f))
	{
		return false;
	}
	Roomy.Flex->SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment::Center);
	Roomy.Run(Manager);
	const double CenteredY = Roomy.Kids[0]->GetAnchoredPosition().Y;

	FOverflowingWrapFixture RoomyStart;
	if (!RoomyStart.Build(TestWorld.World, 4, 400.0f))
	{
		return false;
	}
	RoomyStart.Flex->SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment::Start);
	RoomyStart.Run(Manager);
	const double StartY = RoomyStart.Kids[0]->GetAnchoredPosition().Y;

	TestTrue(TEXT("With surplus, Center is below Start"), CenteredY < StartY);

	Roomy.Root->DestroyWidget();
	RoomyStart.Root->DestroyWidget();
	return true;
}

#endif
