// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Controls/DreamControlStyles.h"
#include "Controls/DreamUIStyleSheet.h"
#include "DreamUIControl.generated.h"

/**
 * A control whose hierarchy is code, not an asset.
 *
 * What every one of them shares is not the tree -- each builds its own in NativeOnInitialized --
 * but the style contract: where the look comes from (the project sheet by default, this instance
 * on request), which named variant, and the obligation to re-push every knob when one changes,
 * because nothing re-derives from a property the way instancing a changed template would. That
 * last part is UMG's SynchronizeProperties, and it is the tax the whole family pays.
 *
 * The concrete style struct stays on the derived class, typed; a control resolves it as
 *
 *     const FDreamToggleStyle& S = ResolveStyle(Style, &UDreamUIStyleSheet::ToggleStyle);
 *
 * which reads the sheet when StyleSource says to and this instance's Style otherwise.
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamUIControl : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	/** See EDreamUIStyleSource: the sheet is the default because one-place-changes-all is the point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	EDreamUIStyleSource StyleSource = EDreamUIStyleSource::ProjectStyleSheet;

	/** Named entry in the sheet ("Danger", "Compact"); none means the family default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "StyleSource == EDreamUIStyleSource::ProjectStyleSheet"))
	FName StyleVariant;

	/**
	 * Re-push the resolved style, and every other knob, into the parts. Called for you after the
	 * tree is built and whenever a property changes in the editor; call it yourself after editing
	 * a style in place at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Style")
	virtual void ApplyStyle() {}

protected:
	template<class TStyle>
	const TStyle& ResolveStyle(const TStyle& InInlineStyle, const TStyle& (UDreamUIStyleSheet::*InFamily)(FName) const) const
	{
		if (StyleSource == EDreamUIStyleSource::ProjectStyleSheet)
		{
			if (const UDreamUIStyleSheet* Sheet = UDreamUIStyleSheet::GetProjectSheet())
			{
				return (Sheet->*InFamily)(StyleVariant);
			}
		}
		return InInlineStyle;
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
		ApplyStyle();
	}
#endif
};
