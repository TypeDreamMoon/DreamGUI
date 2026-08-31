// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamButton.generated.h"

class UDreamWidget;
class UUIButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamButtonClickedEvent);

/**
 * A button whose hierarchy is code, not an asset.
 *
 * Two nodes -- a face, a label on it -- and the behaviour BP_Button shipped without for months.
 * That omission is the whole argument for this class existing: a control that always adds its own
 * UIButton has no state in which clicking it does nothing.
 *
 * The face's brush stays white and the selectable writes the style's colours onto it as absolutes,
 * rather than the gallery's habit of a coloured brush multiplied by near-white tints -- a product
 * of two sources is a look no style sheet can name.
 *
 *     /Script/DreamGUI.DreamButton Confirm {
 *         Label = "确定"
 *         OnClicked -> HandleConfirm
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Button")
class DREAMGUI_API UDreamButton : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FDreamButtonStyle Style;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FText Label;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnClicked;

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> LabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UUIButton> ButtonBehaviour = nullptr;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleClicked();
};
