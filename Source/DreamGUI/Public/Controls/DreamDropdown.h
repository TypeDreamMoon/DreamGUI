// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamDropdown.generated.h"

class UDreamWidget;
class UUIDropdown;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamDropdownChangedEvent, int32, SelectedIndex);

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
	/** This instance's own look -- consulted only when StyleSource is Inline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown", meta = (EditCondition = "StyleSource == EDreamUIStyleSource::Inline"))
	FDreamDropdownStyle Style;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	TArray<FText> Options;

	/** Authored selection in; mirror of the behaviour's out. -1 is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown")
	int32 SelectedIndex = 0;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Dropdown")
	FDreamDropdownChangedEvent OnSelectionChanged;

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	int32 GetSelectedIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetSelectedIndex(int32 InIndex);

	/** Replace the options and rebuild the list next time it opens. */
	UFUNCTION(BlueprintCallable, Category = "Dropdown")
	void SetOptions(const TArray<FText>& InOptions);

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
	virtual void NativeOnInitialized() override;

private:
	void HandleValueChanged(int32 InIndex);
	void PushOptions();
};
