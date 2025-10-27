// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LexPolygonLine.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"
#include "LTweenManager.h"

ULexPolygonLine::ULexPolygonLine(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	EndType = ELex2DLineRenderer_EndType::ConnectStartAndEnd;
}

void ULexPolygonLine::CalculatePoints()
{
	Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
	int pointCount = FullCycle ? Sides : (Sides + 1);//ring's point count, not include center point
	CurrentPointArray.Reset(pointCount);
	//vert offset
	int vertexOffsetCount = FullCycle ? Sides : (Sides + 1);
	if (VertexOffsetArray.Num() != vertexOffsetCount)
	{
		if (VertexOffsetArray.Num() > vertexOffsetCount)
		{
			VertexOffsetArray.SetNumUninitialized(vertexOffsetCount);
		}
		else
		{
			for (int i = VertexOffsetArray.Num(); i < vertexOffsetCount; i++)
			{
				VertexOffsetArray.Add(1.0f);
			}
		}
	}

	auto Widget = GetWidget();
	float calcEndAngle = EndAngle;
	if (FullCycle)calcEndAngle = StartAngle + 360.0f;
	float angle = FMath::DegreesToRadians(StartAngle);
	float angleInterval = FMath::DegreesToRadians((calcEndAngle - StartAngle) / Sides);
	float halfWidth = Widget->GetWidth() * 0.5f;
	float halfHeight = Widget->GetHeight() * 0.5f;
	if (!FullCycle)
	{
		CurrentPointArray.Add(FVector2D(0, 0));
	}
	//full cycle points
	for (int i = 0; i < pointCount; i++)
	{
		float x = halfWidth * VertexOffsetArray[i] * FMath::Cos(angle);
		float y = halfHeight * VertexOffsetArray[i] * FMath::Sin(angle);
		CurrentPointArray.Add(FVector2D(x, y));
		angle += angleInterval;
	}
}

FVector2D ULexPolygonLine::GetStartPointTangentDirection()
{
	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(StartAngle);
	auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
	auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
	tanDir.Normalize();
	return tanDir;
}
FVector2D ULexPolygonLine::GetEndPointTangentDirection()
{
	if (FullCycle)
	{
		return GetStartPointTangentDirection();
	}
	else
	{
		auto Widget = GetWidget();
		float angle = FMath::DegreesToRadians(EndAngle);
		auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
		auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
		tanDir.Normalize();
		return tanDir;
	}
}

void ULexPolygonLine::SetFullCycle(bool value) {
	if (FullCycle != value)
	{
		FullCycle = value;
		MarkVerticesDirty(true, true, true, false);
	}
}
void ULexPolygonLine::SetStartAngle(float value) {
	if (StartAngle != value)
	{
		StartAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void ULexPolygonLine::SetEndAngle(float value) {
	if (EndAngle != value)
	{
		EndAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void ULexPolygonLine::SetSides(int value) {
	if (Sides != value)
	{
		Sides = value;
		Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
		MarkVerticesDirty(true, true, true, true);
	}
}
void ULexPolygonLine::SetVertexOffsetArray(const TArray<float>& value)
{
	if (VertexOffsetArray.Num() == value.Num())
	{
		VertexOffsetArray = value;
		MarkVertexPositionDirty();
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUIPolygonLine::SetVertexOffsetArray]Array count not equal! VertexOffsetArray:%d, value:%d"), VertexOffsetArray.Num(), value.Num());
	}
}
#include "Core/LexUISettings.h"
ULTweener* ULexPolygonLine::StartAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, ELTweenEase easeType /* = ELTweenEase::OutCubic */)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexPolygonLine::GetStartAngle), FLTweenFloatSetterFunction::CreateUObject(this, &ULexPolygonLine::SetStartAngle), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(easeType)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexPolygonLine::EndAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, ELTweenEase easeType /* = ELTweenEase::OutCubic */)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexPolygonLine::GetEndAngle), FLTweenFloatSetterFunction::CreateUObject(this, &ULexPolygonLine::SetEndAngle), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(easeType)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
