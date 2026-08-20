// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamNavigationInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamNavigationInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling keyboard/gamepad navigation event. Only allowed on ActorComponent
 */ 
class DREAMGUI_API IDreamNavigationInterface
{
	GENERATED_BODY()
public:
	/**
	 * @return true if we can navigate from other to this, false otherwise
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	bool CanNavigateHere()const;
	/**
	 * Called when a navigation event occurs.
	 * @param direction navigation direction
	 * @param result navigate next object
	 * @return true if this action can navigation to next, false if no need to navigate to next
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	bool OnNavigate(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result);
};