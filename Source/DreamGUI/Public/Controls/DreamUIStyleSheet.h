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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progress Bar")
	FDreamProgressBarStyle ProgressBar;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progress Bar")
	TMap<FName, FDreamProgressBarStyle> ProgressBarVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio Button")
	FDreamRadioButtonStyle RadioButton;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio Button")
	TMap<FName, FDreamRadioButtonStyle> RadioButtonVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spin Box")
	FDreamSpinBoxStyle SpinBox;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spin Box")
	TMap<FName, FDreamSpinBoxStyle> SpinBoxVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Box")
	FDreamScrollBoxStyle ScrollBox;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Box")
	TMap<FName, FDreamScrollBoxStyle> ScrollBoxVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar")
	FDreamScrollBarStyle ScrollBar;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar")
	TMap<FName, FDreamScrollBarStyle> ScrollBarVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "List")
	FDreamListStyle List;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "List")
	TMap<FName, FDreamListStyle> ListVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tree View")
	FDreamTreeViewStyle TreeView;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tree View")
	TMap<FName, FDreamTreeViewStyle> TreeViewVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tab View")
	FDreamTabViewStyle TabView;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tab View")
	TMap<FName, FDreamTabViewStyle> TabViewVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialog")
	FDreamDialogStyle Dialog;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialog")
	TMap<FName, FDreamDialogStyle> DialogVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Expandable Area")
	FDreamExpandableAreaStyle ExpandableArea;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Expandable Area")
	TMap<FName, FDreamExpandableAreaStyle> ExpandableAreaVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Key Selector")
	FDreamInputKeySelectorStyle InputKeySelector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input Key Selector")
	TMap<FName, FDreamInputKeySelectorStyle> InputKeySelectorVariants;

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
	const FDreamProgressBarStyle& ProgressBarStyle(FName InVariant) const { return Pick(ProgressBar, ProgressBarVariants, InVariant); }
	const FDreamRadioButtonStyle& RadioButtonStyle(FName InVariant) const { return Pick(RadioButton, RadioButtonVariants, InVariant); }
	const FDreamSpinBoxStyle& SpinBoxStyle(FName InVariant) const { return Pick(SpinBox, SpinBoxVariants, InVariant); }
	const FDreamScrollBoxStyle& ScrollBoxStyle(FName InVariant) const { return Pick(ScrollBox, ScrollBoxVariants, InVariant); }
	const FDreamScrollBarStyle& ScrollBarStyle(FName InVariant) const { return Pick(ScrollBar, ScrollBarVariants, InVariant); }
	const FDreamListStyle& ListStyle(FName InVariant) const { return Pick(List, ListVariants, InVariant); }
	const FDreamTreeViewStyle& TreeViewStyle(FName InVariant) const { return Pick(TreeView, TreeViewVariants, InVariant); }
	const FDreamTabViewStyle& TabViewStyle(FName InVariant) const { return Pick(TabView, TabViewVariants, InVariant); }
	const FDreamDialogStyle& DialogStyle(FName InVariant) const { return Pick(Dialog, DialogVariants, InVariant); }
	const FDreamExpandableAreaStyle& ExpandableAreaStyle(FName InVariant) const { return Pick(ExpandableArea, ExpandableAreaVariants, InVariant); }
	const FDreamInputKeySelectorStyle& InputKeySelectorStyle(FName InVariant) const { return Pick(InputKeySelector, InputKeySelectorVariants, InVariant); }

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
