// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Animation/DreamUIAnimationPlayCallbackProxy.h"
#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Core/DreamUserWidget.h"

UDreamUIAnimationPlayCallbackProxy* UDreamUIAnimationPlayCallbackProxy::NewPlayAnimationProxyObject(
	FDreamUIAnimationHandle& Result,
	UDreamUserWidget* Widget,
	UMovieSceneSequence* Animation,
	float StartAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed)
{
	UDreamUIAnimationPlayCallbackProxy* Proxy = NewObject<UDreamUIAnimationPlayCallbackProxy>();
	// Alive for as long as the calling frame refers to it, the way every async proxy node is.
	Proxy->SetFlags(RF_StrongRefOnFrame);
	Proxy->Execute(Widget, Animation, StartAtTime, TOptional<float>(), NumLoopsToPlay, PlayMode, PlaybackSpeed, Result);
	return Proxy;
}

UDreamUIAnimationPlayCallbackProxy* UDreamUIAnimationPlayCallbackProxy::NewPlayAnimationTimeRangeProxyObject(
	FDreamUIAnimationHandle& Result,
	UDreamUserWidget* Widget,
	UMovieSceneSequence* Animation,
	float StartAtTime,
	float EndAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed)
{
	UDreamUIAnimationPlayCallbackProxy* Proxy = NewObject<UDreamUIAnimationPlayCallbackProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	const TOptional<float> End = EndAtTime > 0.0f ? TOptional<float>(EndAtTime) : TOptional<float>();
	Proxy->Execute(Widget, Animation, StartAtTime, End, NumLoopsToPlay, PlayMode, PlaybackSpeed, Result);
	return Proxy;
}

void UDreamUIAnimationPlayCallbackProxy::Execute(UDreamUserWidget* Widget, UMovieSceneSequence* Animation, float StartAtTime, TOptional<float> EndAtTime, int32 NumLoopsToPlay, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, FDreamUIAnimationHandle& OutHandle)
{
	OutHandle = FDreamUIAnimationHandle();
	if (IsValid(Widget))
	{
		OutHandle = EndAtTime.IsSet()
			? Widget->PlayAnimationTimeRange(Animation, StartAtTime, EndAtTime.GetValue(), NumLoopsToPlay, PlayMode, PlaybackSpeed)
			: Widget->PlayAnimation(Animation, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed);
	}

	UDreamWidgetAnimationComponent* Animator = OutHandle.IsValid() ? OutHandle.Player->GetTypedOuter<UDreamWidgetAnimationComponent>() : nullptr;
	if (Animator == nullptr)
	{
		// Nothing started, so nothing will ever finish. Complete anyway rather than leave the
		// graph hanging on an event that cannot come; the widget already logged why.
		ScheduleFinished();
		return;
	}

	// The instance may have ended inside PlayAnimation itself (an empty animation), in which case
	// the release already happened and no delegate will fire for it.
	if (!Animator->IsAnimationPlaying(OutHandle) && !Animator->IsAnimationPaused(OutHandle))
	{
		ScheduleFinished();
		return;
	}

	Component = Animator;
	Player = OutHandle.Player;
	FinishedHandle = Animator->OnInstanceFinished.AddUObject(this, &UDreamUIAnimationPlayCallbackProxy::OnInstanceFinished);
}

void UDreamUIAnimationPlayCallbackProxy::OnInstanceFinished(const FDreamUIAnimationHandle& InHandle)
{
	if (InHandle.Player != Player.Get())
	{
		return;
	}
	if (Component.IsValid())
	{
		Component->OnInstanceFinished.Remove(FinishedHandle);
	}
	FinishedHandle.Reset();
	ScheduleFinished();
}

void UDreamUIAnimationPlayCallbackProxy::ScheduleFinished()
{
	// Next frame, as UMG does: the caller is often still inside the evaluation that ended the
	// instance, and whatever the graph does on Finished should not re-enter it.
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDreamUIAnimationPlayCallbackProxy::BroadcastFinished));
	}
}

bool UDreamUIAnimationPlayCallbackProxy::BroadcastFinished(float /*DeltaTime*/)
{
	TickerHandle.Reset();
	Finished.Broadcast();
	// One shot.
	return false;
}
