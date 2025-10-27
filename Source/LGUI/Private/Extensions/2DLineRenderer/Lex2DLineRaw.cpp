// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/Lex2DLineRaw.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"

ULex2DLineRaw::ULex2DLineRaw(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void ULex2DLineRaw::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void ULex2DLineRaw::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULex2DLineRaw::SetPoints(const TArray<FVector2D>& InPoints)
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
