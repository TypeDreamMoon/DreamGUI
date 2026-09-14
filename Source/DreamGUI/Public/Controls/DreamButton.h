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

	/**
	 * WHEN this button's click fires, per input kind -- UMG's three enums, surfaced at the control.
	 *
	 * The behaviour has carried them since they were added to UUISelectable; a control that did not
	 * state them left every button in the project on the desktop's DownAndUp, which is right for a
	 * desktop and wrong for a key on a virtual keyboard (which wants MouseDown) and for a button
	 * inside a scroll box (which wants PreciseClick, so a drag that was really a scroll cancels).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetClickMethod", BlueprintSetter = "SetClickMethod", Category = "Button")
	EDreamUIClickMethod ClickMethod = EDreamUIClickMethod::DownAndUp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTouchMethod", BlueprintSetter = "SetTouchMethod", Category = "Button")
	EDreamUITouchMethod TouchMethod = EDreamUITouchMethod::DownAndUp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetPressMethod", BlueprintSetter = "SetPressMethod", Category = "Button")
	EDreamUIPressMethod PressMethod = EDreamUIPressMethod::DownAndUp;

	/*
	 * UMG's IsFocusable is UDreamWidget's own bIsFocusable / GetIsFocusable / SetIsFocusable, which
	 * EVERY widget in this framework carries -- so this control declares none of its own. A second
	 * member of that name would shadow the base one (which UHT refuses outright) and would be a
	 * second answer to one question besides.
	 */

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnClicked;

	/**
	 * The other four moments UMG's button speaks, re-broadcast from the same behaviour and for the
	 * same reason OnClicked is: the signals existed on UUIButton the whole time and stopped at the
	 * face, so anything wanting them -- a sound on press, a tooltip on hover -- had to reach past the
	 * control into a part the control owns.
	 *
	 * Press and release are the POINTER's, not the click's: a press that slides off the button
	 * releases without clicking, which is exactly the distinction a hold-to-charge control needs.
	 * A disabled button broadcasts none of the press pair (the behaviour gates them).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnPressed;

	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnReleased;

	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnHovered;

	UPROPERTY(BlueprintAssignable, Category = "Button")
	FDreamButtonClickedEvent OnUnhovered;

	UFUNCTION(BlueprintCallable, Category = "Button")
	EDreamUIClickMethod GetClickMethod() const { return ClickMethod; }

	/** Each of the four setters below writes the field and re-pushes it onto the behaviour. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetClickMethod(EDreamUIClickMethod InMethod);

	UFUNCTION(BlueprintCallable, Category = "Button")
	EDreamUITouchMethod GetTouchMethod() const { return TouchMethod; }

	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetTouchMethod(EDreamUITouchMethod InMethod);

	UFUNCTION(BlueprintCallable, Category = "Button")
	EDreamUIPressMethod GetPressMethod() const { return PressMethod; }

	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetPressMethod(EDreamUIPressMethod InMethod);

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
	void HandlePressed();
	void HandleReleased();
	void HandleHovered();
	void HandleUnhovered();
};
