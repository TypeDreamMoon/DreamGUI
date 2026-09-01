// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamDropdown.generated.h"

class UDreamWidget;
class UUIDropdown;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamDropdownChangedEvent, int32, SelectedIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamDropdownItemEvent, int32, ItemIndex, UDreamWidget*, Item);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	TArray<FText> Options;

	/** Authored selection in; mirror of the behaviour's out. -1 is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	int32 SelectedIndex = 0;

	/**
	 * How many rows the open list shows at most. The list is always exactly as tall as its visible
	 * rows -- rows-times-row-height, no more -- and past this many the rest scroll: the cap is a
	 * count because that is how a designer thinks about a dropdown, not in pixels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown", meta = (ClampMin = "1"))
	int32 MaxVisibleItems = 6;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownChangedEvent OnSelectionChanged;

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

	/** True between Elevate and Restore; resting geometry must not be written while it is. */
	bool bListElevated = false;
};
