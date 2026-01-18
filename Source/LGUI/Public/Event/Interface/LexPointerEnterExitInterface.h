// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/LexPointerEventData.h"
#include "LexPointerEnterExitInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class ULexPointerEnterExitInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling LexUI pointer enter/exist event
 */
class LGUI_API ILexPointerEnterExitInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when pointer enter this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI)
		bool OnPointerEnter(ULexPointerEventData* EventData);
	/**
	 * Called when pointer exit this object.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI)
		bool OnPointerExit(ULexPointerEventData* EventData);
};