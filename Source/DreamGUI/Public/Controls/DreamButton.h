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
 *
 * THE HOLE. The face also carries a `Content` slot, and it is the default one, so nesting fills it:
 *
 *     Native.Button Confirm {
 *         HStack { Image Icon { } Text { Text = "确定" } }
 *     }
 *
 * One child, as every content slot takes one -- an icon BESIDE a label is a panel, and a panel is a
 * thing the author puts in the hole rather than a thing the hole has to be. Filling it stands the
 * stock label down: they are two answers to "what is on this button", and drawing both would put
 * one over the other.
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

	/** The hole. Empty is the normal state; what the host puts here replaces the stock label. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UUIButton> ButtonBehaviour = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleClicked();
};
