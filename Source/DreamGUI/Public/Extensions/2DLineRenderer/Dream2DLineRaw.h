// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Dream2DLineRendererBase.h"
#include "Dream2DLineRaw.generated.h"


UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDream2DLineRaw : public UDream2DLineRendererBase
{
	GENERATED_BODY()

public:	
	UDream2DLineRaw(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	UPROPERTY(EditAnywhere, Category = DreamGUI)
		TArray<FVector2D> PointArray = { FVector2D(-100, 0), FVector2D(100, 0) };

	virtual void CalculatePoints()override {};
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()override
	{
		return PointArray;
	}

	/**
	 * The stroked bounding box of PointArray, or a negative pair when there is no line to bound.
	 * Both measure functions read it; they differ only in which component they return.
	 */
	FVector2f MeasureStrokeBounds()const;
public:
	virtual float GetPreferredWidth()const override;
	virtual float GetPreferredHeight()const override;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetPoints(const TArray<FVector2D>& InPoints);
};
