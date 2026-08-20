// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamBaseInputModule.h"
#include "Event/DreamEventSystem.h"

UDreamBaseInputModule::UDreamBaseInputModule()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamBaseInputModule::RegisterInputModuleToEventSystem(UDreamEventSystem* TargetEventSystem)
{
	EventSystem = TargetEventSystem;
	EventSystem->SetInputModule(this);
}

void UDreamBaseInputModule::UnregisterInputModuleFromEventSystem()
{
	if (EventSystem.IsValid())
	{
		if (EventSystem->GetCurrentInputModule() == this)
		{
			EventSystem->ClearInputModule();
			EventSystem = nullptr;
		}
	}
}

