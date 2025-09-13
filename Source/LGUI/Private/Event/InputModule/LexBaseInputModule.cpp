// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexBaseInputModule.h"
#include "LGUI.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"

ULexBaseInputModule::ULexBaseInputModule()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void ULexBaseInputModule::ActivateInputModule()
{
	ULexUIManagerWorldSubsystem::SetCurrentInputModule(this);
}
void ULexBaseInputModule::DeactivateInputModule()
{
	ULexUIManagerWorldSubsystem::ClearCurrentInputModule(this);
}
void ULexBaseInputModule::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (this->GetWorld() == nullptr)return;
#if WITH_EDITOR
	if (this->GetWorld()->IsGameWorld())
#endif
	{
		ActivateInputModule();
	}
}
void ULexBaseInputModule::Deactivate()
{
	Super::Deactivate();
	DeactivateInputModule();
}
