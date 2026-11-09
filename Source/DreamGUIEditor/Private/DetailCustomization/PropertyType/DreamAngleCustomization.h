// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyEditorModule.h"

class IPropertyHandle;

/** A float property is an angle the dial edits when it is tagged DreamGUI_Editor="AngleWheel" (DreamPropertyMeta). */
class DREAMGUIEDITOR_API FDreamAngleWheelIdentifier : public IPropertyTypeIdentifier
{
public:
	virtual bool IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const override;
};

/**
 * An angle as a dial beside its number: drag the dial (Shift snaps to 15 degrees) or type. Which way the angle runs is
 * the metadata's value -- "" for an image editor's (0 at three o'clock, counter-clockwise), "CW", or "TopCW" (a clock's).
 *
 * An effect that can take its style's global light (a bUseGlobalLight beside its angle) edits the global angle while it
 * does, as an image editor's Use Global Light does: turning one such effect's light turns every one that shares it. The
 * row says so.
 *
 * A drag is one undo step: interactive changes while it moves, the value committed when it is let go.
 */
class DREAMGUIEDITOR_API FDreamAngleCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	/** The style's global light angle InAngle shares when its effect uses the global light, found up the property's parents. */
	static TSharedPtr<IPropertyHandle> FindGlobalAngle(const TSharedRef<IPropertyHandle>& InAngle);
	/** Whether every object InAngle edits takes the global light now. */
	static bool UsesGlobalLight(const TSharedRef<IPropertyHandle>& InAngle);

private:
	TSharedPtr<IPropertyHandle> Target() const;
	TOptional<float> GetValue() const;
	void BeginDrag();
	void Drag(float InDegrees);
	void EndDrag(float InDegrees);

	TSharedPtr<IPropertyHandle> Angle;
	TSharedPtr<IPropertyHandle> Global;
	bool bDragging = false;
};
