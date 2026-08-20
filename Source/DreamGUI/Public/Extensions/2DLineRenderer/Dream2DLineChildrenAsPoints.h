// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Dream2DLineRendererBase.h"
#include "Dream2DLineChildrenAsPoints.generated.h"

//Collect U2DLineChildrenAsPointsChild, and use child's relative location as points to draw line
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDream2DLineChildrenAsPoints : public UDream2DLineRendererBase
{
	GENERATED_BODY()

public:	
	UDream2DLineChildrenAsPoints(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;
	virtual void OnRegister()override;

	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI)TArray<FVector2D> 
		CurrentPointArray;

	virtual void CalculatePoints()override;
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()override
	{
		return CurrentPointArray;
	}
public:
	void OnChildPositionChanged();
};
