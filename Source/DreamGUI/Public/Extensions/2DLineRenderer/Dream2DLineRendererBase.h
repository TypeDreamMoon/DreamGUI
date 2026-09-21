// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamTweener.h"
#include "Core/Components/DreamImage.h"
#include "Dream2DLineRendererBase.generated.h"


UENUM(BlueprintType, Category = DreamGUI)
enum class EDream2DLineRenderer_EndType :uint8
{
	None,
	//Draw a cap at start and end.
	Cap,
	//Connect start and end with a line
	ConnectStartAndEnd,
};
/**
 * Render line use given points.
 */
UCLASS(ClassGroup = (DreamGUI), Abstract, NotBlueprintable)
class DREAMGUI_API UDream2DLineRendererBase : public UDreamImage
{
	GENERATED_BODY()

public:	
	UDream2DLineRendererBase(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;

	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (ClampMin = "0"))
		float LineWidth = 10.0f;
	//Draw extra quad at start and end.
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		EDream2DLineRenderer_EndType EndType = EDream2DLineRenderer_EndType::Cap;
	/** When EndType is Cap, if LineWidth bigger or smaller than Sprite's width, then cap size will scale with it. */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (EditCondition="EndType==EDream2DLineRenderer_EndType::Cap"))
		bool bEndCapSizeAffectByLineWidth = false;
	//This will slide line's width from left to right.
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (ClampMin = "0", ClampMax = "1"))
		float LineWidthOffset = 0.5f;

	static TArray<FVector2D> EmptyArray;
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()PURE_VIRTUAL(UUI2DLineRendererBase::GetCalcaultedPointArray, return EmptyArray;)
	virtual void CalculatePoints()PURE_VIRTUAL(UUI2DLineRendererBase::CalculatePoints, );
	//override start point tangent direction when EndType == Cap
	virtual bool OverrideStartPointTangentDirection() { return false; }
	//override end point tangent direction when EndType == Cap
	virtual bool OverrideEndPointTangentDirection() { return false; }
	//if OverrideStartPointTangentDirection return true, then this function must be implemented
	virtual FVector2D GetStartPointTangentDirection();
	//if OverrideEndPointTangentDirection return true, then this function must be implemented
	virtual FVector2D GetEndPointTangentDirection();

	//Begin DreamVisualBatchMesh interface
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual void OnBeforeCreateOrUpdateGeometry()override;
	//End DreamVisualBatchMesh interface

	FORCEINLINE bool AngleLargerThanPi(const FVector2D& A, const FVector2D& B)
	{
		float temp = A.X * B.Y - B.X * A.Y;
		return temp < 0;
	}
	void GenerateLinePoint(const FVector2D& InCurrentPoint, const FVector2D& InPrevPoint, const FVector2D& InNextPoint
		, float InLineLeftWidth, float InLineRightWidth
		, FVector2D& OutPosA, FVector2D& OutPosB
		, FVector2D& InOutPrevLineDir);
public:
	/**
	 * How far out along a joint's normal the strip's edge vertices sit, as a multiple of the
	 * half-width, for a joint whose half-included-angle has the given sine. Bounded -- see the
	 * definition for the limit and why a sharp corner needs one as much as a degenerate one does.
	 *
	 * Public and static because it is the whole of the joint decision and the only part of this
	 * class's geometry reachable without a canvas to pump.
	 */
	static double ComputeMiterScale(float InSinHalfAngle);
protected:
	FORCEINLINE bool CanConnectStartEndPoint(int InPointCount) { return EndType == EDream2DLineRenderer_EndType::ConnectStartAndEnd && InPointCount >= 3; }
	void Update2DLineRendererBaseTriangle(FDreamUIGeometry& InGeo, const TArray<FVector2D>& InPointArray);
	void Update2DLineRendererBaseUV(FDreamUIGeometry& InGeo, const TArray<FVector2D>& InPointArray);
	void Update2DLineRendererBaseVertex(FDreamUIGeometry& InGeo, const TArray<FVector2D>& InPointArray);
public:
	/**
	 * No opinion, cancelling the one UDreamImage would otherwise supply. See the note on the
	 * definition -- the brush a line paints with says nothing about how long the line is.
	 *
	 * A subclass whose points are AUTHORED rather than derived from the rect overrides this again
	 * and answers for real; UDream2DLineRaw is the one that does.
	 */
	virtual float GetPreferredWidth()const override;
	virtual float GetPreferredHeight()const override;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		float GetLineWidth()const { return LineWidth; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		EDream2DLineRenderer_EndType GetEndType()const { return EndType; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		float GetLineWidthOffset()const { return LineWidthOffset; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetEndType(EDream2DLineRenderer_EndType newValue);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetLineWidth(float newValue);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetLineWidthOffset(float newValue);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamTweener* LineWidthTo(float endValue, float duration, float delay = 0.0f, EDreamTweenEase easeType = EDreamTweenEase::OutCubic);
};
