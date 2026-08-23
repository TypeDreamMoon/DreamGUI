// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Core/Components/DreamLayout.h"
#include "Widget/AnchorPreviewWidget.h"
#pragma once

/**
 * 
 */
class FDreamWidgetCustomization : public IDetailCustomization
{
public:
	FDreamWidgetCustomization();
	~FDreamWidgetCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * Pivot re-anchors the rect without resizing it, so the on-screen offsets are captured before the
	 * property write and replayed after it. The write reaches every selected widget, so the capture must too.
	 */
	static void CaptureAnchorOffsets(const TArray<TWeakObjectPtr<class UDreamWidget>>& Widgets, TArray<FMargin>& OutAnchorOffsets);
	static void RestoreAnchorOffsets(const TArray<TWeakObjectPtr<class UDreamWidget>>& Widgets, const TArray<FMargin>& AnchorOffsets);
	/**
	 * Pushes one edited anchor number into every selected widget.
	 *
	 * A selection can mix stretched and non-stretched anchors, and the number means an edge offset for the
	 * former and a position/size for the latter, so the reading is taken per widget rather than once for all.
	 */
	static void ApplyAnchorValueToWidgets(const TArray<TWeakObjectPtr<class UDreamWidget>>& Widgets, float Value, int AnchorValueIndex);
	/**
	 * The anchors of this widget were just rewritten by hand, so the slot's authored rect has to follow.
	 * Only on the axes the widget still owns: on a layout-owned axis the widget currently holds arranged
	 * output, and recording that as authored intent loses the authored value with nothing left to restore.
	 */
	static void SyncPanelSlotAfterAnchorEdit(class UDreamWidget* Widget);
	/**
	 * Every write helper reaches the whole selection, so a control flag holds for the selection as soon as
	 * it holds for any member - otherwise a layout-owned widget is edited through a row that was enabled
	 * on some other widget's behalf.
	 */
	static FDreamLayoutControlAnchorData FoldLayoutControlAcrossSelection(const TArray<TWeakObjectPtr<class UDreamWidget>>& Widgets);
	/** Every layout arranging any selected widget, in selection order and without repeats. */
	static TArray<FString> CollectArrangerNames(const TArray<TWeakObjectPtr<class UDreamWidget>>& Widgets);
	/** Anchors mean nothing on a screen-space root canvas, and one such widget in the selection is enough. */
	static bool IsAnchorEditableForSelection(const TArray<TWeakObjectPtr<class UDreamWidget>>& Widgets);
private:
	TArray<TWeakObjectPtr<class UDreamWidget>> TargetScriptArray;
	static TArray<float> ValueRangeArray;

	FText GetAnchorsTooltipText()const;
	
	void ForceUpdateUI();

	bool OnCanCopyAnchor()const;
	bool OnCanPasteAnchor()const;
	void OnCopyAnchor();
	void OnPasteAnchor(IDetailLayoutBuilder* DetailBuilder);
	EVisibility GetAnchorPresetButtonVisibility()const;

	/** One entry per entry of TargetScriptArray, filled by OnPrePivotChange. */
	TArray<FMargin> AnchorOffsetArray;
	void OnPrePivotChange(TSharedPtr<IPropertyHandle> PivotPH);
	void OnPivotChanged(TSharedPtr<IPropertyHandle> PivotPH);

	TOptional<float> GetAnchorValue(TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)const;
	TOptional<float> GetMinMaxSliderValue(TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex, bool MinOrMax)const;
	void ApplyValueChanged(float Value, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex, bool Commited);
	void OnAnchorValueChanged(float Value, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex);
	void OnAnchorValueCommitted(float Value, ETextCommit::Type commitType, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex);
	void OnAnchorValueSliderMovementBegin();
	void OnAnchorValueSliderMovementEnd(float Value, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex);
	bool IsAnchorValueEnable(TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)const;
	bool IsAnchorEditable()const;
	/** A fitter or the parent panel currently owns at least one geometry axis of the primary selection. */
	bool IsAnyGeometryAxisArranged()const;
	/** Anchors belong to the position domain: editable only while no layout owns a position axis. */
	bool AreAnchorsFreeToEdit()const;
	bool IsAnchoredPositionRowEnabled()const;
	bool IsSizeDeltaRowEnabled()const;
	FText GetArrangedByBannerText()const;
	EVisibility GetArrangedByBannerVisibility()const;
	TSharedPtr<IPropertyHandle> GetAnchorPropertyHandle(IDetailLayoutBuilder* DetailBuilder, TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, int Index)const;
	/** A mixed selection leaves the handle's out-param untouched, so the rows read the primary selection instead of stack garbage. */
	void GetAnchorMinMaxForDisplay(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, FVector2D& OutAnchorMin, FVector2D& OutAnchorMax)const;
	FText GetAnchorLabelText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, int LabelIndex)const;
	FText GetAnchorLabelTooltipText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, int LabelTooltipIndex)const;
	void OnSelectAnchor(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign HorizontalAlign, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign VerticalAlign, IDetailLayoutBuilder* DetailBuilder);
	DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign GetAnchorHAlign(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;
	DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign GetAnchorVAlign(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;
	FText GetHAlignText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;
	FText GetVAlignText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;

	FDreamLayoutControlAnchorData GetLayoutControlAnchorValue()const;
	bool GetLayoutControlHorizontalAnchoredPosition()const;
	bool GetLayoutControlVerticalAnchoredPosition()const;
	bool GetLayoutControlHorizontalSizeDelta()const;
	bool GetLayoutControlVerticalSizeDelta()const;

	/** Prefab root only: the design canvas rows, backed by the owning prefab editor. */
	void AddCanvasSizeRowsForPrefabRoot(IDetailLayoutBuilder& DetailBuilder);
	TOptional<int32> GetDesignScreenSizeAxis(int32 AxisIndex) const;
	void OnDesignScreenSizeAxisCommitted(int32 NewValue, ETextCommit::Type CommitType, int32 AxisIndex);
	FText GetCanvasSizeText() const;
	TWeakPtr<class FDreamUIPrefabEditor> PrefabEditorForCanvasSize;
};
