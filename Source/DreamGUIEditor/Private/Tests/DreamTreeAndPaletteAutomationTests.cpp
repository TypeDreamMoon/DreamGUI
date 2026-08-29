// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Designer/DreamWidgetEditorHierarchyView.h"
#include "Designer/DreamWidgetEditorHierarchyViewItem.h"
#include "Designer/SDreamWidgetPalette.h"
#include "DreamUIControlRegistry.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/DreamContentWidget.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// Both trees hid the same thing in different ways.
//
// Everything in the hierarchy is a UDreamWidget -- what makes one a text block and another a vertical
// box hangs off it as a Visual, a layout container or a behaviour -- so a tree keyed on the display
// name alone showed no type and could not search for one either. In the palette, a locked row also
// refused Above/Below drops although those never enter the locked widget, and the favourites,
// group expansion and the search all had to be derived from the collected list rather than by
// re-running the collectors per keystroke.
//
// COVERAGE BOUNDARY: what follows pins the decisions, which is what these fixes moved out of Slate
// on purpose. It does NOT reach the rows: that the palette row binds its check box, that the
// hierarchy row prints the label and highlights the match, and that the collectors are only run
// from RebuildList, all need a live designer and generated rows.
namespace DreamTreeAndPaletteTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UObject* Outer, const TCHAR* DisplayName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(50.0f);
		return Widget;
	}

	DreamUIPalette::FItemPtr MakeGroup(const TCHAR* DisplayName)
	{
		DreamUIPalette::FItemPtr Group = MakeShared<DreamUIPalette::FPaletteItem>();
		Group->Kind = DreamUIPalette::EItemKind::Category;
		Group->DisplayName = DisplayName;
		return Group;
	}

	DreamUIPalette::FItemPtr AddEntry(const DreamUIPalette::FItemPtr& Group, DreamUIPalette::EItemKind Kind, const TCHAR* DisplayName)
	{
		DreamUIPalette::FItemPtr Item = MakeShared<DreamUIPalette::FPaletteItem>();
		Item->Kind = Kind;
		Item->DisplayName = DisplayName;
		Item->FavoriteKey = DreamUIPalette::MakeFavoriteKey(*Item);
		Group->Children.Add(Item);
		return Item;
	}

	/** The palette's own filter is a substring match over the display name. */
	bool MatchesText(const DreamUIPalette::FPaletteItem& Item)
	{
		return Item.DisplayName.Contains(TEXT("Text"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyTypeIsSearchableTest,
	"DreamGUI.Editor.HierarchySearch.WidgetTypeIsSearchableAndLabelled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyTypeIsSearchableTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeAndPaletteTestLocal;
	FScopedTestWorld TestWorld;

	// A plain widget is the case that must stay quiet: no type of its own, so nothing to print after
	// the name and nothing to match beyond it.
	TStrongObjectPtr<UDreamWidget> Plain(MakeWidget(TestWorld.World, TEXT("Plain")));
	TArray<FString> PlainTerms;
	DreamWidgetHierarchyType::CollectSearchTerms(Plain.Get(), PlainTerms);
	TestEqual(TEXT("a plain widget offers only its name to the search"), PlainTerms.Num(), 1);
	TestEqual(TEXT("and that name is its display name"), PlainTerms[0], FString(TEXT("Plain")));
	TestTrue(TEXT("a plain widget prints no type label"), DreamWidgetHierarchyType::GetTypeLabel(Plain.Get()).IsEmpty());

	// The visual is what "Text"/"Image" means to whoever is searching.
	TStrongObjectPtr<UDreamWidget> Pictured(MakeWidget(TestWorld.World, TEXT("Banner")));
	Pictured->CreateNewVisual<UDreamImage>();
	TArray<FString> PicturedTerms;
	DreamWidgetHierarchyType::CollectSearchTerms(Pictured.Get(), PicturedTerms);
	TestTrue(TEXT("the visual's class is searchable"), PicturedTerms.Contains(TEXT("DreamImage")));
	TestTrue(TEXT("a widget with a visual is labelled by it"), DreamWidgetHierarchyType::GetTypeLabel(Pictured.Get()).Contains(TEXT("Image")));

	// The layout container is what "vertical box" means, and it is the label when nothing is drawn.
	TStrongObjectPtr<UDreamWidget> Column(MakeWidget(TestWorld.World, TEXT("Column")));
	Column->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	TArray<FString> ColumnTerms;
	DreamWidgetHierarchyType::CollectSearchTerms(Column.Get(), ColumnTerms);
	TestTrue(TEXT("the layout container's class is searchable"), ColumnTerms.Contains(TEXT("DreamLayoutContainerVerticalBox")));
	TestTrue(TEXT("a layout-only widget is labelled by its container"), DreamWidgetHierarchyType::GetTypeLabel(Column.Get()).Contains(TEXT("Vertical")));

	// Behaviours carry the interactive types, which is most of what a control IS.
	TStrongObjectPtr<UDreamWidget> Behaving(MakeWidget(TestWorld.World, TEXT("Slot")));
	Behaving->AddComponent<UDreamContentWidget>();
	TArray<FString> BehavingTerms;
	DreamWidgetHierarchyType::CollectSearchTerms(Behaving.Get(), BehavingTerms);
	TestTrue(TEXT("a behaviour's class is searchable"), BehavingTerms.Contains(TEXT("DreamContentWidget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHierarchyDropLockOwnerTest,
	"DreamGUI.Editor.HierarchyDrop.SiblingDropAsksTheParentsLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHierarchyDropLockOwnerTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeAndPaletteTestLocal;
	using namespace DreamWidgetHierarchyDrop;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, TEXT("Root")));
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* Child = MakeWidget(Root.Get(), TEXT("Child"));
	Child->TrySetParent(Root.Get(), false);

	// Onto the row means inside it, so the row's own lock decides.
	TestTrue(TEXT("a drop onto a widget is governed by that widget"), GetLockOwnerForDropZone(Child, EItemDropZone::OntoItem) == Child);
	// Above/Below only rewrite the parent's child list, and asking the hovered row instead is what
	// made a locked widget un-neighbourable.
	TestTrue(TEXT("a drop above a widget is governed by its parent"), GetLockOwnerForDropZone(Child, EItemDropZone::AboveItem) == Root.Get());
	TestTrue(TEXT("a drop below a widget is governed by its parent"), GetLockOwnerForDropZone(Child, EItemDropZone::BelowItem) == Root.Get());
	// A root has no sibling list, so an above/below drop on it is rewritten into a drop INSIDE it --
	// answering "nobody" here let a drag land inside a locked root by hovering its edge.
	TestTrue(TEXT("a drop above a root widget is governed by the root itself"), GetLockOwnerForDropZone(Root.Get(), EItemDropZone::AboveItem) == Root.Get());
	TestTrue(TEXT("a drop below a root widget is governed by the root itself"), GetLockOwnerForDropZone(Root.Get(), EItemDropZone::BelowItem) == Root.Get());
	TestNull(TEXT("a drop with no target has no lock owner"), GetLockOwnerForDropZone(nullptr, EItemDropZone::OntoItem));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaletteFavoritesTest,
	"DreamGUI.Editor.Palette.FavoritesLeadTheTreeAndKeepTheirOwnKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaletteFavoritesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTreeAndPaletteTestLocal;
	using namespace DreamUIPalette;

	// A basic named "Text" and a control named "Text" are two different entries; one star must not
	// light the other.
	FPaletteItem BasicText;
	BasicText.Kind = EItemKind::BasicWidget;
	BasicText.DisplayName = TEXT("Text");
	FPaletteItem ControlText;
	ControlText.Kind = EItemKind::Native;
	ControlText.DisplayName = TEXT("Text");
	TestNotEqual(TEXT("two kinds sharing a display name get different keys"), MakeFavoriteKey(BasicText), MakeFavoriteKey(ControlText));

	// A registry entry is keyed by its registry Name, so relabelling the control keeps the star.
	FPaletteItem Labelled;
	Labelled.Kind = EItemKind::Native;
	Labelled.DisplayName = TEXT("Push Button");
	Labelled.NativeDescriptor = MakeShared<FDreamUIControlDescriptor>();
	Labelled.NativeDescriptor->Name = TEXT("Button");
	const FString LabelledKey = MakeFavoriteKey(Labelled);
	Labelled.DisplayName = TEXT("Renamed Later");
	TestEqual(TEXT("a registry entry's key follows its Name, not its label"), MakeFavoriteKey(Labelled), LabelledKey);

	auto MatchesFilter = [](const FPaletteItem& Item) { return MatchesText(Item); };

	TArray<FItemPtr> Groups;
	FItemPtr Basic = MakeGroup(TEXT("Basic"));
	FItemPtr Image = AddEntry(Basic, EItemKind::BasicWidget, TEXT("Image"));
	AddEntry(Basic, EItemKind::BasicWidget, TEXT("Text"));
	Groups.Add(Basic);
	FItemPtr Panels = MakeGroup(TEXT("Panels"));
	AddEntry(Panels, EItemKind::Native, TEXT("Overlay"));
	Groups.Add(Panels);

	TArray<FItemPtr> RootItems;
	BuildRootItems(Groups, TSet<FString>(), false, MatchesFilter, RootItems);
	TestEqual(TEXT("with no favourites the tree is just the groups"), RootItems.Num(), 2);
	TestEqual(TEXT("and the first group is the first collected one"), RootItems[0]->DisplayName, FString(TEXT("Basic")));

	TSet<FString> Favorites;
	Favorites.Add(Image->FavoriteKey);
	BuildRootItems(Groups, Favorites, false, MatchesFilter, RootItems);
	TestEqual(TEXT("a favourite adds a group at the head of the tree"), RootItems.Num(), 3);
	TestEqual(TEXT("and that group is the favourites"), RootItems[0]->DisplayName, FString(FavoritesGroupName));
	TestEqual(TEXT("carrying just the starred entry"), RootItems[0]->Children.Num(), 1);
	TestEqual(TEXT("which keeps its key, so unstarring it from there works"), RootItems[0]->Children[0]->FavoriteKey, Image->FavoriteKey);
	// A tree view keys its rows by item: the same entry twice would be one row fighting two parents.
	TestTrue(TEXT("a favourite is a copy, not the same item under two groups"), RootItems[0]->Children[0] != Image);
	TestTrue(TEXT("and the entry is still in its home group"), RootItems[1]->Children.Contains(Image));

	// A search that leaves a favourite behind must drop it from the favourites group as well, or the
	// panel answers a search with entries that do not match it.
	BuildRootItems(Groups, Favorites, true, MatchesFilter, RootItems);
	TestEqual(TEXT("filtering keeps only the group that has a match"), RootItems.Num(), 1);
	TestEqual(TEXT("and it is not the favourites, whose only entry was filtered out"), RootItems[0]->DisplayName, FString(TEXT("Basic")));
	TestEqual(TEXT("with only the matching entry left in it"), RootItems[0]->Children.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaletteGroupExpansionTest,
	"DreamGUI.Editor.Palette.CollapsedGroupsSurviveARebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaletteGroupExpansionTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPalette;

	// Search rebuilds the tree, so every keystroke re-applied expansion; forcing it open unconditionally
	// is what reopened every group the user had closed, on every character typed.
	TestFalse(TEXT("a group the user collapsed stays collapsed through a rebuild"), ShouldExpandGroup(false, true));
	TestTrue(TEXT("a group the user never touched is open"), ShouldExpandGroup(false, false));
	// While searching, a collapsed group would hide its own hits and read as "no results".
	TestTrue(TEXT("a filter opens even a collapsed group"), ShouldExpandGroup(true, true));
	TestTrue(TEXT("a filter leaves an open group open"), ShouldExpandGroup(true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlRegistryRefusalTest,
	"DreamGUI.Editor.ControlRegistry.ARefusedRegistrationSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlRegistryRefusalTest::RunTest(const FString& Parameters)
{
	// Both refusals used to be a discarded bool: an extension registering a name the defaults already
	// hold simply never appeared in the palette, with nothing anywhere to read.
	AddExpectedMessagePlain(TEXT("that name is already registered by"), ELogVerbosity::Warning);
	AddExpectedMessagePlain(TEXT("Refused a control descriptor with no Name"), ELogVerbosity::Warning);

	FDreamUIControlRegistry& Registry = FDreamUIControlRegistry::Get();
	FDreamUIControlDescriptor Descriptor;
	Descriptor.Name = TEXT("DreamGUIAutomationTest.Control");
	Descriptor.DisplayName = FText::FromString(TEXT("Automation Test Control"));
	Descriptor.Category = TEXT("Automation");
	Descriptor.VisualClass = UDreamImage::StaticClass();

	TestTrue(TEXT("a fresh name registers"), Registry.Register(Descriptor));
	TestFalse(TEXT("the same name a second time is refused"), Registry.Register(Descriptor));

	FDreamUIControlDescriptor Nameless = Descriptor;
	Nameless.Name = NAME_None;
	TestFalse(TEXT("a descriptor with no name is refused"), Registry.Register(Nameless));

	TestTrue(TEXT("and the test's own entry unregisters"), Registry.Unregister(Descriptor.Name));
	return true;
}

#endif
