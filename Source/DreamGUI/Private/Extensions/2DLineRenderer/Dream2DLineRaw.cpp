// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"

UDream2DLineRaw::UDream2DLineRaw(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UDream2DLineRaw::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void UDream2DLineRaw::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDream2DLineRaw::SetPoints(const TArray<FVector2D>& InPoints)
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
