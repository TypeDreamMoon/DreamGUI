// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Core/Components/LexLayout.h"
#include "Widget/AnchorPreviewWidget.h"
#pragma once

/**
 * 
 */
class FLexWidgetCustomization : public IDetailCustomization
{
public:
	FLexWidgetCustomization();
	~FLexWidgetCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * Pivot re-anchors the rect without resizing it, so the on-screen offsets are captured before the
	 * property write and replayed after it. The write reaches every selected widget, so the capture must too.
	 */
	static void CaptureAnchorOffsets(const TArray<TWeakObjectPtr<class ULexWidget>>& Widgets, TArray<FMargin>& OutAnchorOffsets);
	static void RestoreAnchorOffsets(const TArray<TWeakObjectPtr<class ULexWidget>>& Widgets, const TArray<FMargin>& AnchorOffsets);
	/**
	 * Pushes one edited anchor number into every selected widget.
	 *
	 * A selection can mix stretched and non-stretched anchors, and the number means an edge offset for the
	 * former and a position/size for the latter, so the reading is taken per widget rather than once for all.
	 */
	static void ApplyAnchorValueToWidgets(const TArray<TWeakObjectPtr<class ULexWidget>>& Widgets, float Value, int AnchorValueIndex);
private:
	TArray<TWeakObjectPtr<class ULexWidget>> TargetScriptArray;
	static TArray<float> ValueRangeArray;

	FText GetAnchorsTooltipText()const;
	
	void ForceUpdateUI();

	bool OnCanCopyAnchor()const;
	bool OnCanPasteAnchor()const;
	void OnCopyAnchor();
	void OnPasteAnchor(IDetailLayoutBuilder* DetailBuilder);
	void OnCopyHierarchyIndex();
	void OnPasteHierarchyIndex(TSharedRef<IPropertyHandle> PropertyHandle);
	FReply OnClickIncreaseOrDecreaseSiblingIndex(bool IncreaseOrDecrease, TSharedRef<IPropertyHandle> HierarchyIndexHandle);
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
	void OnSelectAnchor(LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign HorizontalAlign, LGUIAnchorPreviewWidget::UIAnchorVerticalAlign VerticalAlign, IDetailLayoutBuilder* DetailBuilder);
	LGUIAnchorPreviewWidget::UIAnchorHorizontalAlign GetAnchorHAlign(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;
	LGUIAnchorPreviewWidget::UIAnchorVerticalAlign GetAnchorVAlign(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;
	FText GetHAlignText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;
	FText GetVAlignText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const;

	FLexLayoutControlAnchorData GetLayoutControlAnchorValue()const;
	enum class EAnchorControlledByLayoutType
	{
		HorizontalAnchor,
		HorizontalAnchoredPosition,
		HorizontalSizeDelta,
		VerticalAnchor,
		VerticalAnchoredPosition,
		VerticalSizeDelta,
	};
	bool IsAnchorControlledByMultipleLayout(TMap<EAnchorControlledByLayoutType, TArray<UObject*>>& Result)const;
	bool GetLayoutControlHorizontalAnchoredPosition()const;
	bool GetLayoutControlVerticalAnchoredPosition()const;
	bool GetLayoutControlHorizontalSizeDelta()const;
	bool GetLayoutControlVerticalSizeDelta()const;
};
