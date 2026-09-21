// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerCallback.generated.h"

/**
 * A tween with no value and no length: it exists to fire once, at the moment the clock reaches it.
 *
 * This is what UDreamTweenerSequence::AppendCallback and InsertCallback put on the timeline, and it
 * is a tween rather than a side list because everything a sequence already does to its children --
 * position it, flip it for a yoyo, rewind it for a restart, count it as finished -- is then the same
 * for a callback as for anything else. A zero duration means the first step past its delay lands on
 * "the cycle is over", which runs the completion callbacks and retires it.
 */
UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerCallback : public UDreamTweener
{
	GENERATED_BODY()
public:
	void SetInitialValue()
	{
		this->duration = 0.0f;
	}
protected:
	virtual void OnStartGetValue() override {}
	virtual void TweenAndApplyValue(float currentTime) override {}
	virtual void SetValueForIncremental() override {}
	virtual void SetOriginValueForRestart() override {}
};
