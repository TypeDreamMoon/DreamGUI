// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
#include "UISelectable.h"
#include "Event/DreamUIEventDelegate.h"
#include "UIButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonClickedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonSimpleEvent);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIButton : public UUISelectable, public IDreamPointerClickInterface, public IDreamPointerDoubleClickInterface
{
	GENERATED_BODY()
protected:

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Button")
	FDreamUIEventDelegate OnClick = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty);
	FSimpleMulticastDelegate OnClickCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Toggle", DisplayName="OnClick")
	FUIButtonClickedEvent OnClickBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnHovered")
	FUIButtonSimpleEvent OnHoveredBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnUnhovered")
	FUIButtonSimpleEvent OnUnhoveredBP;
	/**
	 * The C++ counterparts of the two above, and they exist for the same reason OnClickCPP does:
	 * a native control wires its parts from C++, and a dynamic delegate can only carry a UFUNCTION
	 * with no arguments -- so a control with N of these buttons could not tell WHICH one spoke.
	 * A weak lambda per button can, which is how UDreamRingMenu knows the pointer left wedge 3.
	 */
	FSimpleMulticastDelegate OnHoveredCPP;
	FSimpleMulticastDelegate OnUnhoveredCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnPressed")
	FUIButtonSimpleEvent OnPressedBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnReleased")
	FUIButtonSimpleEvent OnReleasedBP;
	/**
	 * The C++ counterparts of the press pair, added for the reason the hover ones were: a native
	 * control wires its parts from C++ and can bind nothing to a dynamic delegate, so the two press
	 * moments reached Blueprint through the behaviour and stopped dead at the control layer --
	 * UDreamButton re-broadcast a click and had nothing to say about a press.
	 */
	FSimpleMulticastDelegate OnPressedCPP;
	FSimpleMulticastDelegate OnReleasedCPP;
	/**
	 * Two clicks inside the event system's DoubleClickTime, on this button.
	 *
	 * The clock is the event system's and nobody else's: UUITextInput already takes its
	 * select-the-word gesture from that one, and a control that measured its own would disagree with
	 * the field beside it on what "a double click" is -- and would be measuring click EVENTS rather
	 * than pointer presses, which is not the same thing once anything else can raise a click.
	 * UDreamListViewBase's OnItemDoubleClicked rides this.
	 *
	 * The single click still fires first, which is the interface's stated contract: a list row that
	 * opens on a double click almost always selects on the first one too.
	 */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnDoubleClick")
	FUIButtonSimpleEvent OnDoubleClickBP;
	FSimpleMulticastDelegate OnDoubleClickCPP;
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;

	/**
	 * Feedback, then all three click delegates. The one body every ClickMethod ends in -- see the
	 * definition for why it is a function rather than four lines repeated in three handlers.
	 */
	void FireClick();
	virtual bool OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)override;
public:
	FSimpleMulticastDelegate& GetOnClickEvent(){return OnClickCPP;}
	FSimpleMulticastDelegate& GetOnDoubleClickEvent(){return OnDoubleClickCPP;}
	FSimpleMulticastDelegate& GetOnHoveredEvent(){return OnHoveredCPP;}
	FSimpleMulticastDelegate& GetOnUnhoveredEvent(){return OnUnhoveredCPP;}
	FSimpleMulticastDelegate& GetOnPressedEvent(){return OnPressedCPP;}
	FSimpleMulticastDelegate& GetOnReleasedEvent(){return OnReleasedCPP;}
};
