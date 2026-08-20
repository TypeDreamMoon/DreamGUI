// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerEnterExitInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerEnterExitInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI pointer enter/exist event
 */
class DREAMGUI_API IDreamPointerEnterExitInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when pointer enter this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerEnter(UDreamPointerEventData* EventData);
	/**
	 * Called when pointer exit this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerExit(UDreamPointerEventData* EventData);
};