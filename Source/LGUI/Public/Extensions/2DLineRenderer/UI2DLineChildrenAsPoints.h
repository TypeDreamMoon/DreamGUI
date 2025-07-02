// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/Actor/LexWidgetActor.h"
#include "UI2DLineRendererBase.h"
#include "UI2DLineChildrenAsPoints.generated.h"

//Collect U2DLineChildrenAsPointsChild, and use child's relative location as points to draw line
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUI2DLineChildrenAsPoints : public UUI2DLineRendererBase
{
	GENERATED_BODY()

public:	
	UUI2DLineChildrenAsPoints(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;
	virtual void OnRegister()override;

	UPROPERTY(VisibleAnywhere, Transient, Category = LGUI)TArray<FVector2D> 
		CurrentPointArray;

	virtual void CalculatePoints()override;
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()override
	{
		return CurrentPointArray;
	}
public:
	void OnChildPositionChanged();
};
