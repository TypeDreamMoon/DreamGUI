// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerSelectDeselectInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerSelectDeselectInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI select/deselect event
 */
class DREAMGUI_API IDreamPointerSelectDeselectInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when DreamUI EventSystem select this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerSelect(UDreamBaseEventData* EventData);
	/**
	 * Called when DreamUI EventSystem deselect this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerDeselect(UDreamBaseEventData* EventData);
};