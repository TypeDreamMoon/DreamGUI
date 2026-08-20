// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerClickInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerClickInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI click event
 */ 
class DREAMGUI_API IDreamPointerClickInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when a click event occurs.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerClick(UDreamPointerEventData* EventData);
};