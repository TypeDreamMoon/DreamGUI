// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Event/DreamPointerEventData.h"
#include "DreamBorder.generated.h"

class UDreamWidget;
class UTexture2D;
class UUIEventTrigger;

/**
 * A pointer moment on a border, carrying the whole event -- which button, where, how many clicks.
 *
 * Its own type rather than UUIEventTrigger's: a consumer binds to the CONTROL, and a delegate named
 * after the component the control happens to use inside itself would leak that choice into every
 * Blueprint that listens.
 *
 * UMG's four mouse events hand back an FEventReply, so a handler decides per call whether the event
 * carries on to whatever is behind. Nothing in this event system asks a handler that question: an
 * element says once, for all its events, whether they bubble (UUIEventTrigger::AllowEventBubbleUp).
 * So the reply becomes a property here -- bConsumeMouseEvents -- and the events are plain multicasts,
 * which is also what lets several listeners share one border.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamBorderPointerEvent, UDreamPointerEventData*, PointerEvent);

/**
 * A border whose hierarchy is code, not an asset: a face, a padding, and a hole to put things in.
 *
 * UMG's Border is a single-child panel that draws a brush behind whatever it holds, and this library
 * already offers that SHAPE as a palette panel -- an overlay carrying an image visual. This is the
 * CONTROL spelling, and the difference is the one that runs through this whole family: a panel's
 * brush, colour and padding are authored per instance, and a control's come from the project style
 * sheet, so restyling a project's boxes is one edit rather than one per box.
 *
 * SEVERAL CHILDREN, deliberately, where UMG's border takes one. The face carries an overlay, so a
 * `Native.Border { A B }` stacks A under B with both filling the padded area -- which is what an
 * overlay means and what a reader of those two lines expects. UDreamScrollBox's content slot makes
 * the same call for the same reason; a hole that took exactly one child would send every second
 * child somewhere invisible.
 *
 * The outline is part of the face rather than a second widget: UDreamRectBlock draws a border of its
 * own, so BorderThickness and BorderColor are pushed into the rect and cost no node. Zero thickness
 * is UMG's plain border, which is what the style ships.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Border")
class DREAMGUI_API UDreamBorder : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Border")
	FDreamBorderStyle Style;

	/**
	 * Where the content sits inside the padded area -- UMG's HorizontalAlignment / VerticalAlignment.
	 *
	 * On the CONTROL rather than in the style, because it is layout rather than look: two borders in
	 * one project sharing a sheet routinely hold different things, and where a thing sits is that
	 * thing's business. Fill is what this control has always arranged, so an existing border does not
	 * move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetHorizontalAlignment", BlueprintSetter = "SetHorizontalAlignment", Category = "Border")
	EDreamPanelHorizontalAlignment HorizontalAlignment = EDreamPanelHorizontalAlignment::Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVerticalAlignment", BlueprintSetter = "SetVerticalAlignment", Category = "Border")
	EDreamPanelVerticalAlignment VerticalAlignment = EDreamPanelVerticalAlignment::Fill;

	/**
	 * A tint over the style's background colour -- UMG's BrushColor, which is a tint over SBorder's
	 * brush for the same reason: the style says what a border looks like, and this says what is
	 * happening to this one right now. White is no opinion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetBrushColor", BlueprintSetter = "SetBrushColor", Category = "Border")
	FColor BrushColor = FColor::White;

	/**
	 * A tint over the CONTENT -- UMG's ContentColorAndOpacity, of which the alpha is the half this
	 * framework can honour.
	 *
	 * The alpha is pushed to the content node's RenderOpacity, which is the one channel that
	 * CASCADES here: it multiplies down the whole subtree, so fading a border fades everything in it
	 * whatever each thing inside is drawn with. That is UMG's opacity half exactly, and the half
	 * nearly every caller wants (a panel fading in, a section greying out).
	 *
	 * The colour half has no counterpart and is deliberately not faked: a colour over a subtree would
	 * have to be WRITTEN onto each visual inside, overwriting whatever the host authored there, and
	 * the original could not be recovered on the next push. Tint the things inside, or put the border
	 * under something that fades.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetContentColorAndOpacity", BlueprintSetter = "SetContentColorAndOpacity", Category = "Border")
	FLinearColor ContentColorAndOpacity = FLinearColor::White;

	/**
	 * Whether this border reports the pointer -- UMG's four mouse events, off by default.
	 *
	 * Off costs nothing: no component is made at all. It is off rather than on because a border that
	 * starts listening is a border that starts CONSUMING (see bConsumeMouseEvents), and every border
	 * already in a project would quietly stop letting clicks through to whatever is behind it.
	 *
	 * The face is already a raycast target, so turning this on changes what LISTENS, not what is hit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetReportMouseEvents", BlueprintSetter = "SetReportMouseEvents", Category = "Border")
	bool bReportMouseEvents = false;

	/**
	 * Whether a reported pointer event stops here -- this framework's spelling of UMG's Handled.
	 *
	 * True (the default, and the event trigger's own) is Handled: the click is this border's and
	 * nothing behind it hears about it. False is Unhandled: the border listens and lets the event
	 * carry on, which is what a decorative frame over a clickable thing wants.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetConsumeMouseEvents", BlueprintSetter = "SetConsumeMouseEvents", Category = "Border", meta = (EditCondition = "bReportMouseEvents"))
	bool bConsumeMouseEvents = true;

	/**
	 * The four moments UMG's border speaks, re-broadcast from the pointer system.
	 *
	 * Silent unless bReportMouseEvents is on -- binding alone does not wake them, because a control
	 * cannot tell when a Blueprint binds and guessing wrong would mean creating the component (and
	 * consuming clicks) for every border in every project.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Border")
	FDreamBorderPointerEvent OnMouseButtonDownEvent;

	UPROPERTY(BlueprintAssignable, Category = "Border")
	FDreamBorderPointerEvent OnMouseButtonUpEvent;

	/** Pointer MOTION over the border, which this system reports as the drag it is part of. */
	UPROPERTY(BlueprintAssignable, Category = "Border")
	FDreamBorderPointerEvent OnMouseMoveEvent;

	/**
	 * The second press of a double click, and the fourth, and the sixth -- said at that PRESS, and in
	 * place of OnMouseButtonDownEvent for it, which is how UBorder reports one: Slate routes the second
	 * press to OnMouseButtonDoubleClick and never to OnMouseButtonDown, so a double click here is one
	 * button down, one double click and two button ups.
	 *
	 * The event system decides what counts as a double (its DoubleClickTime, the same widget and button,
	 * and a second press within the pointer's drag threshold of the first); this re-broadcasts that
	 * decision rather than making a second one that could disagree with it.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Border")
	FDreamBorderPointerEvent OnMouseDoubleClickEvent;

	/** The face: what draws the brush, the tint and the outline, and what clips to the rounding. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Border")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	/** The hole. Whatever a host nests on this control ends up here. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Border")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;

	/** Where things go. Parent into this, or nest in `.dui`, and the padding holds them off the edge. */
	UFUNCTION(BlueprintCallable, Category = "Border")
	UDreamWidget* GetContentNode() const { return ContentNode; }

	UFUNCTION(BlueprintPure, Category = "Border")
	FDreamBorderStyle GetStyle() const { return Style; }

	/** This instance's whole look, replaced and pushed. See UDreamButton::SetStyle for the caveat. */
	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetStyle(const FDreamBorderStyle& InStyle);

	UFUNCTION(BlueprintPure, Category = "Border")
	EDreamPanelHorizontalAlignment GetHorizontalAlignment() const { return HorizontalAlignment; }

	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetHorizontalAlignment(EDreamPanelHorizontalAlignment InHorizontalAlignment);

	UFUNCTION(BlueprintPure, Category = "Border")
	EDreamPanelVerticalAlignment GetVerticalAlignment() const { return VerticalAlignment; }

	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetVerticalAlignment(EDreamPanelVerticalAlignment InVerticalAlignment);

	UFUNCTION(BlueprintPure, Category = "Border")
	FColor GetBrushColor() const { return BrushColor; }

	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetBrushColor(FColor InBrushColor);

	/**
	 * What holds the content off the edge, read from the style in EFFECT rather than from the field
	 * beside it -- so a border driven by the project sheet answers the number it is actually drawn
	 * with rather than the one this instance happens to be carrying.
	 */
	UFUNCTION(BlueprintPure, Category = "Border")
	FMargin GetPadding() const;

	/**
	 * Every appearance setter below edits THIS INSTANCE'S style and re-pushes, because the padding,
	 * the brush and the rest are one quantity and the style is where it lives -- a second copy on the
	 * control is how a control and its style learn to disagree.
	 *
	 * Which means they show up when this instance's style is what is in effect: StyleSource Inline,
	 * or Override with that field ticked, or a project with no sheet at all. Under a sheet they are
	 * writes to a value the sheet is overruling, and the getters above say so by answering the
	 * resolved number.
	 */
	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetPadding(FMargin InPadding);

	/** The face's skin, read from the style in effect. */
	UFUNCTION(BlueprintPure, Category = "Border")
	FDreamUIFaceBrush GetBrush() const;

	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetBrush(const FDreamUIFaceBrush& InBrush);

	/**
	 * The common case of the above, and the one UMG spells: put this texture on the face and leave
	 * every other thing about the brush where it was.
	 */
	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetBrushFromTexture(UTexture2D* InTexture);

	/**
	 * The other thing a face brush takes: an atlas sprite. UMG has no call for it because Slate has
	 * no atlas; here it is the cheaper of the two, so leaving it unreachable would make the
	 * expensive road the only one a caller could find.
	 */
	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetBrushFromSprite(UDreamUISpriteData_BaseObject* InSprite);

	UFUNCTION(BlueprintPure, Category = "Border")
	FLinearColor GetContentColorAndOpacity() const { return ContentColorAndOpacity; }

	/** See the property: the alpha cascades over the content, the colour has nowhere to go. */
	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetContentColorAndOpacity(FLinearColor InContentColorAndOpacity);

	UFUNCTION(BlueprintPure, Category = "Border")
	bool GetReportMouseEvents() const { return bReportMouseEvents; }

	/** Makes or unmakes the listener on the face. Off destroys it, so an off border costs nothing. */
	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetReportMouseEvents(bool bInReportMouseEvents);

	UFUNCTION(BlueprintPure, Category = "Border")
	bool GetConsumeMouseEvents() const { return bConsumeMouseEvents; }

	UFUNCTION(BlueprintCallable, Category = "Border")
	void SetConsumeMouseEvents(bool bInConsumeMouseEvents);

	/** The listener on the face, or null while bReportMouseEvents is off. */
	UFUNCTION(BlueprintPure, Category = "Border")
	UUIEventTrigger* GetEventTrigger() const;

	virtual void ApplyStyle() override;

	/** The listener, while there is one. Transient: a component is remade with the tree, not loaded. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Border")
	TObjectPtr<UUIEventTrigger> EventTrigger = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	/** Re-broadcast, one per moment, each from the event system's own route for it. */
	void HandlePointerDown(UDreamPointerEventData* InEventData);
	void HandlePointerUp(UDreamPointerEventData* InEventData);
	void HandlePointerDrag(UDreamPointerEventData* InEventData);
	void HandlePointerDoubleClick(UDreamPointerEventData* InEventData);

	/** Makes the trigger and binds it, or destroys it. The one place either of those happens. */
	void ApplyMouseEventReporting();
};
