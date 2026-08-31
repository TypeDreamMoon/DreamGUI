// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamToggle.generated.h"

class UDreamImage;
class UDreamText;
class UDreamWidget;
class UUIToggle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamToggleChangedEvent, bool, bIsOn);

/**
 * A toggle whose hierarchy is code, not an asset.
 *
 * The same four nodes BP_Toggle has -- a box, a tick inside it, a label beside it -- built in
 * NativeOnInitialized instead of instanced from a template. What that buys is that the control
 * cannot be half-built: BP_Button shipped for months with no UIButton on it and nothing said so,
 * and a class that always adds its own behaviour cannot have that happen to it.
 *
 * What it costs is the tree nobody can open. Everything an author would otherwise have reached into
 * the hierarchy to change has to be a property here, which is what FDreamToggleStyle is, and the
 * moment a project wants all its toggles to agree that stops being enough.
 *
 * It composes into .dui with no language change at all, because a class path is already a node tag:
 *
 *     /Script/DreamGUI.DreamToggle Mute {
 *         Label = "muted"
 *         Style.TickChecked = #FF3355
 *         OnToggleChanged -> HandleMute
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Toggle")
class DREAMGUI_API UDreamToggle : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/** This instance's own look -- consulted only when StyleSource is Inline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle", meta = (EditCondition = "StyleSource == EDreamUIStyleSource::Inline"))
	FDreamToggleStyle Style;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle")
	FText Label;

	/**
	 * Checked or not. A property rather than the getter/setter pair alone, because the pair alone is
	 * invisible: .dui writes properties, the designer lists properties, and a binding resolves a
	 * property -- so a knob that exists only as two UFUNCTIONs is a knob nothing outside C++ can turn.
	 *
	 * It is the authored value going in and a mirror of the behaviour's coming out; HandleValueChanged
	 * keeps it honest when the user is the one who changed it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle")
	bool bIsOn = false;

	/** Fired by the toggle underneath, re-broadcast here so a consumer never has to reach into the parts. */
	UPROPERTY(BlueprintAssignable, Category = "Toggle")
	FDreamToggleChangedEvent OnToggleChanged;

	UFUNCTION(BlueprintCallable, Category = "Toggle")
	bool GetIsOn() const;

	UFUNCTION(BlueprintCallable, Category = "Toggle")
	void SetIsOn(bool bInIsOn);

	virtual void ApplyStyle() override;

	/**
	 * The parts, in the shape the rest of the framework expects to find them.
	 *
	 * UPROPERTY rather than bare pointers on purpose: a part nothing reflects is a part the designer,
	 * the write-back and the bindings cannot see. Transient because they are rebuilt on every
	 * initialization and never belong to a saved package.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UDreamWidget> BoxNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UDreamWidget> TickNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UDreamWidget> LabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UUIToggle> ToggleBehaviour = nullptr;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleValueChanged(bool bInIsOn);
};
