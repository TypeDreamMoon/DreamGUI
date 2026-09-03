// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_DreamUIPlayAnimation.generated.h"

/**
 * "Play Animation with Finished event": the async form of UDreamUserWidget::PlayAnimation, with a
 * Finished exec pin. The node is nothing but a pointer at the proxy factory on
 * UDreamUIAnimationPlayCallbackProxy; UK2Node_BaseAsyncTask builds the pins from that function's
 * signature and wires the proxy's delegates to the exec outputs.
 */
UCLASS()
class UK2Node_DreamUIPlayAnimation : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_DreamUIPlayAnimation(const FObjectInitializer& ObjectInitializer);
};

/** The time-range form of the node above. */
UCLASS()
class UK2Node_DreamUIPlayAnimationTimeRange : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_DreamUIPlayAnimationTimeRange(const FObjectInitializer& ObjectInitializer);
};
