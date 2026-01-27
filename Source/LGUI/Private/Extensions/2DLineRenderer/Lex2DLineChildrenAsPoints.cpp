// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/Lex2DLineChildrenAsPoints.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"

ULex2DLineChildrenAsPoints::ULex2DLineChildrenAsPoints(const FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer)
{
}

void ULex2DLineChildrenAsPoints::BeginPlay()
{
    Super::BeginPlay();
}

void ULex2DLineChildrenAsPoints::OnRegister()
{
    Super::OnRegister();
}

void ULex2DLineChildrenAsPoints::CalculatePoints()
{
    auto& SortedItemArray = GetWidget()->GetUIChildren();
    int pointCount = SortedItemArray.Num();
    CurrentPointArray.Reset(pointCount);
    for (int i = 0; i < pointCount; i++)
    {
        auto Location3D = SortedItemArray[i]->GetRelativeLocation();
        CurrentPointArray.Add(FVector2D(Location3D.Y, Location3D.Z));
    }
}

void ULex2DLineChildrenAsPoints::OnChildPositionChanged()
{
    MarkVertexPositionDirty();
}