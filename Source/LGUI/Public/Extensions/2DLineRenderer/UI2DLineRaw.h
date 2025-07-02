// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/Actor/LexWidgetActor.h"
#include "UI2DLineRendererBase.h"
#include "UI2DLineRaw.generated.h"


UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUI2DLineRaw : public UUI2DLineRendererBase
{
	GENERATED_BODY()

public:	
	UUI2DLineRaw(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	UPROPERTY(EditAnywhere, Category = LGUI)
		TArray<FVector2D> PointArray = { FVector2D(-100, 0), FVector2D(100, 0) };

	virtual void CalculatePoints()override {};
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()override
	{
		return PointArray;
	}
public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetPoints(const TArray<FVector2D>& InPoints);
};
