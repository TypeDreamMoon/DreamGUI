// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Extensions/2DLineRenderer/Dream2DLineRendererBase.h"
#include "DreamTweener.h"
#include "DreamPolygonLine.generated.h"


/**
 * render a polygon line shape
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamPolygonLine : public UDream2DLineRendererBase
{
	GENERATED_BODY()

public:
	UDreamPolygonLine(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool FullCycle = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float StartAngle = 0.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "!FullCycle"))
		float EndAngle = 90.0f;
	//Sides of polygon
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		int Sides = 3;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (UIMin = "0.0", UIMax = "1.0"))
		TArray<float> VertexOffsetArray;

	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI)TArray<FVector2D> CurrentPointArray;

	//Begin UI2DLineRendererBase interface
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()override
	{
		return CurrentPointArray;
	}
	virtual void CalculatePoints()override;
	virtual bool OverrideStartPointTangentDirection()override { return true; }
	virtual bool OverrideEndPointTangentDirection()override { return true; }
	virtual FVector2D GetStartPointTangentDirection()override;
	virtual FVector2D GetEndPointTangentDirection()override;
	//End UI2DLineRendererBase interface
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetFullCycle()const { return FullCycle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetStartAngle()const { return StartAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetEndAngle()const { return EndAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int GetSides()const { return Sides; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const TArray<float>& GetVertexOffsetArray()const { return VertexOffsetArray; }
	//Return direct mutable array for edit and change. Call MarkVertexPositionDirty() function after change.
	TArray<float>& GetVertexOffsetArray_Direct() { return VertexOffsetArray; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFullCycle(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetStartAngle(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEndAngle(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSides(int value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetVertexOffsetArray(const TArray<float>& value);

	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
		UDreamTweener* StartAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
		UDreamTweener* EndAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
};

