// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerSequence.generated.h"

UCLASS(BlueprintType)
class DREAMTWEEN_API UDreamTweenerSequence:public UDreamTweener
{
	GENERATED_BODY()
private:
	UPROPERTY(VisibleAnywhere, Category = DreamTween)TArray<TObjectPtr<UDreamTweener>> tweenerList;
	UPROPERTY(VisibleAnywhere, Category = DreamTween)TArray<TObjectPtr<UDreamTweener>> finishedTweenerList;
	float lastTweenStartTime = 0;
	/**
	 * How many Goto or Restart calls on this sequence are on the stack, one inside another. A child's
	 * callback that seeks the sequence past its own position runs a pass that reaches the callback
	 * again, which seeks again; the depth is what stops that before the stack runs out.
	 */
	int32 seekDepth = 0;
	/**
	 * Put a tween that is joining this sequence onto the sequence's clock, so it plays from the
	 * position the sequence just gave it rather than from wherever its own run had got to.
	 */
	void AdoptTweenerClock(UDreamTweener* tweener);
	/**
	 * Drop the children that are gone: collected (the list holds null), garbage, or made on an object that
	 * is. Children are often made on other objects than the sequence -- a widget verb outers its tween to
	 * the widget -- and streaming out a level, loading a map or destroying a widget marks those garbage
	 * while the sequence lives on; every loop over the children dereferenced them unchecked.
	 */
	void RemoveInvalidChildren();
	/** Reject an ancestor (including this sequence) before adopting a child's clock or changing any lists. */
	bool WouldCreateCycle(const UDreamTweener* InTweener) const;
	/** Whether a Goto or Restart may run, refusing (with an error) one nested too deep; see seekDepth. */
	bool CanSeekFromHere()const;
	/** Every child back on the list, sorted, its value put back at its start and its clock at zero: what Restart and Goto both begin with. */
	void RewindChildrenToStart();
	/** The one implementation behind all four callback entry points. */
	UDreamTweenerSequence* InsertCallbackInternal(float timePosition, const TFunction<void()>& callback);
public:
	/**
	 * Adds the given tween to the end of the Sequence.
	 * Not support Tweener type: Delay/ DelayFrame/ Virtual.
	 * Has no effect if the Sequence has already started.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		UDreamTweenerSequence* Append(UObject* WorldContextObject, UDreamTweener* tweener);
	/**
	 * Adds the given interval to the end of the Sequence.
	 * Has no effect if the Sequence has already started.
	 * @param interval The interval duration
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		UDreamTweenerSequence* AppendInterval(UObject* WorldContextObject, float interval);
	/**
	 * Inserts the given tween at the given time position in the Sequence, automatically adding an interval if needed.
	 * Not support Tweener type: Delay/ DelayFrame/ Virtual.
	 * Has no effect if the Sequence has already started.
	 * @param timePosition The time position where the tween will be placed
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		UDreamTweenerSequence* Insert(UObject* WorldContextObject, float timePosition, UDreamTweener* tweener);
	/**
	 * Adds the given tween to the beginning of the Sequence, pushing forward the other nested content.
	 * Not support Tweener type: Delay/ DelayFrame/ Virtual.
	 * Has no effect if the Sequence has already started.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		UDreamTweenerSequence* Prepend(UObject* WorldContextObject, UDreamTweener* tweener);
	/**
	 * Adds the given interval to the beginning of the Sequence, pushing forward the other nested content.
	 * Has no effect if the Sequence has already started.
	 * @param interval The interval duration
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		UDreamTweenerSequence* PrependInterval(UObject* WorldContextObject, float interval);
	/**
	 * Inserts the given tween at the same time position of the last tween added to the Sequence.
	 * Note that, in case of a Join after an interval, the insertion time will be the time where the interval starts, not where it finishes.
	 * Not support Tweener type: Delay/ DelayFrame/ Virtual.
	 * Has no effect if the Sequence has already started.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = DreamTween)
		UDreamTweenerSequence* Join(UObject* WorldContextObject, UDreamTweener* tweener);

	/**
	 * Fires a callback when the sequence reaches the end of what it holds so far. DOTween's
	 * AppendCallback: "slide out, THEN hide the panel" is one sequence rather than a sequence plus an
	 * OnComplete that has to know how long the sequence turned out to be.
	 * Has no effect if the Sequence has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamTween)
		UDreamTweenerSequence* AppendCallback(const FDreamTweenSimpleDynamicDelegate& callback);
	/**
	 * Fires a callback at a time position in the sequence, wherever that falls among the tweens.
	 * Has no effect if the Sequence has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamTween)
		UDreamTweenerSequence* InsertCallback(float timePosition, const FDreamTweenSimpleDynamicDelegate& callback);
	/** AppendCallback for C++ callers. */
	UDreamTweenerSequence* AppendCallback(const TFunction<void()>& callback);
	/** InsertCallback for C++ callers. */
	UDreamTweenerSequence* InsertCallback(float timePosition, const TFunction<void()>& callback);

protected:
	virtual void OnStartGetValue() override {};
	virtual void TweenAndApplyValue(float currentTime) override;
	virtual void SetValueForIncremental() override;
	virtual void SetValueForYoyo() override;
	virtual void SetValueForRestart() override;
	virtual void SetOriginValueForRestart() override;

	virtual void Restart()override;
	virtual void Goto(float timePoint)override;
};