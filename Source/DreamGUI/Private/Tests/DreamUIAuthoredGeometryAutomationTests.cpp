// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

/*
 * "Arranged geometry must never masquerade as authored data", from both sides.
 *
 * Measurement: GetDesiredSize may only see authored values. Feeding a panel-arranged rect back into
 * measurement closes the loop where a squeezed widget measures as squeezed forever.
 *
 * Authoring: a user's anchor edit IS the authored value, and the next canvas pass must not overwrite
 * it with what the panel arranged.
 *
 * A third test lived here, asserting the same rule through a prefab save and re-save. The asset
 * model it round-tripped through is gone; what it was really pinning -- that arrangement is
 * re-derived rather than stored -- these two assert directly.
 */

namespace DreamUIAuthoredGeometryTestLocal
{
	/** Game world with Root(320x180, VerticalBox) -> Child(authored 120x80, slot Fill), laid out once. */
	struct FArrangedFixture
	{
		UWorld* World = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamWidget* Child = nullptr;
		UDreamLayoutContainerVerticalBox* Panel = nullptr;

		bool BuildAndArrange()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return false;
			}
			Root = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Child = NewObject<UDreamWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("ArrangedRoot"));
			Child->SetDisplayName(TEXT("ArrangedChild"));
			Root->SetWidth(320.0f);
			Root->SetHeight(180.0f);
			Child->SetWidth(120.0f);
			Child->SetHeight(80.0f);
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			Panel = Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
			UDreamPanelSlot* Slot = Child->GetPanelSlot();
			if (!Panel || !Slot)
			{
				return false;
			}
			Slot->SetSizeRule(EDreamPanelSizeRule::Fill);
			Root->OnRegister();
			Child->OnRegister();
			UDreamWidget::MarkLayoutForRebuild(Root);
			UDreamWidget::RebuildLayoutImmediately(Root);
			return true;
		}

		~FArrangedFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeasureIgnoresArrangedValuesTest,
	"DreamGUI.Layout.Measure.DesiredSizeIgnoresArrangedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeasureIgnoresArrangedValuesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIAuthoredGeometryTestLocal;
	FArrangedFixture Fixture;
	if (!Fixture.BuildAndArrange())
	{
		AddError(TEXT("Fixture failed to build"));
		return false;
	}

	// Precondition: the panel really stomped the child (Fill stretches it to the panel rect).
	TestEqual(TEXT("Fill child is arranged to the panel size"), Fixture.Child->GetSize(), Fixture.Root->GetSize());
	TestTrue(TEXT("Slot records that layout wrote the rect"), Fixture.Child->GetPanelSlot()->HasLayoutGeometryApplied());

	// Measurement must report the authored 120x80, never the arranged 320x180.
	TestEqual(TEXT("Desired size is the authored rect, not the arranged rect"),
		Fixture.Panel->GetDesiredSize(Fixture.Child), FVector2D(120.0, 80.0));

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelSlotUserAnchorEditTest,
	"DreamGUI.Layout.Canvas.UserAnchorEditPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelSlotUserAnchorEditTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Parent = NewObject<UDreamWidget>();
	UDreamWidget* Child = NewObject<UDreamWidget>(Parent);
	UDreamLayoutContainerCanvasPanel* Canvas = Parent->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TestNotNull(TEXT("Canvas layout created"), Canvas);
	TestTrue(TEXT("Child joins the canvas"), Child->TrySetParent(Parent, false));
	UDreamPanelSlot* Slot = Child->CreateNewPanelSlot<UDreamPanelSlot>();
	TestNotNull(TEXT("Canvas slot created"), Slot);
	if (!Parent || !Child || !Canvas || !Slot)
	{
		return false;
	}

	FDreamUIAnchorData Centered;
	Centered.AnchorMin = FVector2D(0.5, 0.5);
	Centered.AnchorMax = FVector2D(0.5, 0.5);
	Centered.SizeDelta = FVector2D(1920.0, 1080.0);
	Child->SetAnchorData(Centered);
	Slot->CaptureAuthoredGeometry(true);
	Slot->MarkLayoutGeometryApplied();

	FDreamUIAnchorData Stretched = Centered;
	Stretched.AnchorMin = FVector2D::ZeroVector;
	Stretched.AnchorMax = FVector2D::UnitVector;
	Stretched.SizeDelta = FVector2D::ZeroVector;
	Child->SetAnchorData(Stretched);
	Slot->SyncAuthoredGeometryAfterUserEdit();
	TestFalse(TEXT("Canvas without AutoSize releases stale layout geometry"), Slot->HasLayoutGeometryApplied());
	Canvas->SnapshotLayout();
	Canvas->CalculateLayout();
	if (Slot->HasLayoutGeometryApplied())
	{
		Slot->RestoreAuthoredGeometry();
	}
	else
	{
		Slot->CaptureAuthoredGeometry(true);
	}
	TestEqual(TEXT("Stretch anchor minimum survives the next canvas pass"), Child->GetAnchorMin(), FVector2D::ZeroVector);
	TestEqual(TEXT("Stretch anchor maximum survives the next canvas pass"), Child->GetAnchorMax(), FVector2D::UnitVector);

	Child->SetAnchorData(Centered);
	Slot->CaptureAuthoredGeometry(true);
	Slot->SetAutoSize(true);
	Slot->MarkLayoutGeometryApplied(false, false, true, true);
	Child->SetAnchorData(Stretched);
	Slot->SyncAuthoredGeometryAfterUserEdit();
	TestTrue(TEXT("Canvas AutoSize retains size ownership"), Slot->HasLayoutGeometryApplied());
	Canvas->SnapshotLayout();
	Canvas->CalculateLayout();
	Slot->SetAutoSize(false);
	Slot->RestoreAuthoredGeometry();
	TestEqual(TEXT("AutoSize restore keeps the user-authored anchor minimum"), Child->GetAnchorMin(), FVector2D::ZeroVector);
	TestEqual(TEXT("AutoSize restore keeps the user-authored anchor maximum"), Child->GetAnchorMax(), FVector2D::UnitVector);
	TestEqual(TEXT("AutoSize restore returns the authored width"), Child->GetSizeDelta().X, 1920.0);
	TestEqual(TEXT("AutoSize restore returns the authored height"), Child->GetSizeDelta().Y, 1080.0);
	return true;
}

#endif
