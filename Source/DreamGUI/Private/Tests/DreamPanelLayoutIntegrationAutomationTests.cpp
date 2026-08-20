// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/WidgetSerializer.h"

namespace DreamPanelLayoutIntegrationTestLocal
{
	bool IsFiniteVector(const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutAnimationSnapshotDefaultsTest,
	"DreamGUI.Layout.AnimationSnapshotDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutAnimationSnapshotDefaultsTest::RunTest(const FString& Parameters)
{
	const FLayoutAnimationSnapshotData Snapshot;
	TestNull(TEXT("Default snapshot has no widget"), Snapshot.Widget);
	TestEqual(TEXT("Default snapshot position is deterministic"), Snapshot.Position, FVector2D::ZeroVector);
	TestEqual(TEXT("Default snapshot size is deterministic"), Snapshot.Size, FVector2D::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelAlgorithmsStayFiniteTest,
	"DreamGUI.Layout.Panel.AllAlgorithmsStayFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelAlgorithmsStayFiniteTest::RunTest(const FString& Parameters)
{
	const TArray<TSubclassOf<UDreamPanelLayoutBase>> PanelClasses = {
		UDreamLayoutContainerCanvasPanel::StaticClass(),
		UDreamLayoutContainerOverlay::StaticClass(),
		UDreamLayoutContainerHorizontalBox::StaticClass(),
		UDreamLayoutContainerVerticalBox::StaticClass(),
		UDreamLayoutContainerWrapBox::StaticClass(),
		UDreamLayoutContainerGridPanel::StaticClass(),
		UDreamLayoutContainerUniformGridPanel::StaticClass(),
		UDreamLayoutContainerSizeBox::StaticClass(),
		UDreamLayoutContainerScaleBox::StaticClass(),
		UDreamLayoutContainerSafeZone::StaticClass(),
		UDreamLayoutContainerScrollBox::StaticClass(),
		UDreamLayoutContainerWidgetSwitcher::StaticClass(),
	};

	for (const TSubclassOf<UDreamPanelLayoutBase>& PanelClass : PanelClasses)
	{
		const FString Context = PanelClass->GetDisplayNameText().ToString();
		UDreamWidget* Root = NewObject<UDreamWidget>();
		UDreamWidget* Child = NewObject<UDreamWidget>(Root);
		Root->SetWidth(640.0f);
		Root->SetHeight(360.0f);
		Child->SetWidth(160.0f);
		Child->SetHeight(90.0f);
		TestTrue(*FString::Printf(TEXT("%s accepts its seed child"), *Context), Child->TrySetParent(Root, false));

		UDreamPanelLayoutBase* Layout = Cast<UDreamPanelLayoutBase>(Root->CreateNewLayoutContainer(PanelClass));
		TestNotNull(*FString::Printf(TEXT("%s layout is created"), *Context), Layout);
		UDreamPanelSlot* Slot = Child->GetPanelSlot();
		TestNotNull(*FString::Printf(TEXT("%s creates a child slot"), *Context), Slot);
		if (!Layout || !Slot)
		{
			continue;
		}
		Slot->SetPadding(FMargin(3.0f, 4.0f, 5.0f, 6.0f));
		Layout->SnapshotLayout();
		Layout->CalculateLayout();

		FDreamLayoutDebugInfo DebugInfo;
		TestTrue(*FString::Printf(TEXT("%s exposes child diagnostics"), *Context),
			Layout->GetLayoutDebugInfo(Child, DebugInfo));
		TestFalse(*FString::Printf(TEXT("%s identifies its algorithm"), *Context), DebugInfo.Algorithm.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s desired size stays finite"), *Context),
			DreamPanelLayoutIntegrationTestLocal::IsFiniteVector(DebugInfo.DesiredSize));
		TestTrue(*FString::Printf(TEXT("%s arranged position stays finite"), *Context),
			DreamPanelLayoutIntegrationTestLocal::IsFiniteVector(DebugInfo.ArrangedPosition));
		TestTrue(*FString::Printf(TEXT("%s arranged size stays finite"), *Context),
			DreamPanelLayoutIntegrationTestLocal::IsFiniteVector(DebugInfo.ArrangedSize));
		TestTrue(*FString::Printf(TEXT("%s arranged size stays non-negative"), *Context),
			DebugInfo.ArrangedSize.X >= 0.0 && DebugInfo.ArrangedSize.Y >= 0.0);
		Root->RemoveLayoutContainer();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelModeTransitionContractsTest,
	"DreamGUI.Layout.Panel.ModeTransitionContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelModeTransitionContractsTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Root = NewObject<UDreamWidget>();
	UDreamWidget* Child = NewObject<UDreamWidget>(Root);
	UDreamWidget* RejectedChild = NewObject<UDreamWidget>(Root);
	Root->SetWidth(800.0f);
	Root->SetHeight(450.0f);
	Child->SetWidth(240.0f);
	Child->SetHeight(120.0f);
	Child->SetAnchoredPosition(FVector2D(37.0, -19.0));
	TestTrue(TEXT("Child joins before a panel is assigned"), Child->TrySetParent(Root, false));

	UDreamLayoutContainerHorizontalBox* Horizontal = Root->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>();
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	TestNotNull(TEXT("Horizontal panel created"), Horizontal);
	TestNotNull(TEXT("Horizontal panel created a slot"), Slot);
	if (!Horizontal || !Slot)
	{
		return false;
	}
	const FVector2D AuthoredPosition = Child->GetAnchoredPosition();
	const FVector2D AuthoredSize = Child->GetSize();
	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
	Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Top);
	TestTrue(TEXT("An unregistered child joins an existing panel"), RejectedChild->TrySetParent(Root, false));
	UDreamPanelSlot* RejectedSlot = RejectedChild->GetPanelSlot();
	TestNotNull(TEXT("Joining an existing panel creates a slot immediately"), RejectedSlot);
	TestNull(TEXT("An over-capacity panel replacement is rejected atomically"),
		Root->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>());
	TestTrue(TEXT("Rejected replacement preserves the old layout"), Root->GetLayoutContainer() == Horizontal);
	TestEqual(TEXT("Rejected replacement preserves the child hierarchy"), Root->GetChildrenCount(), 2);
	TestTrue(TEXT("Rejected replacement preserves the first slot"), Child->GetPanelSlot() == Slot);
	TestTrue(TEXT("Rejected replacement preserves the second slot"), RejectedChild->GetPanelSlot() == RejectedSlot);

	UDreamWidget* PlainParent = NewObject<UDreamWidget>();
	TestTrue(TEXT("A panel child can move to a non-panel parent"), RejectedChild->TrySetParent(PlainParent, false));
	TestNull(TEXT("Moving to a non-panel parent removes the old slot immediately"), RejectedChild->GetPanelSlot());

	UDreamLayoutContainerScaleBox* ScaleBox = Root->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	TestNotNull(TEXT("ScaleBox transition succeeds"), ScaleBox);
	TestTrue(TEXT("Panel-to-panel transition preserves the slot object"), Child->GetPanelSlot() == Slot);
	TestEqual(TEXT("First ScaleBox transition uses centered horizontal alignment"),
		Slot->HorizontalAlignment, EDreamPanelHorizontalAlignment::Center);
	TestEqual(TEXT("First ScaleBox transition uses centered vertical alignment"),
		Slot->VerticalAlignment, EDreamPanelVerticalAlignment::Center);
	TestFalse(TEXT("ScaleBox rejects a second child"), RejectedChild->TrySetParent(Root, false));

	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
	Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Bottom);
	TestNotNull(TEXT("Replacing ScaleBox with ScaleBox succeeds"),
		Root->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>());
	TestEqual(TEXT("Same-mode replacement preserves horizontal slot edits"),
		Slot->HorizontalAlignment, EDreamPanelHorizontalAlignment::Right);
	TestEqual(TEXT("Same-mode replacement preserves vertical slot edits"),
		Slot->VerticalAlignment, EDreamPanelVerticalAlignment::Bottom);

	CastChecked<UDreamPanelLayoutBase>(Root->GetLayoutContainer())->SnapshotLayout();
	Root->GetLayoutContainer()->CalculateLayout();
	// With the legacy family gone, the only container transition left is panel-to-panel. Swapping to a
	// different panel class must still leave the child holding a slot; the authored-geometry restore
	// contract is exercised by RemoveLayoutContainer below, which is now its only trigger.
	TestNotNull(TEXT("Cross-panel transition succeeds"),
		Root->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>());
	TestNotNull(TEXT("Cross-panel transition leaves the child with a slot"), Child->GetPanelSlot());
	CastChecked<UDreamPanelLayoutBase>(Root->GetLayoutContainer())->SnapshotLayout();
	Root->GetLayoutContainer()->CalculateLayout();
	Root->RemoveLayoutContainer();
	TestNull(TEXT("Removing a panel removes the child slot immediately"), Child->GetPanelSlot());
	TestEqual(TEXT("Removing a panel restores the authored position"), Child->GetAnchoredPosition(), AuthoredPosition);
	TestEqual(TEXT("Removing a panel restores the authored size"), Child->GetSize(), AuthoredSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisibleNestedPanelRebuildTest,
	"DreamGUI.Layout.Panel.VisibleNestedTreeRebuilds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisibleNestedPanelRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamWidget* Root = World ? NewObject<UDreamWidget>(World) : nullptr;
	UDreamWidget* NestedPanel = Root ? NewObject<UDreamWidget>(Root) : nullptr;
	UDreamWidget* NestedChild = NestedPanel ? NewObject<UDreamWidget>(NestedPanel) : nullptr;
	if (!World || !Root || !NestedPanel || !NestedChild)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}

	Root->SetWidth(320.0f);
	Root->SetHeight(180.0f);
	NestedPanel->SetWidth(80.0f);
	NestedPanel->SetHeight(60.0f);
	NestedChild->SetWidth(16.0f);
	NestedChild->SetHeight(12.0f);
	TestTrue(TEXT("Nested panel joins the root"), NestedPanel->TrySetParent(Root, false));
	TestTrue(TEXT("Nested child joins its panel"), NestedChild->TrySetParent(NestedPanel, false));
	TestNotNull(TEXT("Root overlay is created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>());
	TestNotNull(TEXT("Nested overlay is created"), NestedPanel->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>());

	Root->OnRegister();
	NestedPanel->OnRegister();
	NestedChild->OnRegister();
	NestedPanel->SetVisibility(EDreamWidgetVisibility::Collapsed);
	UDreamWidget::MarkLayoutForRebuild(Root);
	UDreamWidget::RebuildLayoutImmediately(Root);

	NestedPanel->SetVisibility(EDreamWidgetVisibility::Visible);
	UDreamWidget::RebuildLayoutImmediately(Root);
	TestEqual(TEXT("Newly visible nested panel fills the root immediately"), NestedPanel->GetSize(), Root->GetSize());
	TestEqual(TEXT("Newly visible nested child is included in the rebuilt layout tree"),
		NestedChild->GetSize(), NestedPanel->GetSize());

	Root->DestroyWidget();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelPrefabRoundTripTest,
	"DreamGUI.Prefab.PanelLayoutRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelPrefabRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPrefabSystem;

	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Test world created"), World);
	if (!World)
	{
		return false;
	}

	UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* Child = NewObject<UDreamWidget>(Root, NAME_None, RF_Public | RF_Transactional);
	TestTrue(TEXT("Child joins the prefab root"), Child->TrySetParent(Root, false));
	UDreamLayoutContainerHorizontalBox* Horizontal = Root->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>();
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	TestNotNull(TEXT("Horizontal layout created"), Horizontal);
	TestNotNull(TEXT("Panel slot created"), Slot);
	if (!Prefab || !Horizontal || !Slot)
	{
		World->DestroyWorld(false);
		return false;
	}
	Horizontal->SetSpacing(17.0f);
	Slot->SetSizeRule(EDreamPanelSizeRule::Fill);
	Slot->SetFillWeight(3.25f);
	Slot->SetPadding(FMargin(2.0f, 4.0f, 6.0f, 8.0f));

	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Root, FGuid::NewGuid());
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Panel prefab serializes"),
		WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));

	TMap<FGuid, TObjectPtr<UObject>> ReloadedObjects;
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> ReloadedSubPrefabs;
	UDreamWidget* ReloadedRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
		World, World, Prefab, nullptr, ReloadedObjects, ReloadedSubPrefabs);
	UDreamWidget* ReloadedChild = IsValid(ReloadedRoot) && ReloadedRoot->GetChildrenCount() == 1
		? ReloadedRoot->GetChildByIndex(0) : nullptr;
	UDreamLayoutContainerHorizontalBox* ReloadedHorizontal = IsValid(ReloadedRoot)
		? Cast<UDreamLayoutContainerHorizontalBox>(ReloadedRoot->GetLayoutContainer()) : nullptr;
	UDreamPanelSlot* ReloadedSlot = IsValid(ReloadedChild) ? ReloadedChild->GetPanelSlot() : nullptr;
	TestNotNull(TEXT("Horizontal layout survives reload"), ReloadedHorizontal);
	TestNotNull(TEXT("Panel slot survives reload"), ReloadedSlot);
	if (ReloadedHorizontal && ReloadedSlot)
	{
		TestEqual(TEXT("Panel spacing survives reload"), ReloadedHorizontal->Spacing, 17.0f);
		TestEqual(TEXT("Slot size rule survives reload"), ReloadedSlot->SizeRule, EDreamPanelSizeRule::Fill);
		TestEqual(TEXT("Slot fill weight survives reload"), ReloadedSlot->FillWeight, 3.25f);
		TestEqual(TEXT("Slot padding survives reload"), ReloadedSlot->Padding, FMargin(2.0f, 4.0f, 6.0f, 8.0f));
	}

	World->DestroyWorld(false);
	return true;
}

#endif
