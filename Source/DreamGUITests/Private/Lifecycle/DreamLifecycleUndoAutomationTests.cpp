// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamUIBPLibrary.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "UObject/Package.h"

/*
 * Undo and the trees a world makes.
 *
 * The transaction buffer records any transactional object that is Modify()'d, transient or not. A tree
 * the level editor builds for a panel, or the runtime API builds in a world, was made transactional,
 * and tearing it down Modify()'d every widget in it -- so an edit that rebuilt a panel carried the old
 * tree in its record, and undoing the edit brought that tree back to life beside the new one, and
 * registered it. Outside a transaction, the same Modify() marked the map dirty. Only an authored tree
 * -- in an asset, or built in the level by the editor's own tools -- belongs in undo.
 */

namespace DreamLifecycleUndoTestLocal
{
	FString DescribeTransactional(const TArray<UDreamWidget*>& InWidgets)
	{
		TArray<FString> Found;
		for (const UDreamWidget* Widget : InWidgets)
		{
			if (Widget->HasAnyFlags(RF_Transactional))
			{
				Found.Add(Widget->GetPathName());
			}
		}
		return DreamTests::Lifecycle::JoinLines(Found);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleWorldTreesAreNotTransactionalTest,
	"DreamGUI.Lifecycle.ATreeAWorldMakesIsNotUndosToRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleWorldTreesAreNotTransactionalTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;
	using namespace DreamLifecycleUndoTestLocal;

	FScopedPanelClass Panel(TEXT("LifecycleUndoFlags"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWidget* Root = Actor->GetWidgetComponent()->GetLoadedWidget();
	if (!TestNotNull(TEXT("with its tree"), Root))return false;

	TArray<UDreamWidget*> PanelWidgets;
	UDreamWidget::CollectChildrenWidgets(Root, PanelWidgets, true);
	TestTrue(FString::Printf(TEXT("no widget of the tree the level editor built for the panel is transactional (found: %s)"), *DescribeTransactional(PanelWidgets)),
		DescribeTransactional(PanelWidgets) == TEXT("none"));

	UDreamWidget* Made = UDreamUIBPLibrary::ConstructWidget(Level.World, TEXT("Made"), nullptr);
	if (!TestNotNull(TEXT("the runtime API makes a widget in the world"), Made))return false;
	TestFalse(TEXT("which is not transactional either"), Made->HasAnyFlags(RF_Transactional));

	UDreamWidget* Inner = PanelWidgets.Num() > 1 ? PanelWidgets[1] : nullptr;
	if (TestNotNull(TEXT("the panel has a widget of its class's own"), Inner))
	{
		UDreamWidget* Copy = UDreamUIBPLibrary::DuplicateWidget(Level.World, Inner, Inner);
		if (TestNotNull(TEXT("which duplicates"), Copy))
		{
			TestFalse(TEXT("into a widget that is not transactional"), Copy->HasAnyFlags(RF_Transactional));
			Copy->DestroyWidget();
		}
	}
	Made->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleWorldTreeTeardownLeavesMapCleanTest,
	"DreamGUI.Lifecycle.TearingDownATreeAWorldMadeLeavesTheMapClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleWorldTreeTeardownLeavesMapCleanTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedWorld Level(EWorldType::Editor);
	UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(Level.World, TEXT("Root"), nullptr);
	UDreamWidget* Child = UDreamUIBPLibrary::ConstructWidget(Level.World, TEXT("Child"), nullptr);
	if (!TestNotNull(TEXT("a root made in the world"), Root) || !TestNotNull(TEXT("and a child"), Child))return false;
	TestTrue(TEXT("the child joins the root"), Child->TrySetParent(Root, false));

	UPackage* Map = Level.World->GetOutermost();
	Map->SetDirtyFlag(false);
	Root->DestroyWidget();
	TestFalse(TEXT("tearing the tree down, outside any transaction, does not mark the map as modified"), Map->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleUndoBringsBackNoDestroyedTreeTest,
	"DreamGUI.Lifecycle.UndoingAnEditThatRebuiltAPanelBringsBackNoTreeItsHostDestroyed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleUndoBringsBackNoDestroyedTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	FScopedPanelClass Before(TEXT("LifecycleUndoBefore"));
	FScopedPanelClass After(TEXT("LifecycleUndoAfter"));
	if (!TestNotNull(TEXT("the first panel class compiled"), Before.GetClass()) || !TestNotNull(TEXT("and the second"), After.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Before.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWorldWidgetComponent* Presenter = Actor->GetWidgetComponent();
	UDreamWidget* OldRoot = Presenter->GetLoadedWidget();
	if (!TestNotNull(TEXT("with its tree"), OldRoot))return false;

	// An edit that rebuilds the panel: the old tree is torn down inside the transaction.
	GEditor->BeginTransaction(FText::FromString(TEXT("Change the panel's class")));
	Presenter->Modify();
	Presenter->SetWidgetClass(After.GetClass());
	GEditor->EndTransaction();
	TestFalse(TEXT("the edit destroyed the old tree"), IsValid(OldRoot));
	const UTransactor* Transactor = GEditor->Trans;
	const FTransaction* Edit = Transactor->GetQueueLength() > 0 ? Transactor->GetTransaction(Transactor->GetQueueLength() - 1) : nullptr;
	if (TestNotNull(TEXT("and was recorded"), Edit))
	{
		TestTrue(TEXT("with the presenter in it"), Edit->ContainsObject(Presenter));
		TestFalse(TEXT("but not the tree it tore down"), Edit->ContainsObject(OldRoot));
	}

	GEditor->UndoTransaction();
	TestFalse(TEXT("undoing the edit leaves the old tree destroyed"), IsValid(OldRoot));
	TestFalse(TEXT("and unregistered"), OldRoot->HasRegistered());
	return true;
}

#endif
