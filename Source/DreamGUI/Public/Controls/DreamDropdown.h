// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamDropdown.generated.h"

class UDreamWidget;
class UUIDropdown;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamDropdownChangedEvent, int32, SelectedIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamDropdownItemEvent, int32, ItemIndex, UDreamWidget*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamDropdownSimpleEvent);

/**
 * A dropdown whose hierarchy is code, not an asset.
 *
 * The largest of the preset Blueprints -- sixteen widgets -- reduced to what UUIDropdown actually
 * reads: a face with a caption, a list root that Show() positions and animates, and inside it a
 * content column holding one templated row. The behaviour duplicates that row per option, so the
 * template is authored once, inactive, and never drawn itself.
 *
 * The row carries the toggle arrangement the library keeps arriving at: hover tints the row's own
 * face, the selection mark is a separate visual, because one visual cannot hold two transitions.
 *
 * Options are plain texts here rather than the behaviour's text+brush pairs: the common case, and
 * the control's job is to be the common case. A consumer needing per-option icons talks to
 * DropdownBehaviour directly.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Dropdown")
class DREAMGUI_API UDreamDropdown : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	FDreamDropdownStyle Style;

	/**
	 * BlueprintReadOnly rather than a BlueprintSetter pair, and for the reason UDreamDialog::Buttons
	 * is: the options are what the open list is BUILT from, so a Blueprint writing this array in
	 * place changed the data and left the list showing the old copy. SetOptions is the way in. The
	 * designer and .dui still author it directly, where PostEditChangeProperty re-pushes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dropdown")
	TArray<FText> Options;

	/** Authored selection in; mirror of the behaviour's out. -1 is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectedIndex", BlueprintSetter = "SetSelectedIndex", Category = "Dropdown")
	int32 SelectedIndex = 0;

	/**
	 * How many rows the open list shows at most. The list is always exactly as tall as its visible
	 * rows -- rows-times-row-height, no more -- and past this many the rest scroll: the cap is a
	 * count because that is how a designer thinks about a dropdown, not in pixels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMaxVisibleItems", BlueprintSetter = "SetMaxVisibleItems", Category = "Dropdown", meta = (ClampMin = "1"))
	int32 MaxVisibleItems = 6;

	/**
	 * A picture per option, index-matched to Options -- the same parallel-array idiom TabLabels and
	 * TabEnabled use, and for the same reason: a struct per option would make the common case (no
	 * icons at all) cost an array literal the language cannot write.
	 *
	 * A sprite or a texture, by the rule every face in this library follows. A missing or null entry
	 * is no icon, so a short list is an ordinary state.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	TArray<TObjectPtr<UObject>> OptionIcons;

	/**
	 * Whether the face draws its own arrow glyph -- UMG's HasDownArrow. Off is for a face that says
	 * "open me" some other way (an icon of its own, a border).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetHasDownArrow", BlueprintSetter = "SetHasDownArrow", Category = "Dropdown")
	bool bHasDownArrow = true;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownChangedEvent OnSelectionChanged;

	/**
	 * The list is opening -- UMG's OnOpening, and the moment to refresh the options from.
	 *
	 * Fired from the behaviour's Show, which is the moment the list appears. The rows are placed
	 * immediately afterwards, so options written from a handler here are the ones the player sees.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownSimpleEvent OnOpening;

	/** The list closed, whether by a choice or by a click elsewhere. */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownSimpleEvent OnClosed;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownChangedEvent OnValueChangedBP;


	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	int32 GetSelectedIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetSelectedIndex(int32 InIndex);

	/** Replace the options and rebuild the list next time it opens. */
	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetOptions(const TArray<FText>& InOptions);

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	TArray<FText> GetOptions() const { return Options; }

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	int32 GetMaxVisibleItems() const { return MaxVisibleItems; }

	/**
	 * How many rows the open list shows at most, re-pushed at once.
	 *
	 * A setter because the cap is read only by the style push, which is what turns a row COUNT into
	 * the list's pixel height -- so a runtime write onto the variable moved a number nothing read
	 * until something else happened to restyle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetMaxVisibleItems(int32 InMaxVisibleItems);

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	bool GetHasDownArrow() const { return bHasDownArrow; }

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetHasDownArrow(bool bInHasDownArrow);

	/** Replace the per-option pictures and re-push the list, so an open one changes under the pointer. */
	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetOptionIcons(const TArray<UObject*>& InIcons);

	/**
	 * An option row's CONTENT, authored elsewhere: one instance of this class is created inside
	 * every item widget, filling it, and the built-in label steps aside. The row's face, its check
	 * mark, its hover and its selection stay the control's, so a template only has to draw an option.
	 *
	 * Made per ITEM rather than pooled, because a dropdown rebuilds its list on every open rather
	 * than recycling rows; OnItemGenerated fires alongside, and is where a consumer fills it.
	 *
	 * Null (the default) is the built-in label row. Instancing a user widget needs a world, so with
	 * none this quietly stays the built-in row rather than producing half a list. Exactly the bargain
	 * UDreamListViewBase::RowTemplateClass makes, in the same words.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	TSubclassOf<UDreamUserWidget> ItemTemplateClass;

	/**
	 * One per option row, as the list is built. The hook for a consumer whose options are richer
	 * than a word but who would rather not author a whole class: everything under the row is
	 * reachable from here by display name. The dropdown's counterpart of the list's OnRowGenerated.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownItemEvent OnItemGenerated;

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dropdown")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dropdown")
	TObjectPtr<UDreamWidget> CaptionNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dropdown")
	TObjectPtr<UDreamWidget> ArrowNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dropdown")
	TObjectPtr<UDreamWidget> ListNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dropdown")
	TObjectPtr<UDreamWidget> ItemTemplateNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dropdown")
	TObjectPtr<UUIDropdown> DropdownBehaviour = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
	virtual void OnPartsReady() override;
#if WITH_EDITOR
	/** The base re-applies style; options and selection live outside ApplyStyle and re-push here. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void HandleListVisibilityChanged(bool bInVisible);
	void HandleValueChanged(int32 InIndex);
	void PushOptions();
	void ApplyListRestingGeometry(const FDreamDropdownStyle& InActive);

	/** The rows the behaviour duplicated out of the template, which is everything in the column but it. */
	TArray<UDreamWidget*> GetItemRows() const;

	/**
	 * One option row's whole look. The template and every live row go through it, so a restyle
	 * reaches the rows that already exist rather than only the thing the next rebuild copies.
	 */
	void PushItemStyle(UDreamWidget* InItem, const FDreamDropdownStyle& InActive);

	/** True between Elevate and Restore; resting geometry must not be written while it is. */
	bool bListElevated = false;
};
