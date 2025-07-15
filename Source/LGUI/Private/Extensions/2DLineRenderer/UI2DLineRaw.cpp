// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/UI2DLineRaw.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"

UUI2DLineRaw::UUI2DLineRaw(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UUI2DLineRaw::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void UUI2DLineRaw::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UUI2DLineRaw::SetPoints(const TArray<FVector2D>& InPoints)
{
	if (InPoints.Num() != PointArray.Num())
	{
		PointArray = InPoints;
		MarkVerticesDirty(true, true, true, true);
	}
	else
	{
		PointArray = InPoints;
		MarkVertexPositionDirty();
	}
}
