// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Components/DreamText.h"
#include "Components/DreamSprite.h"
#include "Core/DreamUIMeshIndex.h"
#include "Core/DreamUIMeshVertex.h"

struct FDreamUISpriteInfo;
struct FDreamUITextLineProperty;
class UDreamUIFontData_BaseObject;
class UDreamCanvas;
class UDreamWidget;
class UDreamVisual;

/** Origin position/ normal/ tangent stored in UI item's local space */
struct FDreamUIOriginVertexData
{
public:
	FDreamUIOriginVertexData()
	{
		Position = FVector3f::ZeroVector;
		Normal = FVector3f(-1, 0, 0);
		Tangent = FVector3f(0, 1, 0);
	}
	FDreamUIOriginVertexData(FVector3f InPosition)
	{
		Position = InPosition;
		Normal = FVector3f(-1, 0, 0);
		Tangent = FVector3f(0, 1, 0);
	}
	FDreamUIOriginVertexData(FVector3f InPosition, FVector3f InNormal, FVector3f InTangent)
	{
		Position = InPosition;
		Normal = InNormal;
		Tangent = InTangent;
	}
	FVector3f Position;
	FVector3f Normal;
	FVector3f Tangent;
};

class DREAMGUI_API FDreamUIGeometry
{
public:
	FDreamUIGeometry() = default;

	// Explicit copy constructor
	FDreamUIGeometry(const FDreamUIGeometry& Other)
		: OriginVertices(Other.OriginVertices),
		  Vertices(Other.Vertices),
		  Triangles(Other.Triangles),
		  Texture(Other.Texture),
		  Material(Other.Material),
		  bIsFont(Other.bIsFont),
		  bSupportDrawcallBatching(Other.bSupportDrawcallBatching),
		  TransformRelativeToCanvas(Other.TransformRelativeToCanvas),
		  BoundsMin2DInCanvasSpace(Other.BoundsMin2DInCanvasSpace),
		  BoundsMax2DInCanvasSpace(Other.BoundsMax2DInCanvasSpace)
	{
		
	}

	// Explicit assignment operator
	FDreamUIGeometry& operator=(const FDreamUIGeometry& Other)
	{
		if (this != &Other)
		{
			OriginVertices = Other.OriginVertices;
			Vertices = Other.Vertices;
			Triangles = Other.Triangles;
			Texture = Other.Texture;
			Material = Other.Material;
			bIsFont = Other.bIsFont;
			bSupportDrawcallBatching = Other.bSupportDrawcallBatching;
			TransformRelativeToCanvas = Other.TransformRelativeToCanvas;
			BoundsMin2DInCanvasSpace = Other.BoundsMin2DInCanvasSpace;
			BoundsMax2DInCanvasSpace = Other.BoundsMax2DInCanvasSpace;
		}
		return *this;
	}

	//is calculating vertices?
	std::atomic<bool> bIsCalculating = false;
	//local space vertex position/ normal/ tangent
	TArray<FDreamUIOriginVertexData> OriginVertices;
	//vertex buffer, position/normal/tangent is stored as transformed space(Canvas space), origin position/normal/tangent is stored in originVertices/originNormals/originTangents
	TArray<FDreamUIMeshVertex> Vertices;
	//triangle indices
	TArray<FDreamUIMeshIndex> Triangles;

	TWeakObjectPtr<UTexture> Texture = nullptr;
	TWeakObjectPtr<UMaterialInterface> Material = nullptr;
	bool bIsFont = false;
	bool bSupportDrawcallBatching = true;

	FTransform TransformRelativeToCanvas;
	FVector2D BoundsMin2DInCanvasSpace;
	FVector2D BoundsMax2DInCanvasSpace;

	void CopyDataForPrepare(const FDreamUIGeometry& Other)
	{
		Vertices.SetNumUninitialized(Other.Vertices.Num());
		FMemory::Memcpy(Vertices.GetData(), Other.Vertices.GetData(), Other.Vertices.Num() * sizeof(FDreamUIMeshVertex));
		Triangles.SetNumUninitialized(Other.Triangles.Num());
		FMemory::Memcpy(Triangles.GetData(), Other.Triangles.GetData(), Other.Triangles.Num() * sizeof(FDreamUIMeshIndex));
		
		Texture = Other.Texture;
		Material = Other.Material;
		bIsFont = Other.bIsFont;
		bSupportDrawcallBatching = Other.bSupportDrawcallBatching;
		
		BoundsMin2DInCanvasSpace = Other.BoundsMin2DInCanvasSpace;
		BoundsMax2DInCanvasSpace = Other.BoundsMax2DInCanvasSpace;
	}

