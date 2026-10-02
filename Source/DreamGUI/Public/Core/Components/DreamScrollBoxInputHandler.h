// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "DreamScrollBoxInputHandler.generated.h"

class UDreamLayoutContainerScrollBox;

/**
 * Wheel and drag input for UDreamLayoutContainerScrollBox.
 *
 * A layout container is a UDreamWidgetSubObjectBehaviour and cannot receive pointer events, so the scrolling
 * gesture lives here and writes back into the layout's scroll offset. Created transiently by the scroll box on
 * register and destroyed with it, so it never ends up serialized into a prefab.
 */
UCLASS(ClassGroup = (DreamGUI), Transient, NotBlueprintable)
class DREAMGUI_API UDreamScrollBoxInputHandler : public UDreamUIBehaviour
	, public IDreamPointerDragInterface
	, public IDreamPointerScrollInterface
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TWeakObjectPtr<UDreamLayoutContainerScrollBox> TargetLayout;

protected:
	virtual void Tick(float DeltaTime) override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData) override;

private:
	/** Scrolls the target along its own orientation. Returns true when the offset actually moved. */
	bool ApplyScroll(float PrimaryDelta) const;
	/**
	 * Whether the drag beginning with InEventData is one the box takes, SScrollBox's rules: a finger only while
	 * touch scrolling is on; the right button only while right-click drag scrolling is on and there is something to
	 * scroll; never the middle button. The left button always, which is this library's own gesture.
	 */
	bool AcceptsDragGesture(const UDreamPointerEventData* InEventData) const;

	/**
	 * Whether this handler took the drag in progress, decided when it began and kept for its whole length: a gesture
	 * refused at the start stays refused, and goes on to whatever is behind the box.
	 */
	bool bDragAccepted = false;
	FVector PrevPointerPosition = FVector::ZeroVector;
	/**
	 * Smoothed drag speed carried into momentum on release. The legacy view sampled only the final
	 * frame, so a single hitchy frame at let-go threw the content across the screen; averaging over
	 * the drag costs nothing and removes that failure entirely.
	 */
	float DragVelocity = 0.0f;
	/** Weight of the newest frame in the running average -- responsive, but not to one bad frame. */
	static constexpr float DragVelocitySmoothing = 0.5f;
};
