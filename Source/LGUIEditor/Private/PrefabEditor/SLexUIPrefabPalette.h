// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "LexUIControlRegistry.h"

class FLexUIPrefabEditor;
class ULexWidget;

/**
 * Drag payload for a Palette element. Carries the same data the Palette uses to create an
 * element (basic ULexWidget + Visual, or a prefab by path); the hierarchy view's drop
 * handler calls CreateUnder(dropTargetWidget) to place it. Lets the Palette drop onto an
 * Outliner row, UMG-designer style, reusing FLexUIEditorTools::CreateWidget / CreateUIControls.
 */
class FLexUIPaletteDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FLexUIPaletteDragDropOp, FDecoratedDragDropOp)

	bool bIsBasicWidget = false;// true: ULexWidget + VisualClass; false: instantiate PrefabPath
	TWeakObjectPtr<UClass> VisualClass;
	bool bSetDefaultSprite = false;
	TSharedPtr<FLexUIControlDescriptor> NativeDescriptor;
	FString PrefabPath;
	/** Project assets stay linked to their source; only the plugin's own recipes are flattened. */
	bool bLinkedSubPrefab = false;
	FString DisplayName;

	/**
	 * Create this element under InParentWidget (no-op if null / incompatible, like the menu).
	 * InSiblingIndex places it between existing children, which is what an above/below drop in the
	 * outliner asks for; unset appends, which is what a drop onto a row or into the viewport means.
	 */
	ULexWidget* CreateUnder(ULexWidget* InParentWidget, TOptional<int32> InSiblingIndex = TOptional<int32>(),
		TFunction<void(ULexWidget*)> AfterCreate = nullptr)const;
};

/**
 * "Palette" tab for the LexUI Prefab Editor, modeled on UMG's Palette (and the LGUI3 palette
 * we built earlier): a categorized list of creatable UI elements -- basic widgets/visuals,
 * built-in controls, and project prefab assets -- created under the selected widget on
 * double-click. LexUI creates elements imperatively (ULexWidget + a Visual, or a prefab
 * instantiated under the selection), so this reuses FLexUIEditorTools::CreateWidget /
 * CreateUIControls exactly like the "Create UI Element" context menu, just from a dockable
 * panel instead of a right-click submenu.
 */
class SLexUIPrefabPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabPalette) {}
	SLATE_END_ARGS()

	virtual ~SLexUIPrefabPalette() override;
	void Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** How a palette item creates its element. */
	enum class EItemKind : uint8
	{
		Category,     // header
		BasicWidget,  // ULexWidget + optional Visual class (VisualClass, may be null)
		Prefab,       // flatten a built-in /LGUI/Prefabs/ recipe by path
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
		TSharedPtr<FLexUIControlDescriptor> NativeDescriptor;
		bool bValid = true;
		FText ValidationError;
		TArray<TSharedPtr<FPaletteItem>> Children;
	};
	typedef TSharedPtr<FPaletteItem> FItemPtr;

	void RebuildList();
	void CollectBasics(TArray<FItemPtr>& Out);
	void CollectControls(TArray<FItemPtr>& Out);
	void CollectPrefabs(TArray<FItemPtr>& Out);

	TSharedRef<class ITableRow> OnGenerateRow(FItemPtr InItem, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren);
	void OnItemDoubleClick(FItemPtr InItem);
	FReply OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem);
	void OnSearchTextChanged(const FText& InText);

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
	ULexWidget* GetSelectedWidget()const;

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TSharedPtr<STreeView<FItemPtr>> TreeView;
	TArray<FItemPtr> RootItems;
	FTextFilterExpressionEvaluator SearchFilter{ ETextFilterExpressionEvaluatorMode::BasicString };
	FDelegateHandle RegistryChangedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle FilesLoadedHandle;
	bool bRebuildRequested = false;
	double NextRebuildTime = 0.0;
};
