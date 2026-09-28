// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIInputServices.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

UDreamUIInputServices* UDreamUIInputServices::Get(const UObject* InWorldContext)
{
	// GetSubsystem with the abstract class finds the subclass the input system declares -- the collection
	// answers a base-class query with the first subsystem derived from it.
	const UWorld* World = GEngine != nullptr && InWorldContext != nullptr
		? GEngine->GetWorldFromContextObject(InWorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetSubsystem<UDreamUIInputServices>() : nullptr;
}
