// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamTweener.h"
#include "Core/Components/DreamImage.h"
#include "DreamPolygon.generated.h"


UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamPolygonUVType :uint8
{
	//Use full rect uv
	SpriteRect,
	//Use left center as polygon's center, and right center as polygon's ring uv
	HeightCenter,
	//Use left center as polygon's center, right bottom as polygon ring's start, and right top as polygon ring's end
	StretchSpriteHeight,
};
/**
 * render a solid polygon shape
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamPolygon : public UDreamImage
{
	GENERATED_BODY()

public:	
	UDreamPolygon(const FObjectInitializer& ObjectInitializer);

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
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamPolygonUVType UVType = EDreamPolygonUVType::SpriteRect;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(UIMin="0.0", UIMax="1.0"))
		TArray<float> VertexOffsetArray;
	
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetFullCycle()const { return FullCycle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetStartAngle()const { return StartAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetEndAngle()const { return EndAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int GetSides()const { return Sides; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamPolygonUVType GetUVType()const { return UVType; }
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
		void SetUVType(EDreamPolygonUVType value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetVertexOffsetArray(const TArray<float>& value);

	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
		UDreamTweener* StartAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
		UDreamTweener* EndAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
};

