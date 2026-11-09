// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FViewport;
class SBorder;

/** Where a picked colour was read. */
enum class EDreamUIPickedFrom : uint8
{
	/** A pixel of a DreamGUI designer viewport, as it rendered it: after every effect, style and blend mode. */
	DesignerViewport,
	/** The screen's pixel: anywhere else on the desktop. */
	Screen,
};

/** A colour the eyedropper read, in the two forms a property wants it. */
struct FDreamUIPickedColor
{
	/** Linear light, opaque: for an FLinearColor property. */
	FLinearColor Linear = FLinearColor::White;
	/** The 8-bit sRGB value the pixel holds, opaque: for an FColor property, and what a `.dui` spells `#RRGGBB`. */
	FColor SRGB = FColor::White;
	EDreamUIPickedFrom Source = EDreamUIPickedFrom::Screen;
};

DECLARE_DELEGATE_OneParam(FOnDreamUIColorPicked, const FDreamUIPickedColor&);

/**
 * Reading the colour under a point of the desktop. What the eyedropper button samples with, for a caller with its own UI
 * (a viewport tool, a test).
 *
 * Over a DreamGUI designer viewport the pixel is read from the viewport's own render target -- the final image of the
 * preview, every effect behind and on it applied, and nothing Slate draws over the viewport (rulers, the toolbar). Anywhere
 * else it is the screen's pixel, as the engine's colour picker reads it.
 */
struct DREAMGUIEDITOR_API FDreamUIEyedropper
{
	static bool Sample(const FVector2D& InScreenPosition, FDreamUIPickedColor& OutColor);
	/** One pixel of a viewport's target, by pixel coordinate. False without a GPU (-nullrhi) or off the target. */
	static bool SampleViewport(FViewport* InViewport, FIntPoint InPixel, FDreamUIPickedColor& OutColor);
	/** The designer viewport under InScreenPosition and the pixel of it there, when there is one. */
	static bool FindDesignerViewportPixel(const FVector2D& InScreenPosition, FViewport*& OutViewport, FIntPoint& OutPixel);
};

/**
 * An eyedropper: a small button that picks a colour off the designer viewport or the screen.
 *
 * Click it, move the pointer -- the button shows the colour under it, and OnPreview hears it -- and click to pick (OnPicked);
 * Escape or a right click puts it down again (OnCancelled). The button writes nothing: the caller writes the picked colour
 * in OnPicked, inside a transaction of its own, so a pick is one undo step; and puts back in OnCancelled whatever it showed
 * in OnPreview. Previewing onto a widget being picked from changes the pixels being read, so a caller showing the colour
 * on the very widget it reads should preview elsewhere.
 */
class DREAMGUIEDITOR_API SDreamUIEyedropperButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIEyedropperButton) {}
		/** Picking began. */
		SLATE_EVENT(FSimpleDelegate, OnBegin)
		/** The colour under the pointer while picking, on every move that changed it. */
		SLATE_EVENT(FOnDreamUIColorPicked, OnPreview)
		/** The colour picked. */
		SLATE_EVENT(FOnDreamUIColorPicked, OnPicked)
		/** Picking ended without a pick. */
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
		SLATE_ATTRIBUTE(FText, ToolTipText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool IsPicking() const { return bPicking; }
	/** Start picking, as a click on the button does. */
	void BeginPicking();
	/** The pick a click makes, at InScreenPosition: sample, OnPicked, stop. What a test drives. */
	bool PickAt(const FVector2D& InScreenPosition);
	/** Stop without a pick. */
	void CancelPicking();

	// SWidget
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	FReply OnButtonClicked();
	void EndPicking();
	FLinearColor GetSwatchColor() const;
	EVisibility GetSwatchVisibility() const;

	FSimpleDelegate OnBeginDelegate;
	FOnDreamUIColorPicked OnPreviewDelegate;
	FOnDreamUIColorPicked OnPickedDelegate;
	FSimpleDelegate OnCancelledDelegate;

	bool bPicking = false;
	bool bHasSample = false;
	FDreamUIPickedColor LastSample;
};
