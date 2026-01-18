// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LexRing.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"
#include "LTweenManager.h"

ULexRing::ULexRing(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void ULexRing::BeginPlay()
{
	Super::BeginPlay();
}


void ULexRing::CalculatePoints()
{
	Segment = FMath::Max(0, Segment);
	int pointCount = Segment + 2;
	CurrentPointArray.Reset(pointCount);

	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(StartAngle);
	float angleInterval = FMath::DegreesToRadians((EndAngle - StartAngle) / (Segment + 1));
	float halfWidth = Widget->GetWidth() * 0.5f;
	float halfHeight = Widget->GetHeight() * 0.5f;
	//points
	for (int i = 0; i < pointCount; i++)
	{
		float x = halfWidth * FMath::Cos(angle);
		float y = halfHeight * FMath::Sin(angle);
		CurrentPointArray.Add(FVector2D(x, y));
		angle += angleInterval;
	}
}

FVector2D ULexRing::GetStartPointTangentDirection()
{
	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(StartAngle);
	auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
	auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
	tanDir.Normalize();
	return tanDir;
}
FVector2D ULexRing::GetEndPointTangentDirection()
{
	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(EndAngle);
	auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
	auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
	tanDir.Normalize();
	return tanDir;
}

void ULexRing::SetStartAngle(float newValue)
{
	if (StartAngle != newValue)
	{
		StartAngle = newValue;
		MarkVertexPositionDirty();
	}
}
void ULexRing::SetEndAngle(float newValue)
{
	if (EndAngle != newValue)
	{
		EndAngle = newValue;
		MarkVertexPositionDirty();
	}
}
void ULexRing::SetSegment(int newValue)
{
	newValue = FMath::Max(0, newValue);
	if (Segment != newValue)
	{
		Segment = newValue;
		MarkVerticesDirty(true, true, true, true);
	}
}


#include "Core/LexUISettings.h"
ULTweener* ULexRing::StartAngleTo(float endValue, float duration, float delay, ELTweenEase easeType)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexRing::GetStartAngle), FLTweenFloatSetterFunction::CreateUObject(this, &ULexRing::SetStartAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}
ULTweener* ULexRing::EndAngleTo(float endValue, float duration, float delay, ELTweenEase easeType)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexRing::GetEndAngle), FLTweenFloatSetterFunction::CreateUObject(this, &ULexRing::SetEndAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}