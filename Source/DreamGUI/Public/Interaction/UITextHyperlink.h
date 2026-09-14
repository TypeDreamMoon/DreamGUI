// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerClickInterface.h"
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
 * Put it on the same widget as the UDreamText (it finds the visual itself), and give the widget
 * something that raises clicks at all -- the event system needs a raycast target, the same way a
 * UUIButton does.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUITextHyperlink : public UDreamUIBehaviour, public IDreamPointerClickInterface
{
	GENERATED_BODY()
public:
	/** The text this reads its links from: the widget's own text visual unless one is set here. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	UDreamText* GetTextVisual()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Hyperlink")
	void SetTextVisual(UDreamText* Value);
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	TObjectPtr<UDreamText> TextVisual = nullptr;
	/**
	 * Let the click bubble up when it did not land on a link. False keeps every click on the text,
	 * which is what a text inside a button does NOT want: the button would stop working.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Hyperlink")
	bool bAllowEventBubbleUpWhenNoLinkHit = true;

	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
};
