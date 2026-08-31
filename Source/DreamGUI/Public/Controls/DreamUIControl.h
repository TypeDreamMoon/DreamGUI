// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Controls/DreamControlStyles.h"
#include "Controls/DreamUIStyleSheet.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Engine/Texture.h"
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
	/**
	 * Round a face. Every control's face is a procedural rect now -- that is where most of the UMG
	 * feel lives -- and the radius is the one thing all of them push the same way.
	 */
	static void ShapeFace(const UDreamWidget* InNode, float InRadius)
	{
		if (UDreamRectBlock* Rect = InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr)
		{
			Rect->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Rect->SetCornerRadius(FVector4(InRadius, InRadius, InRadius, InRadius));
		}
	}

	/**
	 * Skin a face. The brush becomes the rect's BODY texture, so the silhouette, the border and the
	 * selectable's tint keep working over it; an empty brush is the plain rect again (a null body
	 * texture self-heals to white). Sprite wins over texture -- see the brush struct.
	 */
	static void SkinFace(const UDreamWidget* InNode, const FDreamUIFaceBrush& InBrush)
	{
		if (UDreamRectBlock* Rect = InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr)
		{
			UDreamUISpriteData_BaseObject* Sprite = Cast<UDreamUISpriteData_BaseObject>(InBrush.Image);
			Rect->SetBodySpriteTexture(Sprite);
			Rect->SetBodyTexture(Cast<UTexture>(InBrush.Image));
			Rect->SetBodyTextureMode(Sprite != nullptr
				? EDreamRectBlockTextureMode::Sprite
				: EDreamRectBlockTextureMode::Texture);
			Rect->SetBodyTextureScaleMode(InBrush.ScaleMode);
			// The tint multiplies whatever the face shows -- the image, or the plain rect when the
			// brush is empty. White is "no opinion", which keeps every existing look intact.
			Rect->SetBodyColor(InBrush.Tint);
		}
	}

	/**
	 * Authored height for the CONTROL itself; width stays whoever-placed-it's. Syncs the slot's
	 * desired-size snapshot, because the slot's first capture (OnRegister) runs before any style
	 * ever applied -- without the sync an Auto consumer measures the pre-style default (100), not
	 * the style's number. The bare-current-size measure fallback that used to paper over this now
	 * lives at the measure root only (see UDreamPanelLayoutBase::GetDesiredSize).
	 */
	void SizeControlHeight(float InHeight)
	{
		SetHeight(InHeight);
		if (UDreamPanelSlot* Slot = GetPanelSlot())
		{
			Slot->SyncAuthoredDesiredSizeFromWidget();
		}
	}

	/** The brush's drawn size when it states one, the style's size otherwise -- Slate's ImageSize rule. */
	static FVector2D BrushSizeOr(const FDreamUIFaceBrush& InBrush, const FVector2D& InStyleSize)
	{
		return InBrush.ImageSize.IsNearlyZero() ? InStyleSize : InBrush.ImageSize;
	}

	/**
	 * Authored size for a rect-faced part (or the control itself). A rect block states no intrinsic
	 * size, so this is what an Auto slot's desired-size fallback reads -- and the slot SNAPSHOTS it,
	 * so a style edit after the first arrange must re-take the snapshot or the new number is never
	 * read. A consumer that stretches the node still wins, as always.
	 */
	static void SizeFace(UDreamWidget* InNode, const FVector2D& InSize)
	{
		if (InNode != nullptr)
		{
			InNode->SetWidth(static_cast<float>(InSize.X));
			InNode->SetHeight(static_cast<float>(InSize.Y));
			if (UDreamPanelSlot* Slot = InNode->GetPanelSlot())
			{
				Slot->CaptureAuthoredGeometry(true);
			}
		}
	}

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
