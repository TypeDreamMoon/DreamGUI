// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamRing.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamWidget.h"

UDreamRing::UDreamRing(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UDreamRing::BeginPlay()
{
	Super::BeginPlay();
}


void UDreamRing::CalculatePoints()
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

FVector2D UDreamRing::GetStartPointTangentDirection()
{
	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(StartAngle);
	auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
	auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
	tanDir.Normalize();
	return tanDir;
}
FVector2D UDreamRing::GetEndPointTangentDirection()
{
	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(EndAngle);
	auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
	auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
	tanDir.Normalize();
	return tanDir;
}

void UDreamRing::SetStartAngle(float newValue)
{
	if (StartAngle != newValue)
	{
		StartAngle = newValue;
		MarkVertexPositionDirty();
	}
}
void UDreamRing::SetEndAngle(float newValue)
{
	if (EndAngle != newValue)
	{
		EndAngle = newValue;
		MarkVertexPositionDirty();
	}
}
void UDreamRing::SetSegment(int newValue)
{
	newValue = FMath::Max(0, newValue);
	if (Segment != newValue)
	{
		Segment = newValue;
		MarkVerticesDirty(true, true, true, true);
	}
}


UDreamTweener* UDreamRing::StartAngleTo(float endValue, float duration, float delay, EDreamTweenEase easeType)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamRing::GetStartAngle), FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamRing::SetStartAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamRing::EndAngleTo(float endValue, float duration, float delay, EDreamTweenEase easeType)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamRing::GetEndAngle), FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamRing::SetEndAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}