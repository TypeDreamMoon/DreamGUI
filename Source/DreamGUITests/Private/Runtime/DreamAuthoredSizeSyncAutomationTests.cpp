// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

/*
 * A panel measures an Auto child from UDreamPanelSlot's authored snapshot rather than from the child's
 * current extent. That is deliberate and correct: the current extent is layout OUTPUT, and feeding it back
 * into measurement is what makes a squeezed widget measure as squeezed forever.
 *
 * The snapshot just never followed a real edit. CaptureAuthoredGeometry returns immediately once
 * bHasAuthoredGeometry is set unless forced, and every forced re-capture lives on an editor, prefab or
 * reparent path - nothing on the runtime path. So the snapshot froze at whatever the child measured when
 * its slot was first registered, and the next pass measured from that stale value and wrote the old size
 * straight back: a runtime SetWidth/SetHeight visibly flashed and snapped back, and
 * UDreamSpriteBase::SetSprite - which sizes the widget from the new art - left the row at the old art's size.
 *
 * A size change is now told apart by where it came from: written from inside a layout pass it is output
 * and is ignored, written from anywhere else it is the new authored intent and the snapshot follows.
 */

namespace DreamAuthoredSizeSyncTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Vertical stack box: children are Fill across, and measured (Auto) down. */
	struct FVerticalStackFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* First = nullptr;
		UDreamWidget* Second = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World);
			First = NewObject<UDreamWidget>(Root);
			Second = NewObject<UDreamWidget>(Root);
			Root->SetWidth(300.0f);
			Root->SetHeight(400.0f);
			First->SetWidth(60.0f);
			First->SetHeight(30.0f);
			Second->SetWidth(60.0f);
			Second->SetHeight(30.0f);
			if (!First->TrySetParent(Root, false) || !Second->TrySetParent(Root, false))
			{
				return false;
			}
			UDreamLayoutContainerStackBox* Stack =
				Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
			if (!Stack)
			{
				return false;
			}
			Root->OnRegister();
			First->OnRegister();
			Second->OnRegister();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAuthoredSizeSurvivesRuntimeResizeTest,
	"DreamGUI.Layout.AuthoredSize.RuntimeResizeSurvivesTheNextPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredSizeSurvivesRuntimeResizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuthoredSizeSyncTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FVerticalStackFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Fixture.Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	const double SecondTopBefore = Fixture.Second->GetAnchoredPosition().Y;

	// A gameplay-side resize on the measured axis. This is the exact shape of UDreamSpriteBase::SetSprite
	// swapping in art of a different height.
	Fixture.First->SetHeight(120.0f);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("The runtime height survives the layout passes that follow it"),
		FMath::IsNearlyEqual(Fixture.First->GetHeight(), 120.0f, 0.01f));
	// And the row genuinely reflowed: the sibling below has to move down by the extra height.
	TestTrue(TEXT("The following sibling reflowed to the new height"),
		Fixture.Second->GetAnchoredPosition().Y < SecondTopBefore - 1.0);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAuthoredSizeIgnoresLayoutOutputTest,
	"DreamGUI.Layout.AuthoredSize.LayoutOutputDoesNotBecomeAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredSizeIgnoresLayoutOutputTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuthoredSizeSyncTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FVerticalStackFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Fixture.Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	UDreamPanelSlot* Slot = Fixture.First->GetPanelSlot();
	if (!TestNotNull(TEXT("The child has a panel slot"), Slot))
	{
		return false;
	}
	const FVector2f AuthoredAfterSettle = Slot->GetAuthoredDesiredSizeFallback();

	// The half that must NOT regress. The stack writes this child's width every pass (Fill across), and
	// that write must never be mistaken for authored intent - otherwise layout output feeds back into
	// measurement, which is the loop the snapshot exists to break.
	for (int32 I = 0; I < 5; ++I)
	{
		Manager->TickDreamUI(0.016f);
	}
	TestEqual(TEXT("Repeated layout passes do not rewrite the authored snapshot"),
		Slot->GetAuthoredDesiredSizeFallback(), AuthoredAfterSettle);

	// Squeezing the panel must not shrink the authored height either: the child keeps measuring at its
	// own size, so widening the panel again restores the original arrangement.
	const double HeightBefore = Fixture.First->GetHeight();
	Fixture.Root->SetWidth(40.0f);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	Fixture.Root->SetWidth(300.0f);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("The measured axis is unchanged by a round trip through a narrow panel"),
		FMath::IsNearlyEqual(Fixture.First->GetHeight(), HeightBefore, 0.01f));

	Fixture.Root->DestroyWidget();
	return true;
}

#endif
