// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Extensions/2DLineRenderer/Dream2DLineRendererBase.h"
#include "DreamTweener.h"
#include "DreamRing.generated.h"


UCLASS(ClassGroup = (DreamGUI), Blueprintable)
class DREAMGUI_API UDreamRing : public UDream2DLineRendererBase
{
	GENERATED_BODY()

public:	
	UDreamRing(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;

	UPROPERTY(EditAnywhere, Category = DreamGUI)
		float StartAngle = 0.0f;
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		float EndAngle = 90.0f;
	//line segment
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (ClampMin = "0", ClampMax = "200"))
		int Segment = 12;

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
	UFUNCTION(BlueprintCallable, Category = DreamGUI)float GetStartAngle()const { return StartAngle; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)float GetEndAngle()const { return EndAngle; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)int GetSegment()const { return Segment; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)void SetStartAngle(float newValue);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)void SetEndAngle(float newValue);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)void SetSegment(int newValue);

	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
		UDreamTweener* StartAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
		UDreamTweener* EndAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
};

