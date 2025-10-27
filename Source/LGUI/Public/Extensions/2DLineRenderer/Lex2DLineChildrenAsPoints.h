// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Lex2DLineRendererBase.h"
#include "Lex2DLineChildrenAsPoints.generated.h"

//Collect U2DLineChildrenAsPointsChild, and use child's relative location as points to draw line
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULex2DLineChildrenAsPoints : public ULex2DLineRendererBase
{
	GENERATED_BODY()

public:	
	ULex2DLineChildrenAsPoints(const FObjectInitializer& ObjectInitializer);

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
