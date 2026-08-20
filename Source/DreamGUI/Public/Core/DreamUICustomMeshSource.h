// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/DreamVisualBatchMesh.h"
#include "Curves/CurveFloat.h"
#include "DreamUICustomMeshSource.generated.h"

class FDreamUIGeometry;
class UDreamCanvas;
class UDreamVisualBatchMesh;

/**
 * UI mesh generator.
 * This class only hold the method of generating mesh, the actual mesh data is stored inside outer class (DreamBatchMesh) which hold this instance.
 */
UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamUICustomMeshSource : public UDreamUIGeometryHelper
{
	GENERATED_BODY()
public:
	/**
	 * Fill the mesh data.
	 * @param InDreamMesh The UI element which will use this mesh.
	 * @param InTriangleChanged Normally just ignore this.
	 * @param InVertexPositionChanged Normally just ignore this.
	 * @param InVertexUVChanged Normally just ignore this.
	 * @param InVertexColorChanged Normally just ignore this.
	 */
	virtual void OnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged);
	/**
	 * Get uv value on raycast hit point. Normally just use "GetHitUVbyFaceIndex".
	 * @param InDreamMesh The UI element which will use this mesh.
	 * @param InHitFaceIndex Hit point face id on this mesh.
	 * @param InHitPoint Hit point.
	 * @param InLineStart Normally just use InHitFaceIndex and InHitPoint can get the right result, but you can use your own method to do line cast with InLineStart and InLineEnd parameters.
	 * @param InLineEnd See InLineStart.
	 * @param OutHitUV result uv
	 * @return true if hit suceess
	 */
	virtual bool GetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)const;
	/**
	 * Is this mesh type support drawcall batching? When the mesh comes in 3d then should not do drawcall batching. Normally just leave it return false.
	 */
	virtual bool SupportDrawcallBatching()const;
protected:
	/**
	 * Fill the mesh data.
	 * @param InDreamMesh The UI element which will use this mesh.
	 * @param InTriangleChanged Normally just ignore this.
	 * @param InVertexPositionChanged Normally just ignore this.
	 * @param InVertexUVChanged Normally just ignore this.
	 * @param InVertexColorChanged Normally just ignore this.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "OnCreateMesh", AdvancedDisplay = 1))
	void ReceiveOnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged);
	/**
	 * Get uv value on raycast hit point. Normally just use "GetHitUVbyFaceIndex".
	 * @param InDreamMesh The UI element which will use this mesh.
	 * @param InHitFaceIndex Hit point face id on this mesh.
	 * @param InHitPoint Hit point.
	 * @param InLineStart Normally just use InHitFaceIndex and InHitPoint can get the right result, but you can use your own method to do line cast with InLineStart and InLineEnd parameters.
	 * @param InLineEnd See InLineStart.
	 * @param OutHitUV result uv
	 * @return true if hit suceess
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "GetHitUV"))
	bool ReceiveGetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)const;
	/**
	 * Is this mesh type support drawcall batching? When the mesh comes in 3d then should not do drawcall batching. Normally just leave it return false.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "SupportDrawcallBatching"))
	bool ReceiveSupportDrawcallBatching()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetHitUVbyFaceIndex(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, FVector2D& OutHitUV)const;
};

UCLASS(ClassGroup = DreamGUI, DisplayName="DreamCustomMeshSource Cylinder")
class DREAMGUI_API UDreamCustomMeshSource_Cylinder : public UDreamUICustomMeshSource
{
	GENERATED_BODY()
public:
	virtual void OnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual bool GetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV) const override;
protected:
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (ClampMin = -180.0f, ClampMax = 180.0f))
	float CylinderArcAngle = 45;
};

UCLASS(ClassGroup = DreamGUI, DisplayName = "DreamCustomMeshSource CurvyPlane")
class DREAMGUI_API UDreamCustomMeshSource_CurvyPlane : public UDreamUICustomMeshSource
{
	GENERATED_BODY()
public:
	UDreamCustomMeshSource_CurvyPlane();
	virtual void OnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual bool GetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV) const override;
protected:
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (ClampMin = 1, ClampMax = 200))
	int Segment = 10;
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (
		DisplayName = "Shape Curve",
		XAxisName = "Time 0-1",
		YAxisName = "Value 0-1"))
	FRuntimeFloatCurve ShapeCurve;
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	float CurveScale = 100;
};
