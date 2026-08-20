// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IDreamUICultureChangedInterface.generated.h"


/**
 * Interface for DreamGUI Widget to handle culture change event.
 * Need to register UObject with RegisterDreamGUICultureChangedEvent, check UIText for reference
 */
UINTERFACE(Blueprintable, MinimalAPI)
class UDreamUICultureChangedInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for DreamGUI Widget to handle culture change event.
 * Need to register UObject with RegisterDreamGUICultureChangedEvent, check UIText for reference
 */
class DREAMGUI_API IDreamUICultureChangedInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when current culture changed and DreamGUI need to update culture.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		void OnCultureChanged();
};