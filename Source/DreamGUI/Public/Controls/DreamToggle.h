// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "DreamToggle.generated.h"

class UDreamImage;
class UDreamText;
class UDreamWidget;
class UUIToggle;

/**
 * What a toggle looks like, separated from what it is.
 *
 * This is the FButtonStyle shape, and it is here for the reason Slate's exists: a control assembled
 * in code has no tree for anyone to open and recolour, so every appearance decision it makes has to
 * be a knob or it is a fork. Per-instance and serialized, like UMG's -- a project-wide default that
 * an instance overrides is the layer above this one, and it does not exist yet.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamToggleStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FVector2D BoxSize = FVector2D(26.0, 26.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FVector2D TickSize = FVector2D(14.0, 14.0);

	/** Between the box and the label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	float Spacing = 8.0;

	/**
	 * The box, which is what the pointer transition tints. The tick is a separate visual on purpose:
	 * one visual cannot carry both transitions, because whichever fires last wins.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor BoxNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor BoxHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor BoxPressed = FColor(38, 42, 52, 255);

	/** The tick, which is what the checked transition tints. Unchecked is transparent, not absent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor TickChecked = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor TickUnchecked = FColor(0, 119, 255, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor LabelColor = FColor(230, 233, 240, 255);
};

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
class DREAMGUI_API UDreamToggle : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle")
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

	/** Re-push Style and Label into the parts. Called for you when either is set; public for a caller that edits in place. */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	void ApplyStyle();

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

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void HandleValueChanged(bool bInIsOn);
};
