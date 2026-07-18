// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class FLexUIPrefabEditor;
class ULexWidget;

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

	void Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor);

private:
	/** How a palette item creates its element. */
	enum class EItemKind : uint8
	{
		Category,   // header
		BasicWidget,// ULexWidget + optional Visual class (VisualClass, may be null)
		Prefab,     // instantiate a prefab by path (built-in control OR project asset)
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
	void OnSearchTextChanged(const FText& InText);

	/** Create InItem's element under the editor's selected widget (no-op if nothing selected). */
	void CreateItem(FItemPtr InItem);
	/** The editor's first selected widget, or null -- the parent for created elements. */
	ULexWidget* GetSelectedWidget()const;

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TSharedPtr<STreeView<FItemPtr>> TreeView;
	TArray<FItemPtr> RootItems;
	FTextFilterExpressionEvaluator SearchFilter{ ETextFilterExpressionEvaluatorMode::BasicString };
};
