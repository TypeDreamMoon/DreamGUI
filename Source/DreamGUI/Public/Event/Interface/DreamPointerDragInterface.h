// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerDragInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerDragInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI beginDrag drag endDrag event
 */
class DREAMGUI_API IDreamPointerDragInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when drag this object begin.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerBeginDrag(UDreamPointerEventData* EventData);
	/**
	 * Called when dragging this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerDrag(UDreamPointerEventData* EventData);
	/**
	 * Called when drag this object end.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerEndDrag(UDreamPointerEventData* EventData);
};