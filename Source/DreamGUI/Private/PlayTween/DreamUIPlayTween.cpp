// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PlayTween/DreamUIPlayTween.h"
#include "DreamGUI.h"
#include "DreamTweenManager.h"

void UDreamUIPlayTween::Stop()
{
	UDreamTweenManager::KillIfIsTweening(this, Tweener, false);
}
void UDreamUIPlayTween::Start()
{
	Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateLambda([] { return 0.0f; })
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamUIPlayTween::OnUpdate)
		, 1.0f, Duration);
	if(Tweener)
		Tweener
		->SetDelay(StartDelay)
		->SetLoop(LoopType, LoopCount)
		->SetEase(EaseType)
		->SetCurveFloat(EaseCurve)
		->OnStart([&] {
			OnStart.FireEvent();
			OnStartCPP.Broadcast();
			OnStartBP.Broadcast();
		})
		->OnUpdate([&](float progress) {
			OnUpdateProgress.FireEvent(progress);
			OnUpdateProgressCPP.Broadcast(progress);
			OnUpdateProgressBP.Broadcast(progress);
		})
		->OnCycleComplete([&] {
			OnCycleComplete.FireEvent();
			OnCycleCompleteCPP.Broadcast(Tweener->GetLoopCycleCount());
			OnCycleCompleteBP.Broadcast(Tweener->GetLoopCycleCount());
		})
		->OnComplete([&] {
			OnComplete.FireEvent();
			OnCompleteCPP.Broadcast();
			OnCompleteBP.Broadcast();
		})
		->SetAffectByGamePause(bAffectByGamePause)
		->SetAffectByTimeDilation(bAffectByTimeDilation);
}
