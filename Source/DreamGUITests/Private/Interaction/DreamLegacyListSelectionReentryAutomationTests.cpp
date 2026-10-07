// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamLegacyListSelectionReentryTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamLegacyListSelectionReentryTestLocal
{
	enum class ECallback : uint8 { ClearItems, ReplaceItems, ReplaceKeepingRequested, RemoveRequested, ClearSelection, SelectOther, SelectRequested, DeselectRequested };

	UDreamLegacyListSelectionReentryList* MakeList(FAutomationTestBase& InTest, FDreamDriverRig& InRig, EUIListSelectionMode InMode)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the selection rig came up"), InRig.IsUsable())
			|| !InTest.TestTrue(TEXT("the selection UI began play"), DreamDragInteraction::BeginPlayForUI(InRig.GetWorld())))return nullptr;
		UDreamWidget* Host = InRig.MakeWidget(TEXT("SelectionListHost"), nullptr, FVector2D(300.0, 300.0));
		UDreamWidget* Content = Host != nullptr ? InRig.MakeWidget(TEXT("Content"), Host, FVector2D(300.0, 300.0)) : nullptr;
		UDreamWidget* Cell = Content != nullptr ? InRig.MakeWidget(TEXT("Cell"), Content, FVector2D(300.0, 100.0)) : nullptr;
		if (!InTest.TestNotNull(TEXT("the selection cell template exists"), Cell))return nullptr;
		Cell->AddComponent<UUIListEntry>();
		UDreamLegacyListSelectionReentryList* List = Host->AddComponent<UDreamLegacyListSelectionReentryList>();
		if (!InTest.TestNotNull(TEXT("the selection list exists"), List))return nullptr;
		List->SetMode(InMode);
		List->SetHorizontal(false);
		List->SetVertical(true);
		List->SetContent(Content);
		List->SetCellTemplate(Cell);
		InRig.PumpFrames(2);
		return List;
	}

	bool TestSelection(FAutomationTestBase& InTest, const UUIListView& InList, UObject* InExpectedSelected)
	{
		const TArray<UObject*> Items = InList.GetListItems();
		const TArray<UObject*> Selected = InList.GetSelectedItems();
		bool bValid = InTest.TestEqual(TEXT("the callback's selection count is final"), Selected.Num(), InExpectedSelected != nullptr ? 1 : 0);
		if (InExpectedSelected != nullptr)bValid = InTest.TestTrue(TEXT("the callback's selected item stays selected"), InList.IsItemSelected(InExpectedSelected)) && bValid;
		for (UObject* Item : Selected)
		{
			bValid = InTest.TestTrue(TEXT("every selected item still belongs to the list"), IsValid(Item) && Items.Contains(Item)) && bValid;
		}
		for (const FUIRecyclableScrollViewCellContainer& Cell : InList.GetCacheCellList())
		{
			const UUIListEntry* Entry = Cast<UUIListEntry>(Cell.CellComponent);
			if (!InTest.TestTrue(TEXT("each visible selection row remains live"), IsValid(Cell.Widget) && IsValid(Entry)))
			{
				bValid = false;
				continue;
			}
			bValid = InTest.TestEqual(TEXT("each visible row reflects the final selection"), Entry->IsSelected(), InList.IsItemSelected(Entry->GetItem())) && bValid;
		}
		return bValid;
	}

	bool RunSwitch(FAutomationTestBase& InTest, ECallback InCallback, EUIListSelectionMode InMode)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
		UDreamLegacyListSelectionReentryList* List = MakeList(InTest, Rig, InMode);
		if (List == nullptr)return false;
		const TArray<UObject*> Items = DreamListsInteraction::MakeItems(4);
		const TArray<UObject*> Replacement = DreamListsInteraction::MakeItems(2);
		UObject* Requested = Items[0];
		UObject* Previous = Items[1];
		UObject* Other = Items[2];
		List->SetListItems(Items);
		List->SetItemSelection(Previous, true);
		TStrongObjectPtr<UDreamLegacyListSelectionReentryProbe> Probe(NewObject<UDreamLegacyListSelectionReentryProbe>());
		List->BindProbe(Probe.Get());
		Probe->Action = [&InTest, List, InCallback, Requested, Previous, Other, Replacement](UObject* Item, bool bSelected)
		{
			InTest.TestEqual(TEXT("the callback starts when the old item is deselected"), Item, Previous);
			InTest.TestFalse(TEXT("the callback is a deselection"), bSelected);
			switch (InCallback)
			{
			case ECallback::ClearItems: List->ClearListItems(); break;
			case ECallback::ReplaceItems: List->SetListItems(Replacement); break;
			case ECallback::ReplaceKeepingRequested: List->SetListItems({Requested, Other}); break;
			case ECallback::RemoveRequested: List->RemoveItem(Requested); break;
			case ECallback::ClearSelection: List->ClearSelection(); break;
			case ECallback::SelectOther: List->SetItemSelection(Other, true); break;
			case ECallback::SelectRequested: List->SetItemSelection(Requested, true); break;
			case ECallback::DeselectRequested: List->SetItemSelection(Requested, false); break;
			}
		};
		List->SetItemSelection(Requested, true);
		Rig.PumpFrames(1);
		InTest.TestEqual(TEXT("the selection callback mutated the list once"), Probe->ActionCount, 1);
		UObject* Expected = InCallback == ECallback::SelectOther ? Other : (InCallback == ECallback::SelectRequested ? Requested : nullptr);
		bool bValid = TestSelection(InTest, *List, Expected);
		InTest.TestEqual(TEXT("the interrupted outer selection emits no extra notification"), Probe->EventItems.Num(), Expected != nullptr ? 2 : 1);
		if (InCallback == ECallback::ClearItems)
		{
			InTest.TestEqual(TEXT("the callback leaves the data source empty"), List->GetListItems().Num(), 0);
			InTest.TestEqual(TEXT("the callback leaves no rows"), List->GetCacheCellList().Num(), 0);
		}
		else if (InCallback == ECallback::ReplaceItems || InCallback == ECallback::ReplaceKeepingRequested)
		{
			InTest.TestEqual(TEXT("the callback's replacement data remains installed"), List->GetListItems().Num(), 2);
		}
		return bValid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLegacySelectionSourceMutationTest,
	"DreamGUI.ListView.SelectionCallbacksCannotRestoreAnItemAfterClearingOrReplacingTheSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamLegacySelectionSourceMutationTest, "DreamGUI.ListView.SelectionCallbacksCannotRestoreAnItemAfterClearingOrReplacingTheSource", "[Pointer][Animated]")

bool FDreamLegacySelectionSourceMutationTest::RunTest(const FString& Parameters)
{
	using namespace DreamLegacyListSelectionReentryTestLocal;
	bool bValid = true;
	for (EUIListSelectionMode Mode : {EUIListSelectionMode::Single, EUIListSelectionMode::Multi})
	{
		for (ECallback Callback : {ECallback::ClearItems, ECallback::ReplaceItems, ECallback::ReplaceKeepingRequested, ECallback::RemoveRequested})
		{
			bValid = RunSwitch(*this, Callback, Mode) && bValid;
		}
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLegacyNestedSelectionTest,
	"DreamGUI.ListView.NestedSelectionRequestsSupersedeTheSelectionThatCalledThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamLegacyNestedSelectionTest, "DreamGUI.ListView.NestedSelectionRequestsSupersedeTheSelectionThatCalledThem", "[Pointer][Animated]")

bool FDreamLegacyNestedSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamLegacyListSelectionReentryTestLocal;
	bool bValid = true;
	for (EUIListSelectionMode Mode : {EUIListSelectionMode::Single, EUIListSelectionMode::Multi})
	{
		for (ECallback Callback : {ECallback::ClearSelection, ECallback::SelectOther, ECallback::SelectRequested, ECallback::DeselectRequested})
		{
			bValid = RunSwitch(*this, Callback, Mode) && bValid;
		}
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLegacyClearSelectionReentryTest,
	"DreamGUI.ListView.AClearSelectionCallbackCanReselectAnOldItemWithoutAStaleDeselectEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamLegacyClearSelectionReentryTest, "DreamGUI.ListView.AClearSelectionCallbackCanReselectAnOldItemWithoutAStaleDeselectEvent", "[Pointer][Animated]")

bool FDreamLegacyClearSelectionReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamLegacyListSelectionReentryTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	UDreamLegacyListSelectionReentryList* List = MakeList(*this, Rig, EUIListSelectionMode::Multi);
	if (List == nullptr)return false;
	const TArray<UObject*> Items = DreamListsInteraction::MakeItems(3);
	List->SetListItems(Items);
	List->SetItemSelection(Items[0], true, false);
	List->SetItemSelection(Items[1], true, false);
	TStrongObjectPtr<UDreamLegacyListSelectionReentryProbe> Probe(NewObject<UDreamLegacyListSelectionReentryProbe>());
	List->BindProbe(Probe.Get());
	UObject* Expected = nullptr;
	Probe->Action = [this, List, Items, &Expected](UObject* Item, bool bSelected)
	{
		TestFalse(TEXT("ClearSelection first notifies a deselection"), bSelected);
		TestTrue(TEXT("the deselected item came from the old selection"), Item == Items[0] || Item == Items[1]);
		Expected = Item == Items[0] ? Items[1] : Items[0];
		List->SetItemSelection(Expected, true, false);
	};
	List->ClearSelection();
	Rig.PumpFrames(1);
	TestEqual(TEXT("the callback makes one nested choice"), Probe->ActionCount, 1);
	TestEqual(TEXT("the outer clear stops before falsely deselecting the chosen item"), Probe->EventItems.Num(), 2);
	if (Probe->EventItems.Num() == 2)
	{
		TestEqual(TEXT("the final event names the nested choice"), Probe->EventItems.Last().Get(), Expected);
		TestTrue(TEXT("the final event selects that item"), Probe->EventStates.Last());
	}
	return TestSelection(*this, *List, Expected);
}

#endif
