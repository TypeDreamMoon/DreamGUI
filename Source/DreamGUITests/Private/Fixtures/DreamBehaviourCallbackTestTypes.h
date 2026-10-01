// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Interaction/DreamContentWidget.h"
#include "DreamBehaviourCallbackTestTypes.generated.h"

/**
 * A named slot whose C++ does something on Tick and on a move. UDreamNamedSlot declared both unused
 * (UDreamUIBehaviour::DeclareTickUnused), and that declaration covers its own class alone: a subclass
 * that overrides either callback must be called as though nothing had been declared.
 */
UCLASS()
class UDreamCountingNamedSlot : public UDreamNamedSlot
{
	GENERATED_BODY()

public:
	int32 TickCount = 0;
	int32 TransformChangedCount = 0;

protected:
	virtual void Tick(float DeltaTime) override { Super::Tick(DeltaTime); ++TickCount; }
	virtual void OnTransformChanged() override { Super::OnTransformChanged(); ++TransformChangedCount; }
};
