// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Controls/DreamControlStyles.h"
#include "DreamUIStyleSheet.generated.h"

/**
 * One place where the project's controls agree on how to look.
 *
 * The counterpart of a Slate style set, resolved the same way: at construction, by name. There is
 * no runtime re-theme here and none intended -- what this buys is that changing the project's
 * accent colour is one edit to one asset, instead of a visit to every screen that ever placed a
 * toggle. Per family it holds the default plus named variants ("Danger", "Compact"), which is the
 * whole of what a control needs to say about itself: which family, and optionally which name.
 *
 * A project without one is fine: the structs' own defaults are the built-in theme, and every
 * control falls back to them. So the plugin ships no asset -- there is nothing a default sheet
 * would say that the code does not already.
 *
 * Point UDreamGUISettings::DefaultStyleSheet at the project's sheet to turn it on.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIStyleSheet : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The project's sheet, or null when none is configured -- which callers treat as "use the
	 * inline defaults", not as an error. Resolves the settings soft pointer, loading it the first
	 * time; construction-time lookup, so the one-off load is where a hitch belongs.
	 */
	static const UDreamUIStyleSheet* GetProjectSheet();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toggle")
	FDreamToggleStyle Toggle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toggle")
	TMap<FName, FDreamToggleStyle> ToggleVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Button")
	FDreamButtonStyle Button;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Button")
	TMap<FName, FDreamButtonStyle> ButtonVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slider")
	FDreamSliderStyle Slider;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slider")
	TMap<FName, FDreamSliderStyle> SliderVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Input")
	FDreamTextInputStyle TextInput;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Input")
	TMap<FName, FDreamTextInputStyle> TextInputVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dropdown")
	FDreamDropdownStyle Dropdown;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dropdown")
	TMap<FName, FDreamDropdownStyle> DropdownVariants;

	/**
	 * The named variant, or the family default when the name is none or matches nothing.
	 *
	 * Falling back rather than failing is deliberate: a misspelled variant produces the project's
	 * default look, which is visibly *something* on screen and correct in nine cases out of ten,
	 * where a null would make every caller carry the same if.
	 */
	const FDreamToggleStyle& ToggleStyle(FName InVariant) const { return Pick(Toggle, ToggleVariants, InVariant); }
	const FDreamButtonStyle& ButtonStyle(FName InVariant) const { return Pick(Button, ButtonVariants, InVariant); }
	const FDreamSliderStyle& SliderStyle(FName InVariant) const { return Pick(Slider, SliderVariants, InVariant); }
	const FDreamTextInputStyle& TextInputStyle(FName InVariant) const { return Pick(TextInput, TextInputVariants, InVariant); }
	const FDreamDropdownStyle& DropdownStyle(FName InVariant) const { return Pick(Dropdown, DropdownVariants, InVariant); }

private:
	template<class T>
	static const T& Pick(const T& InDefault, const TMap<FName, T>& InVariants, FName InVariant)
	{
		if (!InVariant.IsNone())
		{
			if (const T* Found = InVariants.Find(InVariant))
			{
				return *Found;
			}
		}
		return InDefault;
	}
};
