#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/WidgetSerializer.h"

namespace LexPanelLayoutIntegrationTestLocal
{
	bool IsFiniteVector(const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutAnimationSnapshotDefaultsTest,
	"LGUI.Layout.AnimationSnapshotDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutAnimationSnapshotDefaultsTest::RunTest(const FString& Parameters)
{
	const FLayoutAnimationSnapshotData Snapshot;
	TestNull(TEXT("Default snapshot has no widget"), Snapshot.Widget);
	TestEqual(TEXT("Default snapshot position is deterministic"), Snapshot.Position, FVector2D::ZeroVector);
	TestEqual(TEXT("Default snapshot size is deterministic"), Snapshot.Size, FVector2D::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPanelAlgorithmsStayFiniteTest,
	"LGUI.Layout.Panel.AllAlgorithmsStayFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPanelAlgorithmsStayFiniteTest::RunTest(const FString& Parameters)
{
	const TArray<TSubclassOf<ULexPanelLayoutBase>> PanelClasses = {
		ULexLayoutContainerCanvasPanel::StaticClass(),
		ULexLayoutContainerOverlay::StaticClass(),
		ULexLayoutContainerHorizontalBox::StaticClass(),
		ULexLayoutContainerVerticalBox::StaticClass(),
		ULexLayoutContainerWrapBox::StaticClass(),
		ULexLayoutContainerGridPanel::StaticClass(),
		ULexLayoutContainerUniformGridPanel::StaticClass(),
		ULexLayoutContainerSizeBox::StaticClass(),
		ULexLayoutContainerScaleBox::StaticClass(),
		ULexLayoutContainerSafeZone::StaticClass(),
		ULexLayoutContainerScrollBox::StaticClass(),
		ULexLayoutContainerWidgetSwitcher::StaticClass(),
	};

	for (const TSubclassOf<ULexPanelLayoutBase>& PanelClass : PanelClasses)
	{
		const FString Context = PanelClass->GetDisplayNameText().ToString();
		ULexWidget* Root = NewObject<ULexWidget>();
		ULexWidget* Child = NewObject<ULexWidget>(Root);
		Root->SetWidth(640.0f);
		Root->SetHeight(360.0f);
		Child->SetWidth(160.0f);
		Child->SetHeight(90.0f);
		TestTrue(*FString::Printf(TEXT("%s accepts its seed child"), *Context), Child->TrySetParent(Root, false));

		ULexPanelLayoutBase* Layout = Cast<ULexPanelLayoutBase>(Root->CreateNewLayoutContainer(PanelClass));
		TestNotNull(*FString::Printf(TEXT("%s layout is created"), *Context), Layout);
		ULexPanelSlot* Slot = Child->GetPanelSlot();
		TestNotNull(*FString::Printf(TEXT("%s creates a child slot"), *Context), Slot);
		if (!Layout || !Slot)
		{
			continue;
		}
		Slot->SetPadding(FMargin(3.0f, 4.0f, 5.0f, 6.0f));
		Layout->SnapshotLayout();
		Layout->CalculateLayout();

		FLexLayoutDebugInfo DebugInfo;
		TestTrue(*FString::Printf(TEXT("%s exposes child diagnostics"), *Context),
			Layout->GetLayoutDebugInfo(Child, DebugInfo));
		TestFalse(*FString::Printf(TEXT("%s identifies its algorithm"), *Context), DebugInfo.Algorithm.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s desired size stays finite"), *Context),
			LexPanelLayoutIntegrationTestLocal::IsFiniteVector(DebugInfo.DesiredSize));
		TestTrue(*FString::Printf(TEXT("%s arranged position stays finite"), *Context),
			LexPanelLayoutIntegrationTestLocal::IsFiniteVector(DebugInfo.ArrangedPosition));
		TestTrue(*FString::Printf(TEXT("%s arranged size stays finite"), *Context),
			LexPanelLayoutIntegrationTestLocal::IsFiniteVector(DebugInfo.ArrangedSize));
		TestTrue(*FString::Printf(TEXT("%s arranged size stays non-negative"), *Context),
			DebugInfo.ArrangedSize.X >= 0.0 && DebugInfo.ArrangedSize.Y >= 0.0);
		Root->RemoveLayoutContainer();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPanelModeTransitionContractsTest,
	"LGUI.Layout.Panel.ModeTransitionContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPanelModeTransitionContractsTest::RunTest(const FString& Parameters)
{
	ULexWidget* Root = NewObject<ULexWidget>();
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	ULexWidget* RejectedChild = NewObject<ULexWidget>(Root);
	Root->SetWidth(800.0f);
	Root->SetHeight(450.0f);
	Child->SetWidth(240.0f);
	Child->SetHeight(120.0f);
	Child->SetAnchoredPosition(FVector2D(37.0, -19.0));
	TestTrue(TEXT("Child joins before a panel is assigned"), Child->TrySetParent(Root, false));

	ULexLayoutContainerHorizontalBox* Horizontal = Root->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
	ULexPanelSlot* Slot = Child->GetPanelSlot();
	TestNotNull(TEXT("Horizontal panel created"), Horizontal);
	TestNotNull(TEXT("Horizontal panel created a slot"), Slot);
	if (!Horizontal || !Slot)
	{
		return false;
	}
	const FVector2D AuthoredPosition = Child->GetAnchoredPosition();
	const FVector2D AuthoredSize = Child->GetSize();
	Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Left);
	Slot->SetVerticalAlignment(ELexPanelVerticalAlignment::Top);
	TestTrue(TEXT("An unregistered child joins an existing panel"), RejectedChild->TrySetParent(Root, false));
	ULexPanelSlot* RejectedSlot = RejectedChild->GetPanelSlot();
	TestNotNull(TEXT("Joining an existing panel creates a slot immediately"), RejectedSlot);
	TestNull(TEXT("An over-capacity panel replacement is rejected atomically"),
		Root->CreateNewLayoutContainer<ULexLayoutContainerSizeBox>());
	TestTrue(TEXT("Rejected replacement preserves the old layout"), Root->GetLayoutContainer() == Horizontal);
	TestEqual(TEXT("Rejected replacement preserves the child hierarchy"), Root->GetChildrenCount(), 2);
	TestTrue(TEXT("Rejected replacement preserves the first slot"), Child->GetPanelSlot() == Slot);
	TestTrue(TEXT("Rejected replacement preserves the second slot"), RejectedChild->GetPanelSlot() == RejectedSlot);

	ULexWidget* PlainParent = NewObject<ULexWidget>();
	TestTrue(TEXT("A panel child can move to a non-panel parent"), RejectedChild->TrySetParent(PlainParent, false));
	TestNull(TEXT("Moving to a non-panel parent removes the old slot immediately"), RejectedChild->GetPanelSlot());

	ULexLayoutContainerScaleBox* ScaleBox = Root->CreateNewLayoutContainer<ULexLayoutContainerScaleBox>();
	TestNotNull(TEXT("ScaleBox transition succeeds"), ScaleBox);
	TestTrue(TEXT("Panel-to-panel transition preserves the slot object"), Child->GetPanelSlot() == Slot);
	TestEqual(TEXT("First ScaleBox transition uses centered horizontal alignment"),
		Slot->HorizontalAlignment, ELexPanelHorizontalAlignment::Center);
	TestEqual(TEXT("First ScaleBox transition uses centered vertical alignment"),
		Slot->VerticalAlignment, ELexPanelVerticalAlignment::Center);
	TestFalse(TEXT("ScaleBox rejects a second child"), RejectedChild->TrySetParent(Root, false));

	Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Right);
	Slot->SetVerticalAlignment(ELexPanelVerticalAlignment::Bottom);
	TestNotNull(TEXT("Replacing ScaleBox with ScaleBox succeeds"),
		Root->CreateNewLayoutContainer<ULexLayoutContainerScaleBox>());
	TestEqual(TEXT("Same-mode replacement preserves horizontal slot edits"),
		Slot->HorizontalAlignment, ELexPanelHorizontalAlignment::Right);
	TestEqual(TEXT("Same-mode replacement preserves vertical slot edits"),
		Slot->VerticalAlignment, ELexPanelVerticalAlignment::Bottom);

	CastChecked<ULexPanelLayoutBase>(Root->GetLayoutContainer())->SnapshotLayout();
	Root->GetLayoutContainer()->CalculateLayout();
	TestNotNull(TEXT("Panel-to-legacy transition succeeds"),
		Root->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>());
	TestNull(TEXT("Panel-to-legacy transition removes the panel slot immediately"), Child->GetPanelSlot());
	TestEqual(TEXT("Panel-to-legacy transition restores the authored position"), Child->GetAnchoredPosition(), AuthoredPosition);
	TestEqual(TEXT("Panel-to-legacy transition restores the authored size"), Child->GetSize(), AuthoredSize);

	TestNotNull(TEXT("A panel can be created again after legacy layout"),
		Root->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>());
	TestNotNull(TEXT("Returning to a panel creates a fresh slot"), Child->GetPanelSlot());
	CastChecked<ULexPanelLayoutBase>(Root->GetLayoutContainer())->SnapshotLayout();
	Root->GetLayoutContainer()->CalculateLayout();
	Root->RemoveLayoutContainer();
	TestNull(TEXT("Removing a panel removes the child slot immediately"), Child->GetPanelSlot());
	TestEqual(TEXT("Removing a panel restores the authored position"), Child->GetAnchoredPosition(), AuthoredPosition);
	TestEqual(TEXT("Removing a panel restores the authored size"), Child->GetSize(), AuthoredSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPanelPrefabRoundTripTest,
	"LGUI.Prefab.PanelLayoutRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPanelPrefabRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSystem;

	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Test world created"), World);
	if (!World)
	{
		return false;
	}

	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	ULexWidget* Root = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
	ULexWidget* Child = NewObject<ULexWidget>(Root, NAME_None, RF_Public | RF_Transactional);
	TestTrue(TEXT("Child joins the prefab root"), Child->TrySetParent(Root, false));
	ULexLayoutContainerHorizontalBox* Horizontal = Root->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
	ULexPanelSlot* Slot = Child->GetPanelSlot();
	TestNotNull(TEXT("Horizontal layout created"), Horizontal);
	TestNotNull(TEXT("Panel slot created"), Slot);
	if (!Prefab || !Horizontal || !Slot)
	{
		World->DestroyWorld(false);
		return false;
	}
	Horizontal->SetSpacing(17.0f);
	Slot->SetSizeRule(ELexPanelSizeRule::Fill);
	Slot->SetFillWeight(3.25f);
	Slot->SetPadding(FMargin(2.0f, 4.0f, 6.0f, 8.0f));

	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Root, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Panel prefab serializes"),
		WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));

	TMap<FGuid, TObjectPtr<UObject>> ReloadedObjects;
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> ReloadedSubPrefabs;
	ULexWidget* ReloadedRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
		World, World, Prefab, nullptr, ReloadedObjects, ReloadedSubPrefabs);
	ULexWidget* ReloadedChild = IsValid(ReloadedRoot) && ReloadedRoot->GetChildrenCount() == 1
		? ReloadedRoot->GetChildByIndex(0) : nullptr;
	ULexLayoutContainerHorizontalBox* ReloadedHorizontal = IsValid(ReloadedRoot)
		? Cast<ULexLayoutContainerHorizontalBox>(ReloadedRoot->GetLayoutContainer()) : nullptr;
	ULexPanelSlot* ReloadedSlot = IsValid(ReloadedChild) ? ReloadedChild->GetPanelSlot() : nullptr;
	TestNotNull(TEXT("Horizontal layout survives reload"), ReloadedHorizontal);
	TestNotNull(TEXT("Panel slot survives reload"), ReloadedSlot);
	if (ReloadedHorizontal && ReloadedSlot)
	{
		TestEqual(TEXT("Panel spacing survives reload"), ReloadedHorizontal->Spacing, 17.0f);
		TestEqual(TEXT("Slot size rule survives reload"), ReloadedSlot->SizeRule, ELexPanelSizeRule::Fill);
		TestEqual(TEXT("Slot fill weight survives reload"), ReloadedSlot->FillWeight, 3.25f);
		TestEqual(TEXT("Slot padding survives reload"), ReloadedSlot->Padding, FMargin(2.0f, 4.0f, 6.0f, 8.0f));
	}

	World->DestroyWorld(false);
	return true;
}

#endif
