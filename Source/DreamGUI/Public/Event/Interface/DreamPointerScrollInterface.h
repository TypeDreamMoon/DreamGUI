// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerScrollInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerScrollInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI pointer scroll event
 */
class DREAMGUI_API IDreamPointerScrollInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when pointer inside this object and scroll(mouse wheel).
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerScroll(UDreamPointerEventData* EventData);
};