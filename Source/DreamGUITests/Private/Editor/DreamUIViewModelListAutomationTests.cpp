// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Binding/DreamUIEachAdapter.h"
#include "Controls/DreamButton.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIForAdapter.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "DreamEventBindingTestTypes.h"
#include "DreamForLoopTestTypes.h"
#include "DreamScopedWorld.h"
#include "DreamViewModelListTestTypes.h"
#include "DreamViewModelTestTypes.h"
#include "DreamWidgetBlueprint.h"
#include "Event/DreamUIEventDelegate.h"
#include "Interaction/UIListView.h"
#include "Interaction/UIRecyclableScrollView.h"

#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * Lists over view models (plan 17, section E): a `for` or an `each` whose rows follow their OWN items.
 *
 *     for Item in Player.Items {
 *         HorizontalBox Row { Text ItemName { Text <- Item.Name }  Native.Button UseButton { OnClicked -> Item.Use() } }
 *     }
 *
 * Three things are pinned: the items are read through the member path; a member an item announces is written onto that
 * item's row alone, at once, with no refresh (and a member it does not announce waits for one); and every row's events
 * call its own item -- the routes moving with a recycled cell, never calling two items, never left behind.
 *
 * Most of it drives the adapters directly over a tree assembled by hand in a game world, with the binding the builder
 * would have recorded written out here: a red test there is the adapters'. One test compiles a real .dui end to end --
 * the `viewmodels` entry, the path source and the routes through the compiler and UDreamUserWidget -- and is the only
 * one that also depends on the parser, the compiler and the user widget's path watcher.
 */

namespace DreamUIViewModelListTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, UClass* InClass = UDreamWidget::StaticClass())
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, InClass, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(InName);
		Widget->SetParentBeforeRegister(InParent);
		return Widget;
	}

	/**
	 * Owner > Host > Entry (> Inner), registered in a game world, Entry a `for` template. Entry is a UDreamTestRouteWidget,
	 * so every kind of event a route can name is on the row's root.
	 */
	struct FListTree
	{
		UDreamUserWidget* Owner = nullptr;
		UDreamWidget* Host = nullptr;
		UDreamTestRouteWidget* Template = nullptr;

		explicit FListTree(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InOwnerClass = UDreamTestListHost::StaticClass())
		{
			Owner = CreateDreamWidget(InWorld, InOwnerClass);
			if (Owner == nullptr)
			{
				return;
			}
			Host = MakeWidget(InWorld, Owner, TEXT("Host"));
			Template = Cast<UDreamTestRouteWidget>(MakeWidget(InWorld, Host, TEXT("Entry"), UDreamTestRouteWidget::StaticClass()));
			if (Template != nullptr)
			{
				MakeWidget(InWorld, Template, TEXT("Inner"));
			}
			RegisterDreamWidgetHierarchy(Host);
		}

		bool IsValidFixture() const { return Owner != nullptr && Host != nullptr && Template != nullptr; }

		UDreamTestListHost* ListHost() const { return Cast<UDreamTestListHost>(Owner); }
	};

	/** `for Item in Player.Items` (or `each`, bInPanel false) with nothing in its body yet. */
	FDreamWidgetEachBinding MakeBinding(bool bInPanel)
	{
		FDreamWidgetEachBinding Binding;
		Binding.bInPanel = bInPanel;
		Binding.HostWidgetName = TEXT("Host");
		Binding.TemplateWidgetName = TEXT("Entry");
		Binding.SourcePath = { TEXT("Player"), TEXT("Items") };
		Binding.SourceName = TEXT("Items");
		Binding.bSourceIsFunction = false;
		Binding.LoopVariable = TEXT("Item");
		return Binding;
	}

	/** `ToolTipText <- Item.<InMember>` on the row's widget InWidget, through the setter. */
	void AddEntry(FDreamWidgetEachBinding& InOutBinding, const TCHAR* InWidget, const TCHAR* InMember)
	{
		FDreamWidgetEntryBinding& Entry = InOutBinding.EntryBindings.AddDefaulted_GetRef();
		Entry.TargetWidgetDisplayName = InWidget;
		Entry.Target = EDreamWidgetBindingTarget::Widget;
		Entry.PropertyName = TEXT("ToolTipText");
		Entry.SetterName = TEXT("SetToolTipText");
		Entry.ItemMember = InMember;
	}

	/** `<InEvent> -> Item.<InFunction>` (or `Item.<InFunction>()`) on the row's root. */
	void AddRoute(FDreamWidgetEachBinding& InOutBinding, const TCHAR* InEvent, const TCHAR* InFunction, bool bInCallWithoutArguments)
	{
		FDreamWidgetEntryRoute& Route = InOutBinding.EntryRoutes.AddDefaulted_GetRef();
		Route.TargetWidgetDisplayName = TEXT("Entry");
		Route.Target = EDreamWidgetBindingTarget::Widget;
		Route.EventName = InEvent;
		Route.ItemFunction = InFunction;
		Route.bCallWithoutArguments = bInCallWithoutArguments;
	}

	UDreamTestCountingItemVM* MakeItem(UObject* InOuter, const FString& InName)
	{
		UDreamTestCountingItemVM* Item = NewObject<UDreamTestCountingItemVM>(InOuter);
		// Written directly: nobody is listening yet, and a test that wants an announcement calls the setter.
		Item->Name = FText::FromString(InName);
		Item->CountText = FText::FromString(InName + TEXT(" count"));
		Item->Untracked = FText::FromString(InName + TEXT(" untracked"));
		return Item;
	}

	TArray<TObjectPtr<UDreamTestItemVM>> ItemList(std::initializer_list<UDreamTestItemVM*> InItems)
	{
		TArray<TObjectPtr<UDreamTestItemVM>> Result;
		for (UDreamTestItemVM* Item : InItems)
		{
			Result.Add(Item);
		}
		return Result;
	}

	FString TooltipOf(const UDreamWidget* InWidget)
	{
		return IsValid(InWidget) ? InWidget->GetToolTipText().ToString() : FString(TEXT("<none>"));
	}

	FString InnerTooltipOf(const UDreamWidget* InRow)
	{
		return IsValid(InRow) ? TooltipOf(InRow->FindChildByDisplayName(TEXT("Inner"), false)) : FString(TEXT("<none>"));
	}

	UDreamTestRouteWidget* AsRow(UDreamWidget* InWidget)
	{
		return Cast<UDreamTestRouteWidget>(InWidget);
	}

	// ---------------------------------------------------------------------------------------- compiled fixtures

	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}
		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, false, true, true);
		}
		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}
		FString FilePath;
	};

	/** A .dui-backed Blueprint in /Temp, deriving from InParentClass, pointed at InFilePath and compiled. */
	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FCompilerResultsLog Results;

		FScopedTextBlueprint(const TCHAR* InName, UClass* InParentClass, const FString& InFilePath)
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				Blueprint = nullptr;
				return;
			}
			Defaults->SourceFile.FilePath = InFilePath;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		}
		~FScopedTextBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		UClass* GetClass() const { return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr; }
	};

	/** The copies of InTemplate: its siblings carrying its display name, in the order they stand. */
	TArray<UDreamWidget*> CopiesOf(const UDreamWidget* InTemplate)
	{
		TArray<UDreamWidget*> Copies;
		const UDreamWidget* Panel = IsValid(InTemplate) ? InTemplate->GetParent() : nullptr;
		if (Panel == nullptr)
		{
			return Copies;
		}
		for (UDreamWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child) && Child != InTemplate && Child->GetDisplayName() == InTemplate->GetDisplayName())
			{
				Copies.Add(Child);
			}
		}
		return Copies;
	}

	FString TextIn(const UDreamWidget* InRow, const TCHAR* InName)
	{
		// A `Text` node is a widget whose VISUAL is the UDreamText; the widget itself never is one.
		const UDreamWidget* Node = IsValid(InRow) ? InRow->FindChildByDisplayName(InName, true) : nullptr;
		const UDreamText* Text = IsValid(Node) ? Cast<UDreamText>(Node->GetVisual()) : nullptr;
		return Text != nullptr ? Text->GetText().ToString() : FString(TEXT("<none>"));
	}
}

// ============================================================================================ items through a path

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListPathSourceTest,
	"DreamGUI.ViewModel.List.ForReadsItsItemsThroughAMemberPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListPathSourceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelListTestLocal;
	FScopedGameWorld TestWorld;
	FListTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()) || !TestNotNull(TEXT("owned by a list host"), Tree.ListHost()))
	{
		return false;
	}

	FDreamWidgetEachBinding Binding = MakeBinding(/*bInPanel*/true);
	AddEntry(Binding, TEXT("Entry"), TEXT("Name"));
	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, Binding, Tree.Host, Tree.Template);
	// No view model yet: the path is broken, which is a source with nothing in it, not an error.
	TestEqual(TEXT("a path through an unset view model lists nothing"), Adapter->GetCopies().Num(), 0);

	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Tree.Owner);
	Player->Items = ItemList({ MakeItem(Player, TEXT("Sword")), MakeItem(Player, TEXT("Shield")) });
	Tree.ListHost()->Player = Player;
	Adapter->Refresh();
	TArray<UDreamWidget*> Copies = Adapter->GetCopies();
	if (TestEqual(TEXT("the items are read through the path"), Copies.Num(), 2))
	{
		TestEqual(TEXT("the first copy shows the first item"), TooltipOf(Copies[0]), FString(TEXT("Sword")));
		TestEqual(TEXT("the second copy shows the second"), TooltipOf(Copies[1]), FString(TEXT("Shield")));
	}

	// The array replaced on the view model: the same path, new items.
	Player->SetItems(ItemList({ MakeItem(Player, TEXT("Bow")) }));
	Adapter->Refresh();
	Copies = Adapter->GetCopies();
	if (TestEqual(TEXT("a replaced array is read again"), Copies.Num(), 1))
	{
		TestEqual(TEXT("and shown"), TooltipOf(Copies[0]), FString(TEXT("Bow")));
	}

	// Another view model altogether: the path's first member now holds a different object.
	UDreamTestPlayerVM* Other = NewObject<UDreamTestPlayerVM>(Tree.Owner);
	Other->Items = ItemList({ MakeItem(Other, TEXT("Ring")), MakeItem(Other, TEXT("Amulet")), MakeItem(Other, TEXT("Boots")) });
	Tree.ListHost()->Player = Other;
	Adapter->Refresh();
	Copies = Adapter->GetCopies();
	if (TestEqual(TEXT("a swapped view model's items are read"), Copies.Num(), 3))
	{
		TestEqual(TEXT("in its order"), TooltipOf(Copies[2]), FString(TEXT("Boots")));
	}

	// And broken again: every copy goes.
	Tree.ListHost()->Player = nullptr;
	Adapter->Refresh();
	TestEqual(TEXT("a path broken again lists nothing"), Adapter->GetCopies().Num(), 0);

	Tree.Owner->DestroyWidget();
	return true;
}

// ============================================================================================ per-item watching

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListForWatchTest,
	"DreamGUI.ViewModel.List.ForCopyFollowsItsOwnItemWithoutARefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListForWatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelListTestLocal;
	FScopedGameWorld TestWorld;
	FListTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()) || !TestNotNull(TEXT("owned by a list host"), Tree.ListHost()))
	{
		return false;
	}
	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Tree.Owner);
	UDreamTestCountingItemVM* A = MakeItem(Player, TEXT("A"));
	UDreamTestCountingItemVM* B = MakeItem(Player, TEXT("B"));
	UDreamTestCountingItemVM* C = MakeItem(Player, TEXT("C"));
	Player->Items = ItemList({ A, B, C });
	Tree.ListHost()->Player = Player;

	// Name is FieldNotify on the item, Untracked is not.
	FDreamWidgetEachBinding Binding = MakeBinding(/*bInPanel*/true);
	AddEntry(Binding, TEXT("Entry"), TEXT("Name"));
	AddEntry(Binding, TEXT("Inner"), TEXT("Untracked"));
	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, Binding, Tree.Host, Tree.Template);
	const TArray<UDreamWidget*> Copies = Adapter->GetCopies();
	if (!TestEqual(TEXT("a copy per item"), Copies.Num(), 3))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}

	TestEqual(TEXT("every copy watches its item"), Adapter->GetRows().GetWatchedRowCount(), 3);
	// One subscription a copy: Name. Untracked announces nothing, so nothing is placed for it.
	TestEqual(TEXT("on the one member the item announces"), Adapter->GetRows().GetSubscriptionCount(), 3);
	TestEqual(TEXT("which is one delegate on the item"), B->LiveDelegateCount, 1);

	// One item's member, announced: that copy, that entry, nothing else -- and no refresh asked for.
	const int32 WritesBefore = Adapter->GetEntryWriteCount();
	B->SetName(FText::FromString(TEXT("B renamed")));
	TestEqual(TEXT("the item's own copy shows the change at once"), TooltipOf(Copies[1]), FString(TEXT("B renamed")));
	TestEqual(TEXT("written once: one entry of one copy"), Adapter->GetEntryWriteCount() - WritesBefore, 1);
	TestEqual(TEXT("the copy before it is untouched"), TooltipOf(Copies[0]), FString(TEXT("A")));
	TestEqual(TEXT("and the copy after it"), TooltipOf(Copies[2]), FString(TEXT("C")));

	// A member nobody announces waits for a refresh -- the report for Name rewrites Name only.
	A->Untracked = FText::FromString(TEXT("A changed quietly"));
	A->SetName(FText::FromString(TEXT("A renamed")));
	TestEqual(TEXT("an announced member of the same item is shown"), TooltipOf(Copies[0]), FString(TEXT("A renamed")));
	TestEqual(TEXT("an unannounced one is not, until a refresh"), InnerTooltipOf(Copies[0]), FString(TEXT("A untracked")));
	Adapter->Refresh();
	TestEqual(TEXT("a refresh writes it"), InnerTooltipOf(Copies[0]), FString(TEXT("A changed quietly")));
	TestTrue(TEXT("and keeps every copy"), Adapter->GetCopies() == Copies);
	TestEqual(TEXT("re-aiming nothing: still one delegate on the item"), A->LiveDelegateCount, 1);

	// An item that leaves the list takes its watcher with it.
	Player->SetItems(ItemList({ A, C }));
	Adapter->Refresh();
	TestEqual(TEXT("the item that left is watched no more"), B->LiveDelegateCount, 0);
	const int32 WritesAfterLeaving = Adapter->GetEntryWriteCount();
	B->SetName(FText::FromString(TEXT("B gone")));
	TestEqual(TEXT("and writes nothing when it changes"), Adapter->GetEntryWriteCount(), WritesAfterLeaving);

	// Released: no delegate left on any item.
	Adapter->ReleaseCopies();
	TestEqual(TEXT("a released list leaves nothing on its first item"), A->LiveDelegateCount, 0);
	TestEqual(TEXT("nor on its last"), C->LiveDelegateCount, 0);
	TestEqual(TEXT("and keeps no rows"), Adapter->GetRows().Num(), 0);
	Tree.Owner->DestroyWidget();

	// Items whose class announces nothing get no watcher at all: a plain object on the old one-segment source costs what
	// a row cost before watching existed.
	FListTree PlainTree(TestWorld.World, UDreamForLoopTestHost::StaticClass());
	UDreamForLoopTestHost* PlainOwner = Cast<UDreamForLoopTestHost>(PlainTree.Owner);
	if (!TestTrue(TEXT("the plain fixture was assembled"), PlainTree.IsValidFixture() && PlainOwner != nullptr))
	{
		return false;
	}
	UDreamTestPlainModel* First = NewObject<UDreamTestPlainModel>(PlainOwner);
	First->Name = FText::FromString(TEXT("Plain"));
	PlainOwner->Options.Add(First);
	FDreamWidgetEachBinding PlainBinding = MakeBinding(/*bInPanel*/true);
	PlainBinding.SourcePath.Reset();
	PlainBinding.SourceName = TEXT("Options");
	AddEntry(PlainBinding, TEXT("Entry"), TEXT("Name"));
	UDreamUIForAdapter* PlainAdapter = NewObject<UDreamUIForAdapter>(PlainOwner);
	PlainAdapter->Initialize(PlainOwner, PlainBinding, PlainTree.Host, PlainTree.Template);
	const TArray<UDreamWidget*> PlainCopies = PlainAdapter->GetCopies();
	if (TestEqual(TEXT("the plain item has its copy"), PlainCopies.Num(), 1))
	{
		TestEqual(TEXT("with its value written"), TooltipOf(PlainCopies[0]), FString(TEXT("Plain")));
	}
	TestEqual(TEXT("and no watcher"), PlainAdapter->GetRows().GetWatchedRowCount(), 0);
	TestEqual(TEXT("and no subscription"), PlainAdapter->GetRows().GetSubscriptionCount(), 0);
	PlainTree.Owner->DestroyWidget();
	return true;
}

// ============================================================================================ per-item routes

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListForRoutesTest,
	"DreamGUI.ViewModel.List.ForRoutesCallTheCopysOwnItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListForRoutesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelListTestLocal;
	FScopedGameWorld TestWorld;
	FListTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()) || !TestNotNull(TEXT("owned by a list host"), Tree.ListHost()))
	{
		return false;
	}
	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Tree.Owner);
	UDreamTestCountingItemVM* A = MakeItem(Player, TEXT("A"));
	UDreamTestCountingItemVM* B = MakeItem(Player, TEXT("B"));
	UDreamTestCountingItemVM* C = MakeItem(Player, TEXT("C"));
	Player->Items = ItemList({ A, B, C });
	Tree.ListHost()->Player = Player;

	FDreamWidgetEachBinding Binding = MakeBinding(/*bInPanel*/true);
	AddEntry(Binding, TEXT("Entry"), TEXT("Name"));
	// One route on every kind of event, each in the shape it fits...
	AddRoute(Binding, TEXT("OnClicked"), TEXT("Use"), /*bCallWithoutArguments*/true);           // multicast, nothing sent
	AddRoute(Binding, TEXT("OnValueChanged"), TEXT("UseWithValue"), false);                     // multicast, value forwarded
	AddRoute(Binding, TEXT("OnPicked"), TEXT("Use"), false);                                    // single-cast, takes nothing
	AddRoute(Binding, TEXT("OnAmount"), TEXT("UseWithValue"), false);                           // single-cast, value forwarded
	AddRoute(Binding, TEXT("OnSubmit"), TEXT("Use"), true);                                     // FDreamUIEventDelegate, Empty
	AddRoute(Binding, TEXT("OnScale"), TEXT("UseWithValue"), false);                            // FDreamUIEventDelegate, Float
	// ...and one that fits nothing: OnClicked sends no value for UseWithValue to take. Skipped, never placed -- were it,
	// the click below would call UseWithValue with a frame that has no float in it.
	AddRoute(Binding, TEXT("OnClicked"), TEXT("UseWithValue"), false);

	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, Binding, Tree.Host, Tree.Template);
	const TArray<UDreamWidget*> Copies = Adapter->GetCopies();
	if (!TestEqual(TEXT("a copy per item"), Copies.Num(), 3)
		|| !TestNotNull(TEXT("copy 0 is a route row"), AsRow(Copies[0]))
		|| !TestNotNull(TEXT("copy 1 is a route row"), AsRow(Copies[1]))
		|| !TestNotNull(TEXT("copy 2 is a route row"), AsRow(Copies[2])))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}
	TestEqual(TEXT("six routes a copy, the misfit skipped"), Adapter->GetRows().GetRouteCount(), 18);
	TestFalse(TEXT("the template is routed to nothing"), Tree.Template->OnClicked.IsBound() || Tree.Template->OnPicked.IsBound());

	// The second copy's click: the second item, once.
	AsRow(Copies[1])->OnClicked.Broadcast();
	TestEqual(TEXT("a click calls the copy's own item"), B->UseCount, 1);
	TestEqual(TEXT("not the first item"), A->UseCount, 0);
	TestEqual(TEXT("nor the last"), C->UseCount, 0);

	AsRow(Copies[1])->OnValueChanged.Broadcast(0.25f);
	TestEqual(TEXT("a value event calls the item's function"), B->UseCount, 2);
	TestEqual(TEXT("with the value it sent"), B->LastValue, 0.25f);

	AsRow(Copies[2])->OnPicked.ExecuteIfBound();
	TestEqual(TEXT("a single-cast delegate holds the copy's item"), C->UseCount, 1);
	AsRow(Copies[2])->OnAmount.ExecuteIfBound(0.75f);
	TestEqual(TEXT("and forwards a value"), C->UseCount, 2);
	TestEqual(TEXT("the value"), C->LastValue, 0.75f);

	AsRow(Copies[0])->OnSubmit.FireEvent();
	TestEqual(TEXT("an event delegate routes to the copy's item"), A->UseCount, 1);
	AsRow(Copies[0])->OnScale.FireEvent(0.5f);
	TestEqual(TEXT("and forwards its value"), A->UseCount, 2);
	TestEqual(TEXT("the value"), A->LastValue, 0.5f);

	// Reordered: a `for` keeps each item's copy and moves it, so the routes travel with the copies, and do not double.
	Player->SetItems(ItemList({ C, A, B }));
	Adapter->Refresh();
	Adapter->Refresh();
	const TArray<UDreamWidget*> Reordered = Adapter->GetCopies();
	if (TestTrue(TEXT("a reorder keeps the copies"), Reordered == TArray<UDreamWidget*>({ Copies[2], Copies[0], Copies[1] })))
	{
		AsRow(Reordered[0])->OnClicked.Broadcast();
		TestEqual(TEXT("the first row now calls the item that moved there"), C->UseCount, 3);
		TestEqual(TEXT("and only it"), A->UseCount + B->UseCount, 4);
	}
	TestEqual(TEXT("the routes were not placed twice"), Adapter->GetRows().GetRouteCount(), 18);

	// An item that leaves takes its copy, and nothing calls it any more.
	UDreamTestRouteWidget* CopyOfB = AsRow(Copies[1]);
	Player->SetItems(ItemList({ C, A }));
	Adapter->Refresh();
	TestFalse(TEXT("the copy of the item that left is gone"), IsValid(CopyOfB));
	TestEqual(TEXT("taking its routes"), Adapter->GetRows().GetRouteCount(), 12);

	// Released: nothing left on any item.
	Adapter->ReleaseCopies();
	TestEqual(TEXT("a released list has no routes"), Adapter->GetRows().GetRouteCount(), 0);
	TestEqual(TEXT("and no watcher on its items"), A->LiveDelegateCount + B->LiveDelegateCount + C->LiveDelegateCount, 0);

	Tree.Owner->DestroyWidget();
	return true;
}

// ============================================================================================ each: recycled cells

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListEachRecycleTest,
	"DreamGUI.ViewModel.List.EachCellCarriesItsWatcherAndRoutesWhenRecycled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListEachRecycleTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelListTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTestListHost* Owner = CreateDreamWidget<UDreamTestListHost>(TestWorld.World);
	if (!TestNotNull(TEXT("the owner was made"), Owner))
	{
		return false;
	}
	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Owner);
	TArray<UDreamTestCountingItemVM*> Items;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Items.Add(MakeItem(Player, FString::Printf(TEXT("I%d"), Index)));
		Player->Items.Add(Items.Last());
	}
	Owner->Player = Player;

	FDreamWidgetEachBinding Binding = MakeBinding(/*bInPanel*/false);
	AddEntry(Binding, TEXT("Entry"), TEXT("Name"));
	AddEntry(Binding, TEXT("Inner"), TEXT("CountText"));
	AddRoute(Binding, TEXT("OnClicked"), TEXT("Use"), /*bCallWithoutArguments*/true);
	AddRoute(Binding, TEXT("OnPicked"), TEXT("Use"), false);
	AddRoute(Binding, TEXT("OnScale"), TEXT("UseWithValue"), false);

	// No view: the cells a view showing two rows of six would keep, built here and set the way the view sets them. What
	// matters is what SetCell does with a cell it is handed again for another index -- which is all a recycle is.
	UDreamUIEachAdapter* Adapter = NewObject<UDreamUIEachAdapter>(Owner);
	Adapter->Initialize(Owner, Binding, nullptr);
	TestEqual(TEXT("the items are read through the path"), Adapter->GetItems().Num(), 6);

	UDreamWidget* Content = MakeWidget(TestWorld.World, Owner, TEXT("Content"));
	UDreamTestRouteWidget* Cells[2] = { nullptr, nullptr };
	for (int32 CellIndex = 0; CellIndex < 2; ++CellIndex)
	{
		Cells[CellIndex] = Cast<UDreamTestRouteWidget>(MakeWidget(TestWorld.World, Content, TEXT("Entry"), UDreamTestRouteWidget::StaticClass()));
		MakeWidget(TestWorld.World, Cells[CellIndex], TEXT("Inner"));
	}
	RegisterDreamWidgetHierarchy(Content);
	UUIListEntry* Entries[2] = { Cells[0]->AddComponent<UUIListEntry>(), Cells[1]->AddComponent<UUIListEntry>() };
	if (!TestNotNull(TEXT("cell 0 has its entry"), Entries[0]) || !TestNotNull(TEXT("cell 1 has its entry"), Entries[1]))
	{
		Owner->DestroyWidget();
		return false;
	}
	auto SetCell = [Adapter](UUIListEntry* InEntry, int32 InIndex)
	{
		IUIRecyclableScrollViewDataSource::Execute_BeforeSetCell(Adapter);
		IUIRecyclableScrollViewDataSource::Execute_SetCell(Adapter, InEntry, InIndex);
		IUIRecyclableScrollViewDataSource::Execute_AfterSetCell(Adapter);
	};

	SetCell(Entries[0], 0);
	SetCell(Entries[1], 1);
	TestEqual(TEXT("cell 0 shows item 0"), TooltipOf(Cells[0]), FString(TEXT("I0")));
	TestEqual(TEXT("one level down too"), InnerTooltipOf(Cells[0]), FString(TEXT("I0 count")));
	TestEqual(TEXT("cell 1 shows item 1"), TooltipOf(Cells[1]), FString(TEXT("I1")));
	TestEqual(TEXT("a shown item is watched on both members"), Items[0]->LiveDelegateCount, 2);
	TestEqual(TEXT("an item no cell shows is not watched"), Items[4]->LiveDelegateCount, 0);

	// One item's change, one cell.
	int32 Writes = Adapter->GetEntryWriteCount();
	Items[1]->SetName(FText::FromString(TEXT("I1 renamed")));
	TestEqual(TEXT("the cell showing the item follows it"), TooltipOf(Cells[1]), FString(TEXT("I1 renamed")));
	TestEqual(TEXT("the other cell does not move"), TooltipOf(Cells[0]), FString(TEXT("I0")));
	TestEqual(TEXT("one write"), Adapter->GetEntryWriteCount() - Writes, 1);

	// Recycled: cell 0 scrolled down to item 4.
	SetCell(Entries[0], 4);
	TestEqual(TEXT("the recycled cell shows its new item"), TooltipOf(Cells[0]), FString(TEXT("I4")));
	TestEqual(TEXT("the item it left is watched no more"), Items[0]->LiveDelegateCount, 0);
	TestEqual(TEXT("the item it shows is"), Items[4]->LiveDelegateCount, 2);
	Items[0]->SetName(FText::FromString(TEXT("I0 renamed")));
	TestEqual(TEXT("a change of the item it left does not reach it"), TooltipOf(Cells[0]), FString(TEXT("I4")));
	Items[4]->SetCountText(FText::FromString(TEXT("I4 recounted")));
	TestEqual(TEXT("a change of the item it shows does"), InnerTooltipOf(Cells[0]), FString(TEXT("I4 recounted")));

	// The routes moved with it: the old item off, the new one on, never both.
	Cells[0]->OnClicked.Broadcast();
	TestEqual(TEXT("the recycled cell's click calls its new item"), Items[4]->UseCount, 1);
	TestEqual(TEXT("and not the item it showed before"), Items[0]->UseCount, 0);
	Cells[0]->OnPicked.ExecuteIfBound();
	TestEqual(TEXT("its single-cast delegate holds the new item"), Items[4]->UseCount, 2);
	Cells[0]->OnScale.FireEvent(0.5f);
	TestEqual(TEXT("its event delegate calls the new item"), Items[4]->UseCount, 3);
	TestEqual(TEXT("with the value"), Items[4]->LastValue, 0.5f);
	TestEqual(TEXT("and none of them the old"), Items[0]->UseCount, 0);
	TestEqual(TEXT("three routes a cell"), Adapter->GetRows().GetRouteCount(), 6);

	// Set again with the item it already shows -- UpdateCellData over unchanged rows: nothing moves, nothing doubles.
	SetCell(Entries[0], 4);
	Cells[0]->OnClicked.Broadcast();
	TestEqual(TEXT("a cell set twice to one item calls it once"), Items[4]->UseCount, 4);
	TestEqual(TEXT("and watches it once"), Items[4]->LiveDelegateCount, 2);

	// A cell given no item (an index past the end) calls nobody and hears nobody, but a single-cast delegate something
	// else set since is left as that something set it.
	UDreamEventBindingTestHandler* Handler = NewObject<UDreamEventBindingTestHandler>(Owner);
	Cells[1]->OnPicked.BindUFunction(Handler, TEXT("HandleEmpty"));
	SetCell(Entries[1], 99);
	Cells[1]->OnClicked.Broadcast();
	TestEqual(TEXT("a cell with no item calls nobody"), Items[1]->UseCount, 0);
	TestEqual(TEXT("and leaves its last item unwatched"), Items[1]->LiveDelegateCount, 0);
	Cells[1]->OnPicked.ExecuteIfBound();
	TestEqual(TEXT("a delegate set by someone else since is not cleared"), Handler->EmptyCallCount, 1);

	// Given an item again: its own route takes the single-cast slot, as `=` does.
	SetCell(Entries[1], 3);
	Cells[1]->OnPicked.ExecuteIfBound();
	TestEqual(TEXT("the slot is the cell's item's again"), Items[3]->UseCount, 1);
	TestEqual(TEXT("not the handler's"), Handler->EmptyCallCount, 1);

	// A cell the view destroyed: its row goes at the next pass, and its item is watched no more.
	Cells[1]->DestroyWidget();
	IUIRecyclableScrollViewDataSource::Execute_BeforeSetCell(Adapter);
	TestEqual(TEXT("a destroyed cell's item is watched no more"), Items[3]->LiveDelegateCount, 0);
	TestEqual(TEXT("and only the live cell keeps a row"), Adapter->GetRows().Num(), 1);

	// The path read again through Refresh: a replaced array, same length, different objects -- with no view to tell,
	// the items are simply read.
	Player->SetItems(ItemList({ Items[5], Items[4], Items[3], Items[2], Items[1], Items[0] }));
	Adapter->Refresh();
	if (TestEqual(TEXT("a refresh reads the path again"), Adapter->GetItems().Num(), 6))
	{
		TestEqual(TEXT("in the new order"), Adapter->GetItems()[0].Get(), static_cast<UObject*>(Items[5]));
	}

	Owner->DestroyWidget();
	return true;
}

// ============================================================================================ cost

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListOneRowTest,
	"DreamGUI.ViewModel.List.OneItemChangeTouchesOneRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListOneRowTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelListTestLocal;
	constexpr int32 RowCount = 1000;
	FScopedGameWorld TestWorld;
	FListTree Tree(TestWorld.World);
	if (!TestTrue(TEXT("the fixture tree was assembled"), Tree.IsValidFixture()) || !TestNotNull(TEXT("owned by a list host"), Tree.ListHost()))
	{
		return false;
	}
	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Tree.Owner);
	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		Player->Items.Add(MakeItem(Player, FString::Printf(TEXT("Item %d"), Index)));
	}
	Tree.ListHost()->Player = Player;

	// Two announced members a row, so "one entry" is told apart from "one row".
	FDreamWidgetEachBinding Binding = MakeBinding(/*bInPanel*/true);
	AddEntry(Binding, TEXT("Entry"), TEXT("Name"));
	AddEntry(Binding, TEXT("Inner"), TEXT("CountText"));
	UDreamUIForAdapter* Adapter = NewObject<UDreamUIForAdapter>(Tree.Owner);
	Adapter->Initialize(Tree.Owner, Binding, Tree.Host, Tree.Template);
	const TArray<UDreamWidget*> Copies = Adapter->GetCopies();
	if (!TestEqual(TEXT("a copy per item"), Copies.Num(), RowCount))
	{
		Tree.Owner->DestroyWidget();
		return false;
	}
	TestEqual(TEXT("every entry was written once"), Adapter->GetEntryWriteCount(), RowCount * 2);
	TestEqual(TEXT("every row watches its item"), Adapter->GetRows().GetWatchedRowCount(), RowCount);
	TestEqual(TEXT("on both members"), Adapter->GetRows().GetSubscriptionCount(), RowCount * 2);

	const int32 WritesBefore = Adapter->GetEntryWriteCount();
	const FText Changed = FText::FromString(TEXT("Changed"));
	UDreamTestItemVM* Middle = Player->Items[RowCount / 2];
	Middle->SetName(Changed);
	TestEqual(TEXT("one item changed, one entry written"), Adapter->GetEntryWriteCount() - WritesBefore, 1);
	TestEqual(TEXT("on its own row"), TooltipOf(Copies[RowCount / 2]), FString(TEXT("Changed")));
	TestEqual(TEXT("the row before keeps its value"), TooltipOf(Copies[RowCount / 2 - 1]), FString::Printf(TEXT("Item %d"), RowCount / 2 - 1));
	TestEqual(TEXT("the row after too"), TooltipOf(Copies[RowCount / 2 + 1]), FString::Printf(TEXT("Item %d"), RowCount / 2 + 1));

	// The same value again announces nothing (DREAM_VM_SET compares first), so nothing is written.
	Middle->SetName(Changed);
	TestEqual(TEXT("an unchanged value writes nothing"), Adapter->GetEntryWriteCount() - WritesBefore, 1);

	Tree.Owner->DestroyWidget();
	return true;
}

// ============================================================================================ FDreamUIEventDelegate

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListEventDelegateRouteTest,
	"DreamGUI.ViewModel.List.AnEventDelegateRouteComesOffEvenFromInsideItsOwnEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListEventDelegateRouteTest::RunTest(const FString& Parameters)
{
	UDreamTestRouteWidget* Widget = NewObject<UDreamTestRouteWidget>(GetTransientPackage());
	UDreamTestCountingItemVM* Item = NewObject<UDreamTestCountingItemVM>(GetTransientPackage());

	// `OnScale -> Item.Use()`: an event that fires with a float, routed to a function that takes nothing.
	Widget->OnScale.AddRuntimeRoute(Item, TEXT("Use"), /*bInCallWithoutArguments*/true);
	Widget->OnScale.FireEvent(0.5f);
	TestEqual(TEXT("a no-argument route on a value event calls the function"), Item->UseCount, 1);
	TestEqual(TEXT("taking it off removes the one entry"), Widget->OnScale.RemoveRuntimeRoute(Item, TEXT("Use")), 1);
	Widget->OnScale.FireEvent(0.5f);
	TestEqual(TEXT("and nothing is called any more"), Item->UseCount, 1);
	TestFalse(TEXT("leaving the event unbound"), Widget->OnScale.IsBound());

	// Two handlers that each take their own route off while the event is calling them: the walk goes on over the entries
	// it started with, so both run, once.
	UDreamTestSelfRemovingHandler* First = NewObject<UDreamTestSelfRemovingHandler>(GetTransientPackage());
	UDreamTestSelfRemovingHandler* Second = NewObject<UDreamTestSelfRemovingHandler>(GetTransientPackage());
	First->Widget = Widget;
	Second->Widget = Widget;
	Widget->OnSubmit.AddRuntimeRoute(First, TEXT("RemoveSelf"));
	Widget->OnSubmit.AddRuntimeRoute(Second, TEXT("RemoveSelf"));
	Widget->OnSubmit.FireEvent();
	TestEqual(TEXT("the first handler ran"), First->CallCount, 1);
	TestEqual(TEXT("the second was not skipped by the first taking itself off"), Second->CallCount, 1);
	Widget->OnSubmit.FireEvent();
	TestEqual(TEXT("taken off from inside, the first is not called again"), First->CallCount, 1);
	TestEqual(TEXT("nor the second"), Second->CallCount, 1);
	// Retired in place while the event was firing; the next removal outside one compacts them.
	TestEqual(TEXT("nothing left to remove"), Widget->OnSubmit.RemoveRuntimeRoute(First, TEXT("RemoveSelf")), 0);
	TestFalse(TEXT("and the retired entries are gone"), Widget->OnSubmit.IsBound());
	return true;
}

// ============================================================================================ compiled, end to end

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelListCompiledForTest,
	"DreamGUI.ViewModel.List.ACompiledForOverAViewModelFollowsItsItemsAndRoutesToThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelListCompiledForTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelListTestLocal;

	FScopedDuiFile File(TEXT("ViewModelListForFixture.dui"));
	if (!TestTrue(TEXT("the file was written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_ViewModelListFor"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("VerticalBox Root {"),
		TEXT("    for Item in Player.Items {"),
		TEXT("        HorizontalBox Row {"),
		TEXT("            Text ItemName { Text <- Item.Name }"),
		TEXT("            Text ItemNote { Text <- Item.Untracked }"),
		TEXT("            Native.Button UseButton { OnClicked -> Item.Use() }"),
		TEXT("            Native.Slider AmountSlider { OnValueChanged -> Item.UseWithValue }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedTextBlueprint Fixture(TEXT("BP_ViewModelListFor"), UDreamTextUserWidget::StaticClass(), File.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was made"), Fixture.GetClass()))
	{
		return false;
	}
	TestEqual(TEXT("it compiles clean"), Fixture.Results.NumErrors, 0);

	// The record: a path source and two routes, which is what the adapter is handed.
	TArray<FDreamWidgetEachBinding> Loops;
	UDreamWidgetGeneratedClass::CollectEachBindings(Fixture.GetClass(), Loops);
	if (!TestEqual(TEXT("the class carries the loop"), Loops.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("as a 'for'"), Loops[0].bInPanel);
	TestTrue(TEXT("over the path"), Loops[0].SourcePath == TArray<FName>({ TEXT("Player"), TEXT("Items") }));
	TestEqual(TEXT("whose last member is the source"), Loops[0].SourceName, FName(TEXT("Items")));
	if (TestEqual(TEXT("with both routes"), Loops[0].EntryRoutes.Num(), 2))
	{
		const FDreamWidgetEntryRoute* Click = Loops[0].EntryRoutes.FindByPredicate(
			[](const FDreamWidgetEntryRoute& InRoute) { return InRoute.EventName == FName(TEXT("OnClicked")); });
		const FDreamWidgetEntryRoute* Slide = Loops[0].EntryRoutes.FindByPredicate(
			[](const FDreamWidgetEntryRoute& InRoute) { return InRoute.EventName == FName(TEXT("OnValueChanged")); });
		if (TestNotNull(TEXT("the click route"), Click))
		{
			TestEqual(TEXT("on the button"), Click->TargetWidgetDisplayName, FName(TEXT("UseButton")));
			TestEqual(TEXT("to the item's Use"), Click->ItemFunction, FName(TEXT("Use")));
			TestTrue(TEXT("called with nothing"), Click->bCallWithoutArguments);
		}
		if (TestNotNull(TEXT("the slider route"), Slide))
		{
			TestEqual(TEXT("to the item's UseWithValue"), Slide->ItemFunction, FName(TEXT("UseWithValue")));
			TestFalse(TEXT("forwarding the value"), Slide->bCallWithoutArguments);
		}
	}

	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Fixture.GetClass());
	if (!TestNotNull(TEXT("the widget instantiates"), Instance))
	{
		return false;
	}
	UDreamWidget* Template = Instance->GetWidgetFromName(TEXT("Row"));
	if (!TestNotNull(TEXT("with its template"), Template))
	{
		Instance->DestroyWidget();
		return false;
	}
	TestEqual(TEXT("no view model, no rows"), CopiesOf(Template).Num(), 0);

	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Instance);
	UDreamTestCountingItemVM* A = MakeItem(Player, TEXT("A"));
	UDreamTestCountingItemVM* B = MakeItem(Player, TEXT("B"));
	UDreamTestCountingItemVM* C = MakeItem(Player, TEXT("C"));
	Player->Items = ItemList({ A, B, C });
	// Handed over from code, as a host does: the entry's variable is written and announced, and the loop's path watcher
	// refreshes the list from that -- nobody calls refresh.
	TestTrue(TEXT("the view model is accepted"), Instance->SetViewModel(TEXT("Player"), Player));
	TArray<UDreamWidget*> Rows = CopiesOf(Template);
	if (!TestEqual(TEXT("a row per item, read through the path"), Rows.Num(), 3))
	{
		Instance->DestroyWidget();
		return false;
	}
	TestEqual(TEXT("the first row shows the first item"), TextIn(Rows[0], TEXT("ItemName")), FString(TEXT("A")));
	TestEqual(TEXT("the second the second"), TextIn(Rows[1], TEXT("ItemName")), FString(TEXT("B")));

	// One item renamed: its row alone, with no refresh -- which the quietly changed Untracked of another row proves: a
	// refresh would have shown it.
	A->Untracked = FText::FromString(TEXT("A changed quietly"));
	B->SetName(FText::FromString(TEXT("B renamed")));
	TestEqual(TEXT("the renamed item's row shows it"), TextIn(Rows[1], TEXT("ItemName")), FString(TEXT("B renamed")));
	TestEqual(TEXT("the other rows keep theirs"), TextIn(Rows[2], TEXT("ItemName")), FString(TEXT("C")));
	TestEqual(TEXT("and nothing was refreshed"), TextIn(Rows[0], TEXT("ItemNote")), FString(TEXT("A untracked")));
	Instance->RefreshEachBindings();
	TestEqual(TEXT("until a refresh"), TextIn(Rows[0], TEXT("ItemNote")), FString(TEXT("A changed quietly")));

	// The second row's button: the second item, once.
	UDreamButton* SecondButton = Cast<UDreamButton>(Rows[1]->FindChildByDisplayName(TEXT("UseButton"), true));
	if (TestNotNull(TEXT("the second row has its button"), SecondButton))
	{
		SecondButton->OnClicked.Broadcast();
		TestEqual(TEXT("a click calls its row's item"), B->UseCount, 1);
		TestEqual(TEXT("and no other"), A->UseCount + C->UseCount, 0);
	}
	UDreamSlider* ThirdSlider = Cast<UDreamSlider>(Rows[2]->FindChildByDisplayName(TEXT("AmountSlider"), true));
	if (TestNotNull(TEXT("the third row has its slider"), ThirdSlider))
	{
		ThirdSlider->OnValueChanged.Broadcast(0.25f);
		TestEqual(TEXT("a slide calls its row's item"), C->UseCount, 1);
		TestEqual(TEXT("with the value"), C->LastValue, 0.25f);
	}

	// Reordered on the view model: the rows follow their items, and so do their buttons.
	Player->SetItems(ItemList({ C, A, B }));
	Rows = CopiesOf(Template);
	if (TestEqual(TEXT("still a row per item"), Rows.Num(), 3))
	{
		TestEqual(TEXT("in the new order"), TextIn(Rows[0], TEXT("ItemName")), FString(TEXT("C")));
		if (UDreamButton* FirstButton = Cast<UDreamButton>(Rows[0]->FindChildByDisplayName(TEXT("UseButton"), true)))
		{
			FirstButton->OnClicked.Broadcast();
			TestEqual(TEXT("the first row's button calls the item now first"), C->UseCount, 2);
			TestEqual(TEXT("and not the one that stood there before"), A->UseCount, 0);
		}
	}

	// Replaced: new rows for the new item, and nothing left on the old ones.
	UDreamTestCountingItemVM* D = MakeItem(Player, TEXT("D"));
	Player->SetItems(ItemList({ D }));
	Rows = CopiesOf(Template);
	if (TestEqual(TEXT("a replaced list re-makes the rows"), Rows.Num(), 1))
	{
		TestEqual(TEXT("showing the new item"), TextIn(Rows[0], TEXT("ItemName")), FString(TEXT("D")));
		if (UDreamButton* OnlyButton = Cast<UDreamButton>(Rows[0]->FindChildByDisplayName(TEXT("UseButton"), true)))
		{
			OnlyButton->OnClicked.Broadcast();
			TestEqual(TEXT("whose button calls it"), D->UseCount, 1);
		}
	}
	TestEqual(TEXT("the items that left are watched no more"), A->LiveDelegateCount + B->LiveDelegateCount + C->LiveDelegateCount, 0);

	Instance->DestroyWidget();
	return true;
}

#endif
