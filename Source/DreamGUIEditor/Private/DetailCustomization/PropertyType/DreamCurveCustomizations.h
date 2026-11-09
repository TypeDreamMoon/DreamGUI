// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamLayerStyleTypes.h"
#include "IPropertyTypeCustomization.h"
#include "Widget/SDreamCurveEditor.h"

class IPropertyHandle;
class SWidget;
enum class EDreamTweenEase : uint8;

namespace DreamCurveCustomizations
{
	/** A contour's curve as handles: a custom one's points; a named shape sampled, so that editing it starts from it. */
	DREAMGUIEDITOR_API TArray<FDreamCurvePoint> PointsOf(const FDreamStyleContour& InContour);
	/** The custom contour InPoints make. */
	DREAMGUIEDITOR_API FDreamStyleContour ContourOf(const TArray<FDreamCurvePoint>& InPoints);
	/** An ease's curve at T, 0..1; a curve asset's ease (CurveFloat) reads as linear here. */
	DREAMGUIEDITOR_API float EvaluateEase(EDreamTweenEase InEase, float InT);
}

/**
 * A layer style's contour: its curve beside its name, and a popup with the named shapes as pictures to pick and the
 * curve itself to edit (SDreamCurveEditor). Editing a named shape starts a custom curve from it. A drag is one undo step;
 * every change goes through the property handle, so it is carried to a designer's template and written to the .dui --
 * where a contour is a named shape's name or custom(...) of its points (FDreamStyleContour::ToText).
 */
class DREAMGUIEDITOR_API FDreamContourCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	/** InContour on every object the handle edits, as a property row sets a value. */
	static void Commit(const TSharedRef<IPropertyHandle>& InHandle, const FDreamStyleContour& InContour, bool bInteractive);

private:
	TSharedRef<SWidget> MakePopup();
	const FDreamStyleContour& Current() const;
	void BeginEdit();
	void EndEdit();

	TSharedPtr<IPropertyHandle> Handle;
	mutable FDreamStyleContour Cached;
	mutable uint64 CachedFrame = MAX_uint64;
	bool bEditing = false;
};

/** Every EDreamTweenEase property: the ease's curve beside its name, and a menu of them all as curves. */
class DREAMGUIEDITOR_API FDreamEaseCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

private:
	TSharedRef<SWidget> MakeMenu();
	TOptional<uint8> GetCurrent() const;
	TSharedPtr<IPropertyHandle> Handle;
};
