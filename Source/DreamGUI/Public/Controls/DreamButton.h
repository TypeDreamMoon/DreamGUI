// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Event/DreamBaseEventData.h"
#include "DreamButton.generated.h"

class UDreamUIDragSource;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Button")
	FDreamButtonStyle Style;

	/**
	 * A tint multiplied over whatever the face is showing -- UMG's BackgroundColor, and the runtime
	 * half of the face brush's authored Tint.
	 *
	 * Two channels rather than one because they answer different questions: the brush's Tint is part
	 * of the LOOK and comes from the style sheet, this one is a thing game code says about this one
	 * button ("the confirm button goes red while the timer runs out"). They multiply, and white --
	 * where this starts -- is no opinion, so every existing button is unchanged.
	 *
	 * Distinct again from the style's five state colours: those ride the visual's own colour through
	 * the selectable's transition, and this rides the rect's body. All three multiply on screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetBackgroundColor", BlueprintSetter = "SetBackgroundColor", Category = "Button")
	FColor BackgroundColor = FColor::White;

	/**
	 * A tint over the WHOLE button, face and content alike -- UMG's ColorAndOpacity, of which the
	 * alpha is the half this framework can honour.
	 *
	 * The alpha is pushed to the control's own RenderOpacity, which cascades down everything under
	 * it: fading a button fades its face, its label and whatever else was put inside, whatever each
	 * of those is drawn with. That is the half nearly every caller wants -- a button fading in, or
	 * greying while a cooldown runs.
	 *
	 * The colour half is deliberately not faked, for the reason UDreamBorder::ContentColorAndOpacity
	 * gives: a colour over a subtree would have to be written onto each visual inside, overwriting
	 * what the host authored with no way back. BackgroundColor above tints the FACE, which is the
	 * part this control owns and the part a caller usually means.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetColorAndOpacity", BlueprintSetter = "SetColorAndOpacity", Category = "Button")
	FLinearColor ColorAndOpacity = FLinearColor::White;

	/**
	 * Whether a drag may START on this button -- UMG's bAllowDragDrop, and off as UMG has it.
	 *
	 * What it turns on is a UDreamUIDragSource on the face, because that is what makes a drag MEAN
	 * something in this framework: without one the pointer pipeline still reports the geometry and
	 * nothing carries a payload. Turning it back off destroys that component again, so a button
	 * that never asked for one never grows one.
	 *
	 * The payload is the drag source's own (Tag, Payload, DragVisualClass); reach it through
	 * GetDragSource. A button that needs a payload only runtime knows subclasses that component --
	 * which is the same road any other widget takes, and the reason this is a switch rather than a
	 * second set of drag properties here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowDragDrop", BlueprintSetter = "SetAllowDragDrop", Category = "Button")
	bool bAllowDragDrop = false;

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

	/**
	 * WHICH mouse buttons press and click this button -- a bitmask over EDreamUIMouseButtonType, the
	 * left button alone by default, which is UMG's rule: SButton answers the left button and nothing
	 * else, so a right click on a button is not a use of it (no OnPressed, OnReleased or OnClicked)
	 * and goes on to whatever is behind it.
	 *
	 * UMG has no knob for this, so this is the one place that can say otherwise: widen it for a button
	 * that should also answer the right or the middle button. A touch and a gamepad or keyboard press
	 * are not mouse buttons and always count. Pushed onto the behaviour with the three methods above --
	 * UUISelectable::AcceptedMouseButtons is what the clicks actually consult. In .dui it is a number,
	 * one bit per EDreamUIMouseButtonType value: 1 is Left, 4 is Right, 5 is both.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAcceptedMouseButtons", BlueprintSetter = "SetAcceptedMouseButtons", Category = "Button",
		meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIMouseButtonType"))
	int32 AcceptedMouseButtons = 1 << static_cast<int32>(EDreamUIMouseButtonType::Left);

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

	UFUNCTION(BlueprintPure, Category = "Button")
	FDreamButtonStyle GetStyle() const { return Style; }

	/**
	 * This instance's whole look, replaced and pushed. Whether it is the look in EFFECT is still
	 * StyleSource's answer -- writing an inline style while the sheet is driving changes nothing you
	 * can see, which is why the push runs either way rather than pretending otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetStyle(const FDreamButtonStyle& InStyle);

	UFUNCTION(BlueprintPure, Category = "Button")
	FColor GetBackgroundColor() const { return BackgroundColor; }

	UFUNCTION(BlueprintPure, Category = "Button")
	FLinearColor GetColorAndOpacity() const { return ColorAndOpacity; }

	/** See the property: the alpha cascades over the whole button, the colour has nowhere to go. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetColorAndOpacity(FLinearColor InColorAndOpacity);

	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetBackgroundColor(FColor InBackgroundColor);

	UFUNCTION(BlueprintPure, Category = "Button")
	bool GetAllowDragDrop() const { return bAllowDragDrop; }

	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetAllowDragDrop(bool bInAllowDragDrop);

	/**
	 * The drag source this button's switch put on the face, or null while the switch is off.
	 *
	 * Defined in the .cpp rather than inline, because the class is only forward declared up here and
	 * unwrapping a TObjectPtr of an incomplete type is not something to leave to a cast's fallback.
	 */
	UFUNCTION(BlueprintPure, Category = "Button")
	UDreamUIDragSource* GetDragSource() const;

	/**
	 * Whether a pointer is holding this button down right now.
	 *
	 * Asked of the behaviour rather than remembered here: the selectable is what the pointer talks
	 * to, and a second copy of "is it down" on the control would be a flag that goes stale the first
	 * time a press ends somewhere this control does not hear about.
	 */
	UFUNCTION(BlueprintPure, Category = "Button")
	bool IsPressed() const;

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

	UFUNCTION(BlueprintPure, Category = "Button")
	int32 GetAcceptedMouseButtons() const { return AcceptedMouseButtons; }

	/** Writes the bitmask and pushes it onto the behaviour at once, as the three method setters do. */
	UFUNCTION(BlueprintCallable, Category = "Button")
	void SetAcceptedMouseButtons(UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIMouseButtonType")) int32 InAcceptedMouseButtons);

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	/** The hole. Empty is the normal state, and an empty one claims no size -- see RealizeBuiltIn. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UUIButton> ButtonBehaviour = nullptr;

	/** Only while bAllowDragDrop is on; see there for why it is added and destroyed rather than muted. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Button")
	TObjectPtr<UDreamUIDragSource> DragSource = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	/** Add the drag source, or destroy the one this switch added. Idempotent; safe before parts exist. */
	void ApplyDragSource();

	void HandleClicked();
	void HandlePressed();
	void HandleReleased();
	void HandleHovered();
	void HandleUnhovered();
};
