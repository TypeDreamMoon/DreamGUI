// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamBaseInputModule.generated.h"

class UDreamEventSystem;

/**
 * This is the place for handling inputs.
 * Call RegisterInputModuleToEventSystem to make this work. Only one InputModule is valid in the same time.
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamBaseInputModule : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamBaseInputModule();

	virtual void ProcessInput() PURE_VIRTUAL(, );
	virtual void ClearEvent() PURE_VIRTUAL(, );

	/**
	 * Register this InputModule to a EventSystem. Only one InputModule is valid in the same time.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RegisterInputModuleToEventSystem(UDreamEventSystem* TargetEventSystem);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void UnregisterInputModuleFromEventSystem();
protected:
	UPROPERTY(Transient)TWeakObjectPtr<UDreamEventSystem> EventSystem = nullptr;
};