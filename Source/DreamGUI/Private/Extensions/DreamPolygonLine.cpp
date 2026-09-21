// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamPolygonLine.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamWidget.h"

UDreamPolygonLine::UDreamPolygonLine(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	EndType = EDream2DLineRenderer_EndType::ConnectStartAndEnd;
}

void UDreamPolygonLine::CalculatePoints()
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

FVector2D UDreamPolygonLine::GetStartPointTangentDirection()
{
	auto Widget = GetWidget();
	float angle = FMath::DegreesToRadians(StartAngle);
	auto dir = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
	auto tanDir = FVector2D(-Widget->GetWidth() * dir.Y, Widget->GetHeight() * dir.X);
	tanDir.Normalize();
	return tanDir;
}
FVector2D UDreamPolygonLine::GetEndPointTangentDirection()
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

/** Re-clamps Sides for the same reason UDreamPolygon::SetFullCycle does -- see the note there. */
void UDreamPolygonLine::SetFullCycle(bool value) {
	if (FullCycle != value)
	{
		FullCycle = value;
		Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
		MarkVerticesDirty(true, true, true, false);
	}
}
void UDreamPolygonLine::SetStartAngle(float value) {
	if (StartAngle != value)
	{
		StartAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void UDreamPolygonLine::SetEndAngle(float value) {
	if (EndAngle != value)
	{
		EndAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void UDreamPolygonLine::SetSides(int value) {
	if (Sides != value)
	{
		Sides = value;
		Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
		MarkVerticesDirty(true, true, true, true);
	}
}
/*
 * Measured against the count the shape implies rather than against the stored array, for the reason
 * spelled out on UDreamPolygon::SetVertexOffsetArray. The expression is the same one CalculatePoints
 * sizes the array with, which is what keeps this copy of the idea from drifting from that one.
 */
void UDreamPolygonLine::SetVertexOffsetArray(const TArray<float>& value)
{
	const int32 ExpectedCount = FullCycle ? Sides : (Sides + 1);
	if (value.Num() == ExpectedCount)
	{
		VertexOffsetArray = value;
		MarkVertexPositionDirty();
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Array count not equal! Expected:%d for %d sides, value:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ExpectedCount, Sides, value.Num());
	}
}
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWidgetRegistry.h"
UDreamTweener* UDreamPolygonLine::StartAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, EDreamTweenEase easeType /* = EDreamTweenEase::OutCubic */)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamPolygonLine::GetStartAngle), FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamPolygonLine::SetStartAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamPolygonLine::EndAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, EDreamTweenEase easeType /* = EDreamTweenEase::OutCubic */)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamPolygonLine::GetEndAngle), FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamPolygonLine::SetEndAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}

DECLARE_DREAM_GUI_VISUAL("PolygonLine", UDreamPolygonLine)
