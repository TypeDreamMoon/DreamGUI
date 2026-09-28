// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamBaseInputModule.h"
#include "Event/DreamEventSystem.h"
#include "DreamGUI.h"

UDreamBaseInputModule::UDreamBaseInputModule()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamBaseInputModule::RegisterInputModuleToEventSystem(UDreamEventSystem* TargetEventSystem)
{
	//BlueprintCallable, so the argument is whatever a graph handed over -- including the null a failed
	//Get Event System node produces
	if (!IsValid(TargetEventSystem))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d TargetEventSystem is not valid; this input module will not be registered."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
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

