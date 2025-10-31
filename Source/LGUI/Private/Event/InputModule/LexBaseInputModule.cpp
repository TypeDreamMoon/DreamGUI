// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexBaseInputModule.h"
#include "Engine/World.h"
#include "Event/LexEventSystem.h"

ULexBaseInputModule::ULexBaseInputModule()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void ULexBaseInputModule::RegisterInputModuleToEventSystem(ULexEventSystem* TargetEventSystem)
{
	EventSystem = TargetEventSystem;
	EventSystem->SetInputModule(this);
}

void ULexBaseInputModule::UnregisterInputModuleFromEventSystem()
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

