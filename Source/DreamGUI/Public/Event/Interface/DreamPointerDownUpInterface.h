// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerDownUpInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerDownUpInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI trigger press or release event
 */
class DREAMGUI_API IDreamPointerDownUpInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when a pointer press event occurs.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerDown(UDreamPointerEventData* EventData);
	/**
	 * Called when a pointer release event occurs.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerUp(UDreamPointerEventData* EventData);
};