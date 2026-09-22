// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Extensions/DreamUMGWidget.h"
#include "DreamCrosscuttingTestTypes.generated.h"

/**
 * Reaches UDreamUMGWidget's two protected time/draw decisions from a test.
 *
 * Both of them used to read GetWorld() as a bare dereference, and the state that makes that a crash
 * -- a component with no world -- is precisely the state a headless test builds by default. There is
 * no public way in: ShouldDrawWidget and GetCurrentTime are protected because their caller is the
 * component's own tick, and the tick needs a render target. A subclass is the smallest thing that
 * can ask them the question.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamUMGWidgetTimingProbe : public UDreamUMGWidget
{
	GENERATED_BODY()
public:
	/** The branch that used to be `GetWorld()->GetTimeSeconds()` with nothing in front of it. */
	void UseGameTime() { TimingPolicy = EWidgetTimingPolicy::GameTime; }
	void UseRealTime() { TimingPolicy = EWidgetTimingPolicy::RealTime; }

	double CallGetCurrentTime() const { return GetCurrentTime(); }
	bool CallShouldDrawWidget() const { return ShouldDrawWidget(); }
};
