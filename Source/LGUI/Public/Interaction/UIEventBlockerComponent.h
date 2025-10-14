// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerEnterExitInterface.h"
#include "Event/Interface/LexPointerDownUpInterface.h"
#include "Event/Interface/LexPointerClickInterface.h"
#include "Event/Interface/LexPointerDragInterface.h"
#include "Event/Interface/LexPointerDragDropInterface.h"
#include "Event/Interface/LexPointerScrollInterface.h"
#include "Event/Interface/LexPointerSelectDeselectInterface.h"

#include "Components/ActorComponent.h"
#include "UIEventBlockerComponent.generated.h"

//use this component to stop LexPointerEvent bubble up
UCLASS(HideCategories = (Collision, LOD, Physics, Cooking, Rendering, Activation, Actor, Input, Lighting, Mobile), ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIEventBlockerComponent : public UActorComponent
	, public ILexPointerEnterExitInterface
	, public ILexPointerDownUpInterface
	, public ILexPointerClickInterface
	, public ILexPointerDragInterface
	, public ILexPointerDragDropInterface
	, public ILexPointerScrollInterface
	, public ILexPointerSelectDeselectInterface
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, Category = "UIEventBlocker") bool AllowEventBubbleUp = false;
	virtual bool OnPointerEnter_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerExit_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDown_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerUp_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerClick_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerBeginDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerEndDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDragDrop_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerScroll_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerSelect_Implementation(ULexBaseEventData* eventData)override;
	virtual bool OnPointerDeselect_Implementation(ULexBaseEventData* eventData)override;
};
