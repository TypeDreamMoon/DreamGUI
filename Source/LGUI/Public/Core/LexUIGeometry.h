// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Components/LexText.h"
#include "Components/UISprite.h"
#include "Core/LexUIMeshIndex.h"
#include "Core/LexUIMeshVertex.h"

struct FLexUISpriteInfo;
struct FUITextLineProperty;
class ULexUIFontData_BaseObject;
class ULexCanvas;
class ULexWidget;
class ULexVisual;

/** Origin position/ normal/ tangent stored in UI item's local space */
struct FLexUIOriginVertexData
{
public:
	FLexUIOriginVertexData()
	{
		Position = FVector3f::ZeroVector;
		Normal = FVector3f(-1, 0, 0);
		Tangent = FVector3f(0, 1, 0);
	}
	FLexUIOriginVertexData(FVector3f InPosition)
	{
		Position = InPosition;
		Normal = FVector3f(-1, 0, 0);
		Tangent = FVector3f(0, 1, 0);
	}
	FLexUIOriginVertexData(FVector3f InPosition, FVector3f InNormal, FVector3f InTangent)
	{
		Position = InPosition;
		Normal = InNormal;
		Tangent = InTangent;
	}
	FVector3f Position;
	FVector3f Normal;
	FVector3f Tangent;
};

class LGUI_API FLexUIGeometry
{
public:
	//local space vertex position/ normal/ tangent
	TArray<FLexUIOriginVertexData> OriginVertices;
	//vertex buffer, position/normal/tangent is stored as transformed space(Canvas space), origin position/normal/tangent is stored in originVertices/originNormals/originTangents
	TArray<FLexUIMeshVertex> Vertices;
	//triangle indices
	TArray<FLexUIMeshIndexBufferType> Triangles;

	TWeakObjectPtr<UTexture> Texture = nullptr;
	TWeakObjectPtr<UMaterialInterface> Material = nullptr;
	bool bIsFont = false;

	FTransform TransformRelativeToCanvas;
	FVector2D BoundsMin2DInCanvasSpace;
	FVector2D BoundsMax2DInCanvasSpace;

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
	 * Fill this data to another.
	 * @return true if any data size changed, false otherwise
	 */
	bool CopyTo(FLexUIGeometry* Target)
	{
		bool verticesCountChanged = false;
		if (Vertices.Num() != Target->Vertices.Num())
		{
			Target->Vertices.SetNumUninitialized(Vertices.Num());
			Target->OriginVertices.SetNumUninitialized(Vertices.Num());
			verticesCountChanged = true;
		}
		FMemory::Memcpy(Target->OriginVertices.GetData(), OriginVertices.GetData(), OriginVertices.Num() * sizeof(FLexUIOriginVertexData));
		FMemory::Memcpy(Target->Vertices.GetData(), Vertices.GetData(), Vertices.Num() * sizeof(FLexUIMeshVertex));

		bool triangleCountChanged = false;
		if (Triangles.Num() != Target->Triangles.Num())
		{
			Target->Triangles.SetNumUninitialized(Triangles.Num());
			triangleCountChanged = true;
		}
		FMemory::Memcpy(Target->Triangles.GetData(), Triangles.GetData(), Triangles.Num() * sizeof(FLexUIMeshIndexBufferType));

		return verticesCountChanged || triangleCountChanged;
	}

