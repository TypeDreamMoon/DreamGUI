// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextPaintLibrary.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"

namespace DreamTextPaintLibraryLocal
{
	/** The text's phase getter and setter for a layer: what a phase tween reads its start from and writes each step to. */
	void GetPaintPhaseAccessors(UDreamText* InText, EDreamTextPaintLayer InLayer,
		FDreamTweenFloatGetterFunction& OutGetter, FDreamTweenFloatSetterFunction& OutSetter)
	{
		switch (InLayer)
		{
		case EDreamTextPaintLayer::Outline:
			OutGetter = FDreamTweenFloatGetterFunction::CreateUObject(InText, &UDreamText::GetOutlinePaintPhase);
			OutSetter = FDreamTweenFloatSetterFunction::CreateUObject(InText, &UDreamText::SetOutlinePaintPhase);
			break;
		case EDreamTextPaintLayer::Overlay:
			OutGetter = FDreamTweenFloatGetterFunction::CreateUObject(InText, &UDreamText::GetOverlayPaintPhase);
			OutSetter = FDreamTweenFloatSetterFunction::CreateUObject(InText, &UDreamText::SetOverlayPaintPhase);
			break;
		case EDreamTextPaintLayer::Face:
		default:
			OutGetter = FDreamTweenFloatGetterFunction::CreateUObject(InText, &UDreamText::GetFacePaintPhase);
			OutSetter = FDreamTweenFloatSetterFunction::CreateUObject(InText, &UDreamText::SetFacePaintPhase);
			break;
		}
	}

	/** What every tween of a visual is given: its ease and delay, and the clock its widget runs on (pause, dilation). */
	void FinishPaintTween(UDreamText* InText, UDreamTweener* InTweener, float InDelay, EDreamTweenEase InEase)
	{
		InTweener->SetEase(InEase)->SetDelay(InDelay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(InText->GetWidget(), InTweener);
	}
}

UDreamTweener* UDreamTextPaintLibrary::PaintPhaseTo(UDreamText* Text, EDreamTextPaintLayer Layer, float To, float Duration, float Delay, EDreamTweenEase Ease)
{
	if (!IsValid(Text))
	{
		return nullptr;
	}
	FDreamTweenFloatGetterFunction Getter;
	FDreamTweenFloatSetterFunction Setter;
	DreamTextPaintLibraryLocal::GetPaintPhaseAccessors(Text, Layer, Getter, Setter);
	// Made on the text, so the manager ends it when the text goes.
	UDreamTweener* Tweener = UDreamTweenManager::To(Text, Getter, Setter, To, Duration);
	if (Tweener != nullptr)
	{
		DreamTextPaintLibraryLocal::FinishPaintTween(Text, Tweener, Delay, Ease);
	}
	return Tweener;
}

UDreamTweener* UDreamTextPaintLibrary::PaintAngleTo(UDreamText* Text, float To, float Duration, float Delay, EDreamTweenEase Ease)
{
	if (!IsValid(Text))
	{
		return nullptr;
	}
	UDreamTweener* Tweener = UDreamTweenManager::To(Text,
		FDreamTweenFloatGetterFunction::CreateUObject(Text, &UDreamText::GetPaintAngleOffset),
		FDreamTweenFloatSetterFunction::CreateUObject(Text, &UDreamText::SetPaintAngleOffset),
		To, Duration);
	if (Tweener != nullptr)
	{
		DreamTextPaintLibraryLocal::FinishPaintTween(Text, Tweener, Delay, Ease);
	}
	return Tweener;
}

UDreamTweener* UDreamTextPaintLibrary::PlayShimmer(UDreamText* Text, float Duration, float Delay, int32 Loops)
{
	if (!IsValid(Text))
	{
		return nullptr;
	}
	UDreamTweener* Tweener = UDreamTweenManager::To(Text,
		FDreamTweenFloatGetterFunction::CreateUObject(Text, &UDreamText::GetOverlayPaintPhase),
		FDreamTweenFloatSetterFunction::CreateUObject(Text, &UDreamText::SetOverlayPaintPhase),
		1.0f, Duration);
	if (Tweener != nullptr)
	{
		// The start goes in once there is a tween to run from it, as UDreamVisual::ColorFrom does: the tween reads it when
		// it starts, and every pass after the first starts there again.
		Text->SetOverlayPaintPhase(-1.0f);
		DreamTextPaintLibraryLocal::FinishPaintTween(Text, Tweener, Delay, EDreamTweenEase::Linear);
		// Loops counts passes: 0 is one pass, as 1 is, and anything below 0 is no end.
		Tweener->SetLoop(EDreamTweenLoop::Restart, Loops < 0 ? -1 : FMath::Max(Loops, 1));
	}
	return Tweener;
}
