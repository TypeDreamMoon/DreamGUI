// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamGUISettings.h"
#include "Widget/SDreamUIEyedropper.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWrapBox;

/**
 * The project's palette (UDreamGUISettings::ProjectSwatches): read, added to and taken from, saved with the project's
 * DreamGUI settings (DefaultDreamGUI.ini) at once, so the next author to open the project sees the same colours.
 */
struct DREAMGUIEDITOR_API FDreamUIProjectPalette
{
	static const TArray<FDreamUISwatch>& Get();
	/** InSRGB at the end of the palette, unless it is there already. Its name is its hex code when InName is empty. */
	static void Add(const FColor& InSRGB, const FString& InName = FString());
	static void Remove(int32 InIndex);
	/** Every change made here, and an edit of the settings in the Project Settings window. */
	static FSimpleMulticastDelegate& OnChanged();
	/** The project's palette in the Project Settings window. */
	static void OpenInProjectSettings();
};

DECLARE_DELEGATE_OneParam(FOnDreamUIColorCommitted, const FLinearColor& /*Linear light*/);

/**
 * The project's palette as a grid of swatches: one click commits the swatch's colour (OnSwatchClicked), a right click offers
 * to take it out of the palette. With CurrentColor bound, the grid ends with a + that adds that colour.
 */
class DREAMGUIEDITOR_API SDreamUISwatchGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUISwatchGrid) : _SwatchSize(16.0f) {}
		SLATE_EVENT(FOnDreamUIColorCommitted, OnSwatchClicked)
		/** The colour the + adds; no + when unbound. Linear light. */
		SLATE_ATTRIBUTE(FLinearColor, CurrentColor)
		SLATE_ARGUMENT(float, SwatchSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDreamUISwatchGrid() override;

	/** What a click on the InIndex'th swatch does. What a test drives. */
	void ClickSwatch(int32 InIndex);
	/** What the + does. */
	void AddCurrentColor();

private:
	void Rebuild();

	TSharedPtr<SWrapBox> Box;
	FOnDreamUIColorCommitted OnSwatchClickedDelegate;
	TAttribute<FLinearColor> CurrentColor;
	float SwatchSize = 16.0f;
	FDelegateHandle PaletteChangedHandle;
};

/**
 * What a colour row puts beside its colour: an eyedropper and the project's palette.
 *
 * A pick, or a swatch clicked, is OnColorCommitted: the caller writes it, in a transaction of its own (one undo step). The
 * palette's menu also adds the row's current colour to the palette, and opens the palette in the project settings.
 */
class DREAMGUIEDITOR_API SDreamUIColorQuickPick : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIColorQuickPick) : _bShowEyedropper(true) {}
		/** The colour being edited: what "add to the palette" adds. Linear light. */
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_EVENT(FOnDreamUIColorCommitted, OnColorCommitted)
		/** The eyedropper's colour under the pointer while it picks; optional. */
		SLATE_EVENT(FOnDreamUIColorCommitted, OnColorPreview)
		/** The eyedropper put down without a pick: undo what OnColorPreview showed. */
		SLATE_EVENT(FSimpleDelegate, OnPreviewCancelled)
		SLATE_ARGUMENT(bool, bShowEyedropper)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The eyedropper, for a test or a caller that starts it by hand. Null when it is not shown. */
	TSharedPtr<SDreamUIEyedropperButton> GetEyedropper() const { return Eyedropper; }

private:
	TSharedRef<SWidget> MakePaletteMenu();

	TAttribute<FLinearColor> Color;
	FOnDreamUIColorCommitted OnColorCommittedDelegate;
	TSharedPtr<SDreamUIEyedropperButton> Eyedropper;
};
