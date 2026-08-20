// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerDragDropInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerDragDropInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI drag->drop event
 */
class DREAMGUI_API IDreamPointerDragDropInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when dragging another object and drop on this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerDragDrop(UDreamPointerEventData* EventData);
};