// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Styling/SlateTypes.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "DreamUIControlRegistry.h"

class FDreamUIPrefabEditor;
class UDreamWidget;

/**
 * The Palette's data and the decisions taken over it, kept out of the Slate widget so they can be
 * exercised without one. What is left in SDreamUIPrefabPalette below is the wiring: collectors that
 * need the asset registry, and rows.
 */
namespace DreamUIPalette
{
	/** How a palette item creates its element. */
	enum class EItemKind : uint8
	{
		Category,     // header
		BasicWidget,  // UDreamWidget + optional Visual class (VisualClass, may be null)
		Prefab,       // flatten a built-in /DreamGUI/Prefabs/ recipe by path
		ProjectPrefab,// instantiate a project asset and keep it linked as a sub-prefab
		Native,       // registry recipe (layout/behaviour/optional factory)
	};

	struct FPaletteItem
	{
		EItemKind Kind = EItemKind::Category;
		FString DisplayName;
		// BasicWidget:
		TWeakObjectPtr<UClass> VisualClass;
		bool bSetDefaultSprite = false;// Image element gets a default sprite, like the menu
		// Prefab:
		FString PrefabPath;
		FAssetData PrefabAsset;// valid only for project prefabs (thumbnail/browse)
		TSharedPtr<FDreamUIControlDescriptor> NativeDescriptor;
		bool bValid = true;
		FText ValidationError;
		/** Identity in the favourites list; see MakeFavoriteKey. Empty on category headers. */
		FString FavoriteKey;
		TArray<TSharedPtr<FPaletteItem>> Children;
	};
	typedef TSharedPtr<FPaletteItem> FItemPtr;

	/** The synthesized group the favourites are shown in, at the head of the tree. */
	extern const TCHAR* FavoritesGroupName;

	/**
	 * How an entry is named in the saved favourites list. Display names are not identities -- two
	 * registry entries in different categories may share one, and a project prefab's name repeats
	 * across folders -- so the key is the registry Name or the package path, tagged with the kind so
	 * a basic "Text" and a control named "Text" cannot claim each other's star.
	 */
	FString MakeFavoriteKey(const FPaletteItem& Item);

	/**
	 * Whether a group is shown expanded after a rebuild.
	 *
	 * Search rebuilds the whole tree, so expansion has to be re-applied from something that survives;
	 * what survives is the set of groups the user collapsed. While a filter is active every surviving
	 * group is force-expanded, since a search that hides its own results reads as no results at all.
	 */
	bool ShouldExpandGroup(bool bFilterActive, bool bWasCollapsed);

	/**
	 * Derive the tree from the collected groups: the favourites first, then each group with only the
	 * entries the filter admits, and no empty groups. The favourites are copies rather than the same
	 * items again, because a tree view keys its rows by item and would otherwise see one entry twice.
	 */
	void BuildRootItems(const TArray<FItemPtr>& InAllGroups, const TSet<FString>& InFavorites,
		bool bFilterActive, TFunctionRef<bool(const FPaletteItem&)> InMatchesFilter, TArray<FItemPtr>& OutRootItems);
}

/**
 * Drag payload for a Palette element. Carries the same data the Palette uses to create an
 * element (basic UDreamWidget + Visual, or a prefab by path); the hierarchy view's drop
 * handler calls CreateUnder(dropTargetWidget) to place it. Lets the Palette drop onto an
 * Outliner row, UMG-designer style, reusing FDreamUIEditorTools::CreateWidget / CreateUIControls.
 */
class FDreamUIPaletteDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDreamUIPaletteDragDropOp, FDecoratedDragDropOp)

	bool bIsBasicWidget = false;// true: UDreamWidget + VisualClass; false: instantiate PrefabPath
	TWeakObjectPtr<UClass> VisualClass;
	bool bSetDefaultSprite = false;
	TSharedPtr<FDreamUIControlDescriptor> NativeDescriptor;
	FString PrefabPath;
	/** Project assets stay linked to their source; only the plugin's own recipes are flattened. */
	bool bLinkedSubPrefab = false;
	FString DisplayName;

	/**
	 * Create this element under InParentWidget (no-op if null / incompatible, like the menu).
	 * InSiblingIndex places it between existing children, which is what an above/below drop in the
	 * outliner asks for; unset appends, which is what a drop onto a row or into the viewport means.
	 */
	UDreamWidget* CreateUnder(UDreamWidget* InParentWidget, TOptional<int32> InSiblingIndex = TOptional<int32>(),
		TFunction<void(UDreamWidget*)> AfterCreate = nullptr)const;
};

/**
 * "Palette" tab for the DreamUI Prefab Editor, modeled on UMG's Palette (and the DreamGUI3 palette
 * we built earlier): a categorized list of creatable UI elements -- basic widgets/visuals,
 * built-in controls, and project prefab assets -- created under the selected widget on
 * double-click. DreamUI creates elements imperatively (UDreamWidget + a Visual, or a prefab
 * instantiated under the selection), so this reuses FDreamUIEditorTools::CreateWidget /
 * CreateUIControls exactly like the "Create UI Element" context menu, just from a dockable
 * panel instead of a right-click submenu.
 */
class SDreamUIPrefabPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPrefabPalette) {}
	SLATE_END_ARGS()

	virtual ~SDreamUIPrefabPalette() override;
	void Construct(const FArguments& InArgs, TSharedPtr<FDreamUIPrefabEditor> InPrefabEditor);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	using EItemKind = DreamUIPalette::EItemKind;
	using FPaletteItem = DreamUIPalette::FPaletteItem;
	using FItemPtr = DreamUIPalette::FItemPtr;

	/** Re-run the collectors, then re-derive what is shown. Only source changes need this. */
	void RebuildList();
	/**
	 * Re-derive what is shown from the already-collected groups.
	 *
	 * Typing in the search box used to re-run all three collectors per keystroke, project prefab scan
	 * and a dozen package-existence probes included; nothing they answer can change between two
	 * keystrokes, and what can (a registry or asset change) has its own path into RebuildList.
	 */
	void RefreshRootItems();
	void CollectBasics(TArray<FItemPtr>& Out);
	void CollectControls(TArray<FItemPtr>& Out);
	void CollectPrefabs(TArray<FItemPtr>& Out);

	TSharedRef<class ITableRow> OnGenerateRow(FItemPtr InItem, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren);
	void OnItemDoubleClick(FItemPtr InItem);
	FReply OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem);
	void OnSearchTextChanged(const FText& InText);
	/** What the rows highlight: the live search text. */
	FText GetSearchText()const;

	/** Favourites and collapsed groups, per user and project (GEditorPerProjectIni). */
	void LoadPreferences();
	void SavePreferences()const;
	ECheckBoxState GetFavoriteState(FItemPtr InItem)const;
	void OnFavoriteToggled(ECheckBoxState InState, FItemPtr InItem);
	/** Push expansion onto the tree after a rebuild; see DreamUIPalette::ShouldExpandGroup. */
	void ApplyGroupExpansion();
	void OnGroupExpansionChanged(FItemPtr InItem, bool bExpanded);

	/**
	 * Coalesce asset-registry churn into one rebuild.
	 *
	 * The list used to be built once per editor instance, so a prefab created after the panel opened
	 * never showed up and a deleted one stayed until it was clicked. A cold registry was worse: every
	 * prefab-backed control failed validation and stayed greyed out for the whole session, because
	 * nothing ever asked again.
	 */
	void RequestRebuild();
	void HandlePrefabAssetChanged(const FAssetData& InAssetData);
	void HandlePrefabAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath);
	void HandleAssetRegistryFilesLoaded();

	/** Create InItem's element under the editor's selected widget (no-op if nothing selected). */
	void CreateItem(FItemPtr InItem);
	/** The editor's first selected widget, or null -- the parent for created elements. */
	UDreamWidget* GetSelectedWidget()const;

	TWeakPtr<FDreamUIPrefabEditor> PrefabEditorPtr;
	TSharedPtr<STreeView<FItemPtr>> TreeView;
	/** Everything the collectors found, filter-independent; RootItems is derived from it. */
	TArray<FItemPtr> AllGroups;
	TArray<FItemPtr> RootItems;
	TSet<FString> Favorites;
	/** Groups the user closed. Collapsed rather than expanded, so a group added later starts open. */
	TSet<FString> CollapsedGroups;
	/** Set while the rebuild pushes expansion, so the tree's callback does not read it back as intent. */
	bool bApplyingGroupExpansion = false;
	FTextFilterExpressionEvaluator SearchFilter{ ETextFilterExpressionEvaluatorMode::BasicString };
	FDelegateHandle RegistryChangedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle FilesLoadedHandle;
	bool bRebuildRequested = false;
	double NextRebuildTime = 0.0;
};