	/** 
	 * Clear vertices and triangle indices data and keep memory, so when the data array do SetNumUninitialized (or similar function, which just change num but not memory), the origin data is still there.
	 * e.g. The following lines use InTriangleChanged to tell if we need to set actual data in triangles, after SetNumUninitialized, the old triangles value is good to use.
	 *		
			auto& triangles = uiGeo->triangles;
			triangles.SetNumUninitialized(6);
			if (InTriangleChanged)
			{
				triangles[0] = 0;
				triangles[1] = 3;
				triangles[2] = 2;
				triangles[3] = 0;
				triangles[4] = 1;
				triangles[5] = 3;
			}
	 */
	void Clear()
	{
		Vertices.Reset();
		Triangles.Reset();
		OriginVertices.Reset();
	}

	/**
	 * Unlike default TArray's SetNum, this function only Construct new item when get new memory.
	 * SetNum will Construct item from Num to NewNum, include old existing memory (memory between Num and Max), which is not what I want.
	 * What I want is, use default value only on new memory, so new item will not contain NaN value.
	 */
	template<class T>
	static void DreamUIGeometrySetArrayNum(TArray<T>& InArray, int32 NewNum, bool bAllowShrinking = true)
	{
		auto PrevMax = InArray.Max();
		if (NewNum > InArray.Max())
		{
			InArray.AddUninitialized(InArray.Max() - InArray.Num());//Set Num to Max and can keep existing memory.
			InArray.SetNumZeroed(NewNum, bAllowShrinking ? EAllowShrinking::Yes : EAllowShrinking::No);//New memory will be Zeroed.
		}
		else
		{
			InArray.SetNumUninitialized(NewNum, bAllowShrinking ? EAllowShrinking::Yes : EAllowShrinking::No);
		}
		//SetNum could change array max, so mem-zero the additional memory
		if (InArray.Max() > PrevMax)
		{
			FMemory::Memzero((uint8*)InArray.GetData() + PrevMax * sizeof(T), (InArray.Max() - PrevMax) * sizeof(T));
		}
	}

#pragma region UISprite_UITexture_Simple
public:
	static void UpdateUIRectSimpleVertex(FDreamUIGeometry* uiGeo, 
		float width, float height, const FVector2f& pivot, const FDreamUISpriteInfo& spriteInfo, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
	static void UpdateRectBlockVertex(FDreamUIGeometry* uiGeo,
		bool bEnableOuterShadow, const FVector2f& outerShadowOffset, float outerShadowSize, float outerShadowBlur, bool bSoftEdge,
		float width, float height, const FVector2f& pivot, 
		const FDreamUISpriteInfo& uniformSpriteInfo, const FDreamUISpriteInfo& spriteInfo,
		UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_UITexture_Border
public:
	static void UpdateUIRectBorderVertex(FDreamUIGeometry* uiGeo, bool fillCenter,
		float width, float height, const FVector2f& pivot, const FDreamUISpriteInfo& spriteInfo, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		float pixelsPerUnitMultiplier,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Tiled
public:
	static void UpdateUIRectTiledVertex(FDreamUIGeometry* uiGeo, 
		const FDreamUISpriteInfo& spriteInfo, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, float width, float height, const FVector2f& pivot, const int& widthRectCount, const int& heightRectCount, float widthRemainedRectSize, float heightRemainedRectSize, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Horizontal_Vertical
public:
	static void UpdateUIRectFillHorizontalVerticalVertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
		, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, bool horizontalOrVertical
		, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Radial90
public:
	static void UpdateUIRectFillRadial90Vertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
		, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EDreamUISpriteFillOriginType_Radial90 originType
		, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Radial180
public:
	static void UpdateUIRectFillRadial180Vertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
		, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EDreamUISpriteFillOriginType_Radial180 originType
		, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Radial360
public:
	static void UpdateUIRectFillRadial360Vertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
		, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EDreamUISpriteFillOriginType_Radial360 originType
		, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion


public:
	static void UpdateUIColor(FDreamUIGeometry* uiGeo, FColor color);
	static void TransformVertices(class UDreamCanvas* canvas, class UDreamVisual* item, FDreamUIGeometry* uiGeo);
	static void CalculatePivotOffset(
		float width, float height, const FVector2f& pivot
		, float& pivotOffsetX, float& pivotOffsetY
	);
	static void CalculateOffsetAndSize(
		float width, float height, const FVector2f& pivot, const FDreamUISpriteInfo& spriteInfo
		, float& pivotOffsetX, float& pivotOffsetY, float& halfWidth, float& halfHeight
	);
	/** Snaps emitted glyph quads to the canvas pixel grid; what a pixel-perfect text runs after painting. */
	static void AdjustPixelPerfectPos_For_UIText(TArray<FDreamUIOriginVertexData>& originVertices, const TArray<FDreamUITextCharProperty>& cacheCharPropertyArray, UDreamCanvas* RenderCanvas, UDreamVisual* Visual);
	static void AdjustPixelPerfectPos(
		TArray<FDreamUIOriginVertexData>& originVertices, int startIndex, int count
		, UDreamCanvas* RenderCanvas, UDreamVisual* Visual
	);
private:
	static void OffsetVertices(TArray<FDreamUIOriginVertexData>& vertices, int count, float offsetX, float offsetY);
};