	/**
	 * Unlike default TArray's SetNum, this function only Construct new item when get new memory.
	 * SetNum will Construct item from Num to NewNum, include old existing memory (memory between Num and Max), which is not what I want.
	 * What I want is, use default value only on new memory, so new item will not contain NaN value.
	 */
	template<class T>
	static void LexUIGeometrySetArrayNum(TArray<T>& InArray, int32 NewNum, bool bAllowShrinking = true)
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
	static void UpdateUIRectSimpleVertex(FLexUIGeometry* uiGeo, 
		const float& width, const float& height, const FVector2f& pivot, const FLexUISpriteInfo& spriteInfo, ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
	static void UpdateUIProceduralRectSimpleVertex(FLexUIGeometry* uiGeo,
		bool bEnableBody,
		bool bOuterShadow, const FVector2f& outerShadowOffset, const float& outerShadowSize, const float& outerShadowBlur, bool bSoftEdge,
		const float& width, const float& height, const FVector2f& pivot, 
		const FLexUISpriteInfo& uniformSpriteInfo, const FLexUISpriteInfo& spriteInfo,
		ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_UITexture_Border
public:
	static void UpdateUIRectBorderVertex(FLexUIGeometry* uiGeo, bool fillCenter,
		const float& width, const float& height, const FVector2f& pivot, const FLexUISpriteInfo& spriteInfo, ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Tiled
public:
	static void UpdateUIRectTiledVertex(FLexUIGeometry* uiGeo, 
		const FLexUISpriteInfo& spriteInfo, ULexCanvas* renderCanvas, ULexVisual* uiComp, const float& width, const float& height, const FVector2f& pivot, const int& widthRectCount, const int& heightRectCount, const float& widthRemainedRectSize, const float& heightRemainedRectSize, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Horizontal_Vertical
public:
	static void UpdateUIRectFillHorizontalVerticalVertex(FLexUIGeometry* uiGeo, const float& width, const float& height, const FVector2f& pivot
		, const FLexUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, bool horizontalOrVertical
		, ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Radial90
public:
	static void UpdateUIRectFillRadial90Vertex(FLexUIGeometry* uiGeo, const float& width, const float& height, const FVector2f& pivot
		, const FLexUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EUISpriteFillOriginType_Radial90 originType
		, ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Radial180
public:
	static void UpdateUIRectFillRadial180Vertex(FLexUIGeometry* uiGeo, const float& width, const float& height, const FVector2f& pivot
		, const FLexUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EUISpriteFillOriginType_Radial180 originType
		, ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UISprite_Fill_Radial360
public:
	static void UpdateUIRectFillRadial360Vertex(FLexUIGeometry* uiGeo, const float& width, const float& height, const FVector2f& pivot
		, const FLexUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EUISpriteFillOriginType_Radial360 originType
		, ULexCanvas* renderCanvas, ULexVisual* uiComp, const FColor& color,
		bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
	);
#pragma endregion
#pragma region UIText
public:
	static void UpdateUIText(const FString& text, int32 visibleCharCount, float width, float height, const FVector2f& pivot
		, const FColor& color, uint8 renderOpacity, const FVector2f& fontSpace, FLexUIGeometry* uiGeo, float fontSize
		, EUITextParagraphHorizontalAlign paragraphHAlign, EUITextParagraphVerticalAlign paragraphVAlign, EUITextOverflowType overflowType
		, ETextWrappingPolicy wrappingPolicy, float maxHorizontalWidth, bool kerning
		, EUITextFontStyle fontStyle, FVector2f& textRealSize
		, ULexCanvas* renderCanvas, class ULexText* uiComp
		, TArray<FUITextLineProperty>& cacheLinePropertyArray, TArray<FUITextCharProperty>& cacheCharPropertyArray, TArray<FUIText_RichTextCustomTag>& cacheRichTextCustomTagArray
		, TArray<FUIText_RichTextImageTag>& cacheRichTextImageTagArray
		, ULexUIFontData_BaseObject* font, bool richText, int32 richTextFilterFlags);
#pragma endregion

public:
	static void UpdateUIColor(FLexUIGeometry* uiGeo, const FColor& color);
	static void TransformVertices(class ULexCanvas* canvas, class ULexVisual* item, FLexUIGeometry* uiGeo);
	static void CalculatePivotOffset(
		const float& width, const float& height, const FVector2f& pivot
		, float& pivotOffsetX, float& pivotOffsetY
	);
	static void CalculateOffsetAndSize(
		const float& width, const float& height, const FVector2f& pivot, const FLexUISpriteInfo& spriteInfo
		, float& pivotOffsetX, float& pivotOffsetY, float& halfWidth, float& halfHeight
	);
	static void AdjustPixelPerfectPos(
		TArray<FLexUIOriginVertexData>& originVertices, int startIndex, int count
		, ULexCanvas* renderCanvas, ULexVisual* uiComp
	);
private:
	static void OffsetVertices(TArray<FLexUIOriginVertexData>& vertices, int count, float offsetX, float offsetY);
};
