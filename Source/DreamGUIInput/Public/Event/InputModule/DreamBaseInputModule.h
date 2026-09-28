// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamBaseInputModule.generated.h"

class UDreamEventSystem;
class UDreamUIInputUser;

/**
 * Where a player's input comes in: the entry points an input source -- a preset actor's bindings, a test's driver --
 * calls, feeding the player behind the event system this is registered to.
 * Call RegisterInputModuleToEventSystem to make this work. Only one InputModule is valid in the same time.
 */
UCLASS(Abstract)
class DREAMGUIINPUT_API UDreamBaseInputModule : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamBaseInputModule();

	/**
	 * One frame of the player's input: the player's pipeline (UDreamUIInputUser::RunPipeline). The input subsystem
	 * calls this for every player with a module; a subclass adds to the frame around Super.
	 */
	virtual void ProcessInput();
	/** Let go of every pointer of the player: each hover exited, each press let go, each drag ended. */
	virtual void ClearEvent();

	/**
	 * Register this InputModule to a EventSystem. Only one InputModule is valid in the same time.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RegisterInputModuleToEventSystem(UDreamEventSystem* TargetEventSystem);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void UnregisterInputModuleFromEventSystem();

	/** The player this module feeds: the one the event system it is registered to speaks for. */
	UDreamUIInputUser* GetInputUser() const;
protected:
	UPROPERTY(Transient)TWeakObjectPtr<UDreamEventSystem> EventSystem = nullptr;
};
