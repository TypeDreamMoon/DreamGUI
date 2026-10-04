// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextPaint.h"
#include "Curves/CurveOwnerInterface.h"
#include "Curves/RichCurve.h"
#include "IPropertyTypeCustomization.h"
#include "Templates/Function.h"

class IPropertyHandle;

/**
 * A gradient's stops as the four curves the engine's gradient editor (SColorGradientEditor) edits: the red, green and blue
 * of each stop's colour, in linear light as that editor and its colour picker expect, and each stop's alpha.
 *
 * The editor draws colour marks and alpha marks apart and lets either move on its own; a stop is both at one place. So
 * every stop owns one colour key (in all three colour curves) and one alpha key, and they stay together: moving either
 * mark moves the stop, deleting either deletes it, and a mark the editor adds gets the other half -- with the colour or
 * the alpha the gradient already has there, in its own colour space, so adding a stop changes nothing until it is edited.
 * The editor's strip is drawn from the stops as FDreamGradient::Evaluate paints them, not from the curves' straight lines
 * in linear light, so it shows what the text will.
 *
 * No drift: a stop whose keys nothing touched is written back with the position and colour it was read with, not with its
 * keys' values converted back. Two stops at one place (a hard edge) sit MinKeySpacing apart on the curves -- the editor
 * finds a mark's other channels by its time -- and keep the place they share.
 */
class DREAMGUIEDITOR_API FDreamGradientStopCurves : public FCurveOwnerInterface
{
public:
	/** Where the stops come from and where an edit goes. */
	struct FBinding
	{
		/** The gradient to show; false when there is none any more (its row went away), and the curves keep what they had. */
		TFunction<bool(FDreamGradient& OutGradient)> Read;
		/** Stops the editor changed, for every gradient being edited; bInInteractive while a stop is being dragged. */
		TFunction<void(const TArray<FDreamGradientStop>& InStops, bool bInInteractive)> Write;
		/** The objects the gradient lives in. */
		TFunction<TArray<const UObject*>()> GetOwners;
	};

	/** How far apart, on the curves, stops that share a place are put. */
	static constexpr float MinKeySpacing = 1.0e-3f;

	explicit FDreamGradientStopCurves(FBinding InBinding);

	/** Read the gradient again now, rather than at the editor's next look. */
	void Refresh();
	/** Where the editor's view of the stops begins and ends: 0 to 1, wider to take in a stop outside. Kept still while a stop is dragged. */
	float GetViewMinInput() const { return ViewMinInput; }
	float GetViewMaxInput() const { return ViewMaxInput; }

	//~ Begin FCurveOwnerInterface
	UE_DEPRECATED(5.6, "Use version taking a TAdderReserverRef")
	virtual TArray<FRichCurveEditInfoConst> GetCurves() const override;
	virtual void GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> OutCurves) const override;
	virtual TArray<FRichCurveEditInfo> GetCurves() override;
	virtual void ModifyOwner() override;
	virtual TArray<const UObject*> GetOwners() const override;
	virtual void MakeTransactional() override;
	virtual void OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos) override;
	virtual void SetOnCurveChangedIsInteractive(bool bInIsInteractive) override;
	virtual bool IsLinearColorCurve() const override { return true; }
	virtual FLinearColor GetLinearColorValue(float InTime) const override;
	virtual bool HasAnyAlphaKeys() const override;
	virtual bool IsValidCurve(FRichCurveEditInfo CurveInfo) override;
	//~ End FCurveOwnerInterface

private:
	/** One stop's keys, and what it is written back as. */
	struct FStopKeys
	{
		FKeyHandle Red = FKeyHandle::Invalid();
		FKeyHandle Green = FKeyHandle::Invalid();
		FKeyHandle Blue = FKeyHandle::Invalid();
		FKeyHandle Alpha = FKeyHandle::Invalid();
		/** The stop's position. Its keys may sit a little after it (MinKeySpacing). */
		float Position = 0.0f;
		/** Where its keys were left: a key found elsewhere is one the editor moved. */
		float KeyTime = 0.0f;
		/** The stop's colour, and the key values that stand for it: keys still holding them leave the colour as it was. */
		FColor Color = FColor::White;
		FLinearColor KeyColor = FLinearColor::White;
	};

	void SyncFromSource() const;
	void Rebuild(const FDreamGradient& InSource) const;
	/** After an edit: stops whose half went, halves the editor added, halves it moved. */
	void Reconcile();
	/** Keys of stops that share a place (or nearly) moved apart again; their positions stay. */
	void SeparateCoincidentKeys();
	void CollectStops(TArray<FDreamGradientStop>& OutStops) const;
	/** Whether a stop holds InHandle as its key in channel InChannel (0 red, 1 green, 2 blue, 3 alpha). */
	bool IsHeld(FKeyHandle InHandle, int32 InChannel) const;
	/** A key of channel InChannel at exactly InTime that no stop holds: the other channels of a mark the editor added. */
	FKeyHandle FindLooseKey(int32 InChannel, float InTime) const;
	void UpdateViewRange() const;

	FBinding Binding;

	// Mutable: the editor asks its questions through const functions, and every answer starts by looking at the source.
	mutable FRichCurve Curves[4];
	mutable TArray<FStopKeys> Stops;
	/** The stops the source held when last read or written: a source holding others was changed elsewhere (undo, a paste). */
	mutable TArray<FDreamGradientStop> SyncedStops;
	/** The stops as a left-to-right gradient in the source's colour space: what the editor's strip and new marks are coloured from. */
	mutable FDreamGradient Strip;
	mutable float ViewMinInput = 0.0f;
	mutable float ViewMaxInput = 1.0f;
	mutable bool bSynced = false;
	bool bNextChangeInteractive = false;
};

/**
 * The details row of an FDreamGradient: a preview strip in the header, drawn by FDreamGradient::Evaluate in the gradient's
 * own colour space, beside a preset picker (the project's UDreamGradientAsset assets and UDreamGUISettings::GradientPresets,
 * and "save as preset"); under it the gradient as CSS to copy and paste, its stops in the engine's gradient editor, and the
 * fields its type uses.
 *
 * Every edit goes through the property system: one undo step, every selected object written, and the details view's
 * notify hook told -- which is what carries a designer preview's edit onto its template.
 */
class FDreamGradientCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

/** What the gradient row does, reachable without a details panel. */
namespace DreamGradientDetails
{
	/** Row tags (FDetailWidgetRow::RowTag) of the rows the customization adds besides the gradient's own fields. */
	inline const FName CssRowTag(TEXT("DreamGradientCss"));
	inline const FName StopsRowTag(TEXT("DreamGradientStops"));

	/** Whether a gradient of type InType and radial size InSize uses its field InField (an FDreamGradient member name): the rows the details show. */
	DREAMGUIEDITOR_API bool IsFieldUsed(FName InField, EDreamPaintType InType, EDreamPaintRadialSize InSize);

	/**
	 * Change every gradient InGradientHandle edits with InEdit, as one undoable step named InDescription: the objects are
	 * told before and after (PreEditChange / PostEditChange, and the details view's notify hook), written directly so no
	 * float goes through text. Nothing happens, and false comes back, when no gradient changes.
	 */
	DREAMGUIEDITOR_API bool EditGradients(const TSharedRef<IPropertyHandle>& InGradientHandle, const FText& InDescription, TFunctionRef<void(FDreamGradient&)> InEdit);

	/** Read InCss (FDreamGradient::ParseCss) into every gradient InGradientHandle edits. False, with OutError and nothing written, when it does not read. */
	DREAMGUIEDITOR_API bool ApplyCss(const TSharedRef<IPropertyHandle>& InGradientHandle, const FString& InCss, FText& OutError);

	/** Whether InName can name a project preset: a `<gradient=Name>` tag reads its name up to a space or a bracket. */
	DREAMGUIEDITOR_API bool IsValidPresetName(const FString& InName, FText* OutError = nullptr);

	/** Add or replace the project preset InName (UDreamGUISettings::GradientPresets) with InGradient as CSS, and save the settings. */
	DREAMGUIEDITOR_API bool SaveAsProjectPreset(const FDreamGradient& InGradient, const FString& InName, FText& OutError);
}
