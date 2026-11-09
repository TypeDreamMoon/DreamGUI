// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Workflow/DreamUIPresetLibrary.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FDreamWidgetBlueprintEditor;
class ITableRow;
class SSearchBox;
class STableViewBase;

/**
 * The designer's preset library: three shelves -- gradients, styles, effect settings -- of the project's text presets and
 * preset assets. Double-click or Apply puts a preset on the selected widgets; a preset dragged onto a widget in the viewport
 * or the hierarchy, or onto a property row that takes one, is applied there. The right-click menu applies an asset as a copy
 * instead of following it, keeps a preset the other way (text as an asset, an asset as text), and removes a text preset;
 * Save Selection keeps the selected widget's gradient, style or effect settings as a new preset.
 */
class DREAMGUIEDITOR_API SDreamUIPresetLibrary : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPresetLibrary) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InDesigner);
	virtual ~SDreamUIPresetLibrary() override;

	/** The shelf shown. */
	EDreamUIPresetKind GetKind() const { return Kind; }
	void SetKind(EDreamUIPresetKind InKind);
	/** Read the shelf again: the project settings and the asset registry. */
	void Refresh();
	/** What the shelf shows now, after the search. */
	const TArray<TSharedPtr<FDreamUIPreset>>& GetVisiblePresets() const { return Visible; }
	void SetSearchText(const FText& InText);
	/** The preset applied to the designer's selection. How many widgets took it. */
	int32 ApplyToSelection(const FDreamUIPreset& InPreset, bool bInFollowAsset = true);

private:
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FDreamUIPreset> InPreset, const TSharedRef<STableViewBase>& InOwner);
	TSharedPtr<SWidget> MakeContextMenu();
	TSharedRef<SWidget> MakeSaveMenu();
	void OnDoubleClick(TSharedPtr<FDreamUIPreset> InPreset);
	FReply OnApplyClicked();
	bool HasSelection() const;
	void SaveSelection(bool bInAsAsset);
	void Filter();
	void OnAssetsChanged(const FAssetData& InAsset);

	TWeakPtr<FDreamWidgetBlueprintEditor> Designer;
	EDreamUIPresetKind Kind = EDreamUIPresetKind::Style;
	TArray<TSharedPtr<FDreamUIPreset>> All;
	TArray<TSharedPtr<FDreamUIPreset>> Visible;
	FString SearchText;
	TSharedPtr<SListView<TSharedPtr<FDreamUIPreset>>> List;
	TSharedPtr<SSearchBox> SearchBox;
	FText LastError;
	FDelegateHandle ChangedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
};
