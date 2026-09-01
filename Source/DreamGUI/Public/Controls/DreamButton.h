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
 * Two nodes -- a face, and the hole on it -- and the behaviour BP_Button shipped without for months.
 * That omission is the whole argument for this class existing: a control that always adds its own
 * UIButton has no state in which clicking it does nothing.
 *
 * The face's brush stays white and the selectable writes the style's colours onto it as absolutes,
 * rather than the gallery's habit of a coloured brush multiplied by near-white tints -- a product
 * of two sources is a look no style sheet can name.
 *
 * IT DRAWS NO TEXT OF ITS OWN. What is on a button is whatever the host puts in the hole, which is
 * the default one, so nesting fills it:
 *
 *     Native.Button Confirm {
 *         Text { Text = "确定" }
 *         OnClicked -> HandleConfirm
 *     }
 *
 * One child, as every content slot takes one -- an icon BESIDE a label is a panel, and a panel is a
 * thing the author puts in the hole rather than a thing the hole has to be. There used to be a
 * stock `Label` property with a UDreamText node behind it, and the hole stood it down whenever it
 * filled: two answers to "what is on this button" alternating, with a swap rule to keep exactly one
 * of them awake. One answer needs no rule. UDreamDialog, which was that property's only real
 * consumer, now puts a UDreamText of its own in the hole like any other host.
 *
 * ITS SIZE IS ITS CONTENT'S. The face is a UMG Size Box: it keeps ContentPadding around whatever is
 * in the hole, and measures to that plus the padding, with the style's Height as a FLOOR rather
 * than as the height. That is the difference between a button an Auto row can measure and the one
 * this was: a stock label with a text layout of its own decided the height, the authored number was
 * written onto the widget where the next arrange overwrote it, and the hole -- a container-less
 * node -- never arranged what a host hung in it at all.
 *
 * Height being a floor is what makes both readings work: an empty button is ContentPadding wide and
 * Height tall, a button with a line of text in it is that text plus the padding and still at least
 * Height tall, and a button with something big in it grows. Width has no floor -- an axis nobody
 * states is the consumer's, which is the family's rule everywhere else too.
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

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnClicked;

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	/** The hole. Empty is the normal state, and an empty one claims no size -- see RealizeBuiltIn. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UUIButton> ButtonBehaviour = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	void HandleClicked();
};
