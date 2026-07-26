// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIBehaviour.h"
#include "Event/Interface/LexPointerDragInterface.h"
#include "Event/Interface/LexPointerScrollInterface.h"
#include "LexScrollBoxInputHandler.generated.h"

class ULexLayoutContainerScrollBox;

/**
 * Wheel and drag input for ULexLayoutContainerScrollBox.
 *
 * A layout container is a ULexWidgetSubObjectBehaviour and cannot receive pointer events, so the scrolling
 * gesture lives here and writes back into the layout's scroll offset. Created transiently by the scroll box on
 * register and destroyed with it, so it never ends up serialized into a prefab.
 */
UCLASS(ClassGroup = (LGUI), Transient, NotBlueprintable)
class LGUI_API ULexScrollBoxInputHandler : public ULexUIBehaviour
	, public ILexPointerDragInterface
	, public ILexPointerScrollInterface
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TWeakObjectPtr<ULexLayoutContainerScrollBox> TargetLayout;

protected:
	virtual bool OnPointerBeginDrag_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerDrag_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerScroll_Implementation(ULexPointerEventData* EventData) override;

private:
	/** Scrolls the target along its own orientation. Returns true when the offset actually moved. */
	bool ApplyScroll(float PrimaryDelta) const;

	FVector PrevPointerPosition = FVector::ZeroVector;
};
