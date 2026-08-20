// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"

UDream2DLineChildrenAsPoints::UDream2DLineChildrenAsPoints(const FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer)
{
}

void UDream2DLineChildrenAsPoints::BeginPlay()
{
    Super::BeginPlay();
}

void UDream2DLineChildrenAsPoints::OnRegister()
{
    Super::OnRegister();
}

void UDream2DLineChildrenAsPoints::CalculatePoints()
{
    auto& SortedItemArray = GetWidget()->GetChildren();
    int pointCount = SortedItemArray.Num();
    CurrentPointArray.Reset(pointCount);
    for (int i = 0; i < pointCount; i++)
    {
        auto Location3D = SortedItemArray[i]->GetRelativeLocation();
        CurrentPointArray.Add(FVector2D(Location3D.Y, Location3D.Z));
    }
}

void UDream2DLineChildrenAsPoints::OnChildPositionChanged()
{
    MarkVertexPositionDirty();
}