// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"
#include "DreamTreeViewProviderReentryTestTypes.h"
#include "Controls/DreamTreeView.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamTreeProviderReentryTestLocal
{
	bool TestSource(FAutomationTestBase& InTest, UDreamTreeView& InTree, const TArray<UObject*>& InItems,
		const TArray<int32>& InDepths)
	{
		bool bValid = InTest.TestEqual(TEXT("the completed walk owns the final item count"), InTree.GetItemCount(), InItems.Num());
		bValid = InTest.TestEqual(TEXT("the completed walk owns the final depths"), InTree.ItemDepths.Num(), InDepths.Num()) && bValid;
		bValid = InTest.TestEqual(TEXT("every final item has one row"), InTree.GetRowCount(), InItems.Num()) && bValid;
		for (int32 Index = 0; Index < InItems.Num(); ++Index)
		{
			bValid = InTest.TestTrue(FString::Printf(TEXT("final item %d keeps its identity"), Index),
				InTree.GetItemAt(Index) == InItems[Index]) && bValid;
			if (InTree.ItemDepths.IsValidIndex(Index))
			{
				bValid = InTest.TestEqual(FString::Printf(TEXT("final item %d keeps its depth"), Index),
					InTree.ItemDepths[Index], InDepths[Index]) && bValid;
			}
			bValid = InTest.TestNotNull(FString::Printf(TEXT("final item %d is realized"), Index), InTree.GetRowWidget(Index)) && bValid;
		}
		return bValid;
	}

	bool RunCase(FAutomationTestBase& InTest, bool bInInterfaceProvider, bool bInDeepCallback, bool bInReplaceRoots)
	{
		TDreamTestControl<UDreamTreeView> Tree(NewObject<UDreamTreeView>(GetTransientPackage()));
		Tree->StyleSource = EDreamUIStyleSource::Inline;
		Tree->SetWidth(320.0f);
		Tree->SetHeight(200.0f);
		Tree->Initialize();

		TStrongObjectPtr<UDreamTreeProviderReentryProbe> Probe(NewObject<UDreamTreeProviderReentryProbe>());
		TStrongObjectPtr<UDreamTreeProviderReentryNode> Root(NewObject<UDreamTreeProviderReentryNode>());
		TStrongObjectPtr<UDreamTreeProviderReentryNode> Child(NewObject<UDreamTreeProviderReentryNode>());
		TStrongObjectPtr<UDreamTreeProviderReentryNode> GrandChild(NewObject<UDreamTreeProviderReentryNode>());
		TStrongObjectPtr<UDreamTreeProviderReentryNode> NewRoot(NewObject<UDreamTreeProviderReentryNode>());
		TStrongObjectPtr<UDreamTreeProviderReentryNode> NewChild(NewObject<UDreamTreeProviderReentryNode>());
		Root->Children.Add(Child.Get());
		Child->Children.Add(GrandChild.Get());
		NewRoot->Children.Add(NewChild.Get());
		for (UDreamTreeProviderReentryNode* Node : {Root.Get(), Child.Get(), GrandChild.Get(), NewRoot.Get(), NewChild.Get()})
		{
			Node->Probe = Probe.Get();
		}
		if (!bInInterfaceProvider)
		{
			Tree->OnGetItemChildren.BindDynamic(Probe.Get(), &UDreamTreeProviderReentryProbe::ProvideChildren);
		}
		Probe->TriggerItem = bInDeepCallback ? Child.Get() : Root.Get();
		const TArray<UObject*> ExpectedItems = bInReplaceRoots
			? TArray<UObject*>{NewRoot.Get(), NewChild.Get()}
			: bInDeepCallback ? TArray<UObject*>{Root.Get(), Child.Get(), NewChild.Get()}
			: TArray<UObject*>{Root.Get(), NewChild.Get()};
		const TArray<int32> ExpectedDepths = !bInReplaceRoots && bInDeepCallback
			? TArray<int32>{0, 1, 2} : TArray<int32>{0, 1};
		bool bNestedSourceValid = false;
		Probe->Action = [&InTest, &Tree, &Root, &Child, &NewRoot, &NewChild, &ExpectedItems, &ExpectedDepths,
			bInDeepCallback, bInReplaceRoots, &bNestedSourceValid]()
		{
			if (bInReplaceRoots)
			{
				// One root replaced by one root: the baseline can fail on stale publication without
				// requiring a ranged-for size ensure or touching freed root-array storage.
				Tree->SetRootItems({NewRoot.Get()});
			}
			else
			{
				UDreamTreeProviderReentryNode* ChangedNode = bInDeepCallback ? Child.Get() : Root.Get();
				ChangedNode->Children = {NewChild.Get()};
				Tree->RefreshTree();
			}
			bNestedSourceValid = TestSource(InTest, *Tree, ExpectedItems, ExpectedDepths);
		};
		Tree->SetRootItems({Root.Get()});

		InTest.TestEqual(FString::Printf(TEXT("one provider mutation: interface=%d, deep=%d, replace=%d"),
			bInInterfaceProvider, bInDeepCallback, bInReplaceRoots), Probe->MutationCount, 1);
		InTest.TestTrue(TEXT("the nested walk published the expected source before returning"), bNestedSourceValid);
		InTest.TestEqual(TEXT("the public root source still has one root"), Tree->RootItems.Num(), 1);
		if (Tree->RootItems.Num() == 1)
		{
			InTest.TestTrue(TEXT("the public root source keeps the latest root"),
				Tree->RootItems[0].Get() == (bInReplaceRoots ? NewRoot.Get() : Root.Get()));
		}
		return TestSource(InTest, *Tree, ExpectedItems, ExpectedDepths) && bNestedSourceValid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTreeProviderRootReplacementReentryTest,
	"DreamGUI.Controls.TreeView.AChildrenProviderReplacingRootsCannotBeOverwrittenByTheOldWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeProviderRootReplacementReentryTest::RunTest(const FString& Parameters)
{
	bool bValid = true;
	for (bool bInterface : {false, true})
	{
		for (bool bDeep : {false, true})
		{
			bValid = DreamTreeProviderReentryTestLocal::RunCase(*this, bInterface, bDeep, true) && bValid;
		}
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTreeProviderNestedRefreshReentryTest,
	"DreamGUI.Controls.TreeView.AChildrenProviderRefreshingTheTreeCancelsEveryAncestorWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeProviderNestedRefreshReentryTest::RunTest(const FString& Parameters)
{
	bool bValid = true;
	for (bool bInterface : {false, true})
	{
		for (bool bDeep : {false, true})
		{
			bValid = DreamTreeProviderReentryTestLocal::RunCase(*this, bInterface, bDeep, false) && bValid;
		}
	}
	return bValid;
}

#endif
