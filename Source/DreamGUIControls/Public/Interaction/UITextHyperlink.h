// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "UITextHyperlink.generated.h"

class UDreamText;

/**
 * Makes the `<a=Id>...</a>` ranges of a rich text clickable.
 *
 * The text visual holds the ranges -- a hyperlink is a custom tag that can be clicked -- but a visual
 * never sees pointer events; a behaviour does. So this is the piece between them: it takes the click's
 * world point, asks the text which link is under it, and broadcasts. A click that lands on the text but
 * not on a link is not this component's, so the event bubbles up as if nothing were here.
 *
 * It can also colour the link under the pointer while it is hovered and while it is pressed, the way
 * UMG's hyperlink decorator styles its hovered and pressed states. Both are off by default. The colour
 * is the text's tag colour override, which is applied when the text is painted, so a link lighting up
 * costs a repaint and never a layout.
 *
 * Put it on the same widget as the UDreamText (it finds the visual itself), and give the widget
 * something that raises clicks at all -- the event system needs a raycast target, the same way a
 * UUIButton does.
 *
 * Each link is also a stop for the keys and the pad, as UMG's is: the hyperlink decorator draws a link
 * as an SHyperlink, which is an SButton (Slate/Public/Widgets/Input/SHyperlink.h), focusable like any
 * button (SButton's IsFocusable). In a game, while bLinksAreFocusStops is on, every link drawn gets a
 * stop of its own over its glyphs -- a child widget with a button on it, which the D-pad and Tab reach
 * where the link is drawn, the focus ring is drawn around, and whose confirm announces the link. A link
 * in a disabled text is a disabled button: passed over, and the confirm announces nothing.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUICONTROLS_API UUITextHyperlink : public UDreamUIBehaviour, public IDreamPointerClickInterface,
	public IDreamPointerEnterExitInterface, public IDreamPointerDownUpInterface
{
	GENERATED_BODY()
public:
	UUITextHyperlink();
	/** The text this reads its links from: the widget's own text visual unless one is set here. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	UDreamText* GetTextVisual()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetTextVisual(UDreamText* Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	bool GetUseHoverColor()const { return bUseHoverColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetUseHoverColor(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	FColor GetHoverColor()const { return HoverColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetHoverColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	bool GetUsePressedColor()const { return bUsePressedColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetUsePressedColor(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	FColor GetPressedColor()const { return PressedColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetPressedColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	bool GetLinksAreFocusStops()const { return bLinksAreFocusStops; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetLinksAreFocusStops(bool Value);
	/**
	 * The stop over the link at InTagIndex of the text's GetRichTextCustomTagArray -- the widget the keys and the pad focus
	 * for it -- or null while it has none (outside a game, with bLinksAreFocusStops off, or a link not drawn).
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	UDreamWidget* GetLinkStop(int32 InTagIndex)const;
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	TObjectPtr<UDreamText> TextVisual = nullptr;
	/**
	 * Let the click bubble up when it did not land on a link. False keeps every click on the text,
	 * which is what a text inside a button does NOT want: the button would stop working.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	bool bAllowEventBubbleUpWhenNoLinkHit = true;
	/** Draw the link under the pointer in HoverColor while it is there. Off: links look the same hovered or not. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	bool bUseHoverColor = false;
	/** What the hovered link is drawn in, in place of the colour its markup gives it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink", meta = (EditCondition = "bUseHoverColor"))
	FColor HoverColor = FColor(96, 168, 255, 255);
	/**
	 * Draw the link a press went down on in PressedColor until the press comes up, wherever the pointer goes in
	 * between, as a browser shows a link :active. Off: a press changes nothing about how the link looks.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	bool bUsePressedColor = false;
	/** What the pressed link is drawn in, in place of the colour its markup gives it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink", meta = (EditCondition = "bUsePressedColor"))
	FColor PressedColor = FColor(48, 112, 208, 255);
	/**
	 * Each link drawn is a stop for Tab, the D-pad and the stick, its confirm announcing the link -- UMG's hyperlink is an
	 * SButton, focusable as any button is. Off: the keys and the pad pass the links by, and only a pointer follows them.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	bool bLinksAreFocusStops = true;

	virtual void Tick(float DeltaTime)override;
	virtual void OnEnable()override;
	virtual void OnDisable()override;
	virtual void OnUnregister()override;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
private:
	/**
	 * The pointer over the text, while there is one. Moving from one link to the next, or onto a link from the
	 * text around it, raises no event -- the pointer never leaves the widget -- so its hit point is followed
	 * every frame for as long as it is here.
	 */
	TWeakObjectPtr<UDreamPointerEventData> HoverPointer;
	/** Where HoverPointer was last looked up, so a pointer that has not moved costs no hit test. */
	FVector LastHoverWorldPoint = FVector::ZeroVector;
	/** The text's layout count when HoverPointer was last looked up: a text laid out again is looked up again. */
	int32 LastLayoutRunCount = 0;
	/** The link under HoverPointer, as an index into the text's GetRichTextCustomTagArray; INDEX_NONE for none. */
	int32 HoveredLinkIndex = INDEX_NONE;
	/** The link a press went down on and has not come up from; INDEX_NONE for none. */
	int32 PressedLinkIndex = INDEX_NONE;
	/** The tag this behaviour has recoloured, and on which text, so it can be given back; INDEX_NONE for none. */
	int32 OverriddenLinkIndex = INDEX_NONE;
	TWeakObjectPtr<UDreamText> OverriddenText;
	/** The link under InWorldPoint on the text, or INDEX_NONE. */
	int32 FindLinkIndexAt(const FVector& InWorldPoint)const;
	/** How many times the text has been laid out, a changed text laid out first; 0 without a text. */
	int32 GetTextLayoutRunCount()const;
	/** Put the colour the hover and the press call for on the link they are on, taking it off the one it was on. */
	void RefreshLinkColor();
	/** Tick while a pointer is over the text with a hover colour to show, and while the links are stops in a game. */
	void UpdateTickEnabled();

	/** Whether the links get stops here: a game's world, the switch on, this behaviour enabled. */
	bool WantsLinkStops()const;
	/**
	 * One stop per link, each sized and placed over its glyphs as they are drawn now -- put away while its link is not
	 * drawn; stops for links gone are taken down. Every frame in a game, and when the switch is turned on.
	 */
	void RefreshLinkStops();
	/** Take every stop down. */
	void DestroyLinkStops();
	/**
	 * The stops, each a child of the text's widget with a button on it, ignored by layout and never hit by a pointer: the
	 * pointer reaches the links through the text itself. Paired by position with LinkStopTagIndices.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidget>> LinkStops;
	/** The tag each stop in LinkStops is over, an index into the text's GetRichTextCustomTagArray. */
	TArray<int32> LinkStopTagIndices;
};
