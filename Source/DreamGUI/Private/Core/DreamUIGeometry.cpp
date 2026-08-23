// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIGeometry.h"
#include "DreamGUI.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisual.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIRichTextImageData_BaseObject.h"
#include "Core/FRichTextParser.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/Components/DreamWidget.h"


FORCEINLINE float RoundToFloat(float value)
{
	return FMath::FloorToFloat(value + 0.5f);
}

DECLARE_CYCLE_STAT(TEXT("UIGeometry TransformPixelPerfectVertices"), STAT_TransformPixelPerfectVertices, STATGROUP_DreamGUI);

void FDreamUIGeometry::AdjustPixelPerfectPos(TArray<FDreamUIOriginVertexData>& originVertices, int startIndex, int count, UDreamCanvas* RenderCanvas, UDreamVisual* Visual)
{
	SCOPE_CYCLE_COUNTER(STAT_TransformPixelPerfectVertices);
	auto CanvasWidget = RenderCanvas->GetRootCanvas()->GetWidget();
	auto ComponentToCanvasTransform = Visual->GetWidget()->GetWorldTransform() * CanvasWidget->GetWorldTransform().Inverse();
	if (!UDreamCanvas::Is2DUITransform(ComponentToCanvasTransform))return;//only 2d UI can do pixel perfect
	FTransform canvasToComponentTransform = ComponentToCanvasTransform.Inverse();

	auto halfCanvasWidth = CanvasWidget->GetWidth() * 0.5f;
	auto halfCanvasHeight = CanvasWidget->GetHeight() * 0.5f;
	float rootCanvasScale = RenderCanvas->GetRootCanvas()->GetCanvasScale();
	float inv_RootCanvasScale = 1.0f / rootCanvasScale;

	for (int i = startIndex; i < count; i++)
	{
		auto item = originVertices[i].Position;

		auto canvasSpaceLocation = ComponentToCanvasTransform.TransformPosition(FVector(item));
		canvasSpaceLocation.Y -= halfCanvasWidth;
		canvasSpaceLocation.Z -= halfCanvasHeight;
		float screenSpaceLocationY = canvasSpaceLocation.Y * rootCanvasScale;
		float screenSpaceLocationZ = canvasSpaceLocation.Z * rootCanvasScale;
		item.Y = RoundToFloat(screenSpaceLocationY) * inv_RootCanvasScale;
		item.Z = RoundToFloat(screenSpaceLocationZ) * inv_RootCanvasScale;
		item.Y += halfCanvasWidth;
		item.Z += halfCanvasHeight;

		originVertices[i].Position = FVector3f(canvasToComponentTransform.TransformPosition(FVector(item)));
	}
}
void AdjustPixelPerfectPos_For_UIRectFillRadial360(TArray<FDreamUIOriginVertexData>& originVertices, UDreamCanvas* RenderCanvas, UDreamVisual* Visual)
{
	SCOPE_CYCLE_COUNTER(STAT_TransformPixelPerfectVertices);
	auto CanvasWidget = RenderCanvas->GetRootCanvas()->GetWidget();
	auto ComponentToCanvasTransform = Visual->GetWidget()->GetWorldTransform() * CanvasWidget->GetWorldTransform().Inverse();
	if (!UDreamCanvas::Is2DUITransform(ComponentToCanvasTransform))return;//only 2d UI can do pixel perfect
	FTransform canvasToComponentTransform = ComponentToCanvasTransform.Inverse();

	auto halfCanvasWidth = CanvasWidget->GetWidth() * 0.5f;
	auto halfCanvasHeight = CanvasWidget->GetHeight() * 0.5f;
	float rootCanvasScale = RenderCanvas->GetRootCanvas()->GetCanvasScale();
	float inv_RootCanvasScale = 1.0f / rootCanvasScale;

	static TArray<int> vertArray = { 0, 2, 6, 8 };
	for (int i = 0; i < vertArray.Num(); i++)
	{
		int vertIndex = vertArray[i];
		auto originPos = originVertices[vertIndex].Position;

		auto canvasSpaceLocation = ComponentToCanvasTransform.TransformPosition(FVector(originPos));
		canvasSpaceLocation.Y -= halfCanvasWidth;
		canvasSpaceLocation.Z -= halfCanvasHeight;
		float screenSpaceLocationY = canvasSpaceLocation.Y * rootCanvasScale;
		float screenSpaceLocationZ = canvasSpaceLocation.Z * rootCanvasScale;
		canvasSpaceLocation.Y = RoundToFloat(screenSpaceLocationY) * inv_RootCanvasScale;
		canvasSpaceLocation.Z = RoundToFloat(screenSpaceLocationZ) * inv_RootCanvasScale;
		canvasSpaceLocation.Y += halfCanvasWidth;
		canvasSpaceLocation.Z += halfCanvasHeight;

		originVertices[vertIndex].Position = FVector3f(canvasToComponentTransform.TransformPosition(canvasSpaceLocation));
	}
}
void FDreamUIGeometry::AdjustPixelPerfectPos_For_UIText(TArray<FDreamUIOriginVertexData>& originVertices, const TArray<FDreamUITextCharProperty>& cacheCharPropertyArray, UDreamCanvas* RenderCanvas, UDreamVisual* Visual)
{
	SCOPE_CYCLE_COUNTER(STAT_TransformPixelPerfectVertices);
	if (cacheCharPropertyArray.Num() <= 0)return;

	auto CanvasWidget = RenderCanvas->GetRootCanvas()->GetWidget();
	auto ComponentToCanvasTransform = Visual->GetWidget()->GetWorldTransform() * CanvasWidget->GetWorldTransform().Inverse();
	if (!UDreamCanvas::Is2DUITransform(ComponentToCanvasTransform))return;//only 2d UI can do pixel perfect
	FTransform canvasToComponentTransform = ComponentToCanvasTransform.Inverse();

	auto halfCanvasWidth = CanvasWidget->GetWidth() * 0.5f;
	auto halfCanvasHeight = CanvasWidget->GetHeight() * 0.5f;
	float rootCanvasScale = RenderCanvas->GetRootCanvas()->GetCanvasScale();
	float inv_RootCanvasScale = 1.0f / rootCanvasScale;

	for (int i = 0; i < cacheCharPropertyArray.Num(); i++)
	{
		auto charProperty = cacheCharPropertyArray[i];
		int vertStartIndex = charProperty.StartVertIndex;
		int vertEndIndex = charProperty.StartVertIndex + charProperty.VertCount;

		//calculate first vert
		float offsetY, offsetZ;
		{
			auto originPos = originVertices[vertStartIndex].Position;

			auto canvasSpaceLocation = ComponentToCanvasTransform.TransformPosition(FVector(originPos));
			canvasSpaceLocation.Y -= halfCanvasWidth;
			canvasSpaceLocation.Z -= halfCanvasHeight;
			float screenSpaceLocationX = canvasSpaceLocation.Y * rootCanvasScale;
			float screenSpaceLocationY = canvasSpaceLocation.Z * rootCanvasScale;
			canvasSpaceLocation.Y = RoundToFloat(screenSpaceLocationX) * inv_RootCanvasScale;
			canvasSpaceLocation.Z = RoundToFloat(screenSpaceLocationY) * inv_RootCanvasScale;
			canvasSpaceLocation.Y += halfCanvasWidth;
			canvasSpaceLocation.Z += halfCanvasHeight;

			auto newPos = canvasToComponentTransform.TransformPosition(canvasSpaceLocation);
			originVertices[vertStartIndex].Position = FVector3f(newPos);
			offsetY = newPos.Y - originPos.Y;
			offsetZ = newPos.Z - originPos.Z;
		}

		for (int vertIndex = vertStartIndex + 1; vertIndex < vertEndIndex; vertIndex++)
		{
			auto& originPos = originVertices[vertIndex].Position;
			originPos.Y += offsetY;
			originPos.Z += offsetZ;
		}
	}
}

#pragma region UISprite_UITexture_Simple
void FDreamUIGeometry::UpdateUIRectSimpleVertex(FDreamUIGeometry* uiGeo,
	float width, float height, const FVector2f& pivot, const FDreamUISpriteInfo& spriteInfo, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	DreamUIGeometrySetArrayNum(triangles, 6);
	if (InTriangleChanged)
	{
		triangles[0] = 0;
		triangles[1] = 3;
		triangles[2] = 2;
		triangles[3] = 0;
		triangles[4] = 1;
		triangles[5] = 3;
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	DreamUIGeometrySetArrayNum(vertices, 4);
	DreamUIGeometrySetArrayNum(originVertices, 4);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		if (InVertexPositionChanged)
		{
			//offset and size
			float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
			CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
			//positions
			float minX = -halfW + pivotOffsetX;
			float minY = -halfH + pivotOffsetY;
			float maxX = halfW + pivotOffsetX;
			float maxY = halfH + pivotOffsetY;
			originVertices[0].Position = FVector3f(0, minX, minY);
			originVertices[1].Position = FVector3f(0, maxX, minY);
			originVertices[2].Position = FVector3f(0, minX, maxY);
			originVertices[3].Position = FVector3f(0, maxX, maxY);
			//snap pixel
			if (pixelPerfect)
			{
				AdjustPixelPerfectPos(originVertices, 0, 4, renderCanvas, uiComp);
			}
		}

		if (InVertexUVChanged)
		{
			vertices[0].TextureCoordinate[0] = spriteInfo.GetUV0();
			vertices[1].TextureCoordinate[0] = spriteInfo.GetUV1();
			vertices[2].TextureCoordinate[0] = spriteInfo.GetUV2();
			vertices[3].TextureCoordinate[0] = spriteInfo.GetUV3();
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for(int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
void FDreamUIGeometry::UpdateRectBlockVertex(FDreamUIGeometry* uiGeo,
	bool bEnableOuterShadow, const FVector2f& outerShadowOffset, float outerShadowSize, float outerShadowBlur, bool bSoftEdge,
	float width, float height, const FVector2f& pivot, 
	const FDreamUISpriteInfo& uniformSpriteInfo, const FDreamUISpriteInfo& spriteInfo,
	UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	DreamUIGeometrySetArrayNum(triangles, 6);
	if (InTriangleChanged)
	{
		triangles[0] = 0;
		triangles[1] = 3;
		triangles[2] = 2;
		triangles[3] = 0;
		triangles[4] = 1;
		triangles[5] = 3;
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	DreamUIGeometrySetArrayNum(vertices, 4);
	DreamUIGeometrySetArrayNum(originVertices, 4);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		//offset and size
		float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
		CalculateOffsetAndSize(width, height, pivot, uniformSpriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
		//positions
		float minX = -halfW + pivotOffsetX;
		float minY = -halfH + pivotOffsetY;
		float maxX = halfW + pivotOffsetX;
		float maxY = halfH + pivotOffsetY;
		
		if (InVertexPositionChanged)
		{
			if (bEnableOuterShadow)
			{
				float shadowMinX = minX + outerShadowOffset.X;
				float shadowMinY = minY + outerShadowOffset.Y;
				float shadowMaxX = maxX + outerShadowOffset.X;
				float shadowMaxY = maxY + outerShadowOffset.Y;
				float additionalShadowSize = outerShadowSize + outerShadowBlur * 0.5f;
				shadowMinX -= additionalShadowSize;
				shadowMaxX += additionalShadowSize;
				shadowMinY -= additionalShadowSize;
				shadowMaxY += additionalShadowSize;
				float PosMinX = FMath::Min(minX, shadowMinX);
				float PosMaxX = FMath::Max(maxX, shadowMaxX);
				float PosMinY = FMath::Min(minY, shadowMinY);
				float PosMaxY = FMath::Max(maxY, shadowMaxY);
				if (bSoftEdge)//offset 1 pixel to make edge smooth
				{
					PosMinX -= 1;
					PosMaxX += 1;
					PosMinY -= 1;
					PosMaxY += 1;
				}
				originVertices[0].Position = FVector3f(0, PosMinX, PosMinY);
				originVertices[1].Position = FVector3f(0, PosMaxX, PosMinY);
				originVertices[2].Position = FVector3f(0, PosMinX, PosMaxY);
				originVertices[3].Position = FVector3f(0, PosMaxX, PosMaxY);
			}
			else
			{
				float PosMinX = minX;
				float PosMaxX = maxX;
				float PosMinY = minY;
				float PosMaxY = maxY;
				if (bSoftEdge)//offset 1 pixel to make edge smooth
				{
					PosMinX -= 1;
					PosMaxX += 1;
					PosMinY -= 1;
					PosMaxY += 1;
				}
				originVertices[0].Position = FVector3f(0, PosMinX, PosMinY);
				originVertices[1].Position = FVector3f(0, PosMaxX, PosMinY);
				originVertices[2].Position = FVector3f(0, PosMinX, PosMaxY);
				originVertices[3].Position = FVector3f(0, PosMaxX, PosMaxY);
			}
			//snap pixel
			if (pixelPerfect)
			{
				int startIndex = 0;
				AdjustPixelPerfectPos(originVertices, startIndex, startIndex + 4, renderCanvas, uiComp);
			}
		}

		if (InVertexUVChanged || bSoftEdge)
		{
			auto& OriginVert0 = originVertices[0];
			auto& OriginVert1 = originVertices[1];
			auto& OriginVert2 = originVertices[2];
			auto& OriginVert3 = originVertices[3];
			auto& Vert0 = vertices[0];
			auto& Vert1 = vertices[1];
			auto& Vert2 = vertices[2];
			auto& Vert3 = vertices[3];
			
			float oneDivideWidth = 1.0f / width;
			float oneDivideHeight = 1.0f / height;
			
			Vert0.TextureCoordinate[0] = uniformSpriteInfo.GetUV0() + FVector2f((OriginVert0.Position.Y - minX) * oneDivideWidth, -(OriginVert0.Position.Z - minY) * oneDivideHeight);
			Vert1.TextureCoordinate[0] = uniformSpriteInfo.GetUV1() + FVector2f((OriginVert1.Position.Y - maxX) * oneDivideWidth, -(OriginVert1.Position.Z - minY) * oneDivideHeight);
			Vert2.TextureCoordinate[0] = uniformSpriteInfo.GetUV2() + FVector2f((OriginVert2.Position.Y - minX) * oneDivideWidth, -(OriginVert2.Position.Z - maxY) * oneDivideHeight);
			Vert3.TextureCoordinate[0] = uniformSpriteInfo.GetUV3() + FVector2f((OriginVert3.Position.Y - maxX) * oneDivideWidth, -(OriginVert3.Position.Z - maxY) * oneDivideHeight);
			
			//uv2 store the info for sampling texture and Sprite
			Vert0.TextureCoordinate[2] = spriteInfo.GetUV0();
			Vert1.TextureCoordinate[2] = spriteInfo.GetUV1();
			Vert2.TextureCoordinate[2] = spriteInfo.GetUV2();
			Vert3.TextureCoordinate[2] = spriteInfo.GetUV3();
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}
	}
}
#pragma endregion
#pragma region UISprite_UITexture_Border
void FDreamUIGeometry::UpdateUIRectBorderVertex(FDreamUIGeometry* uiGeo, bool fillCenter,
	float width, float height, const FVector2f& pivot, const FDreamUISpriteInfo& spriteInfo, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	float pixelsPerUnitMultiplier,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	int triangleCount;
	if (fillCenter)
	{
		triangleCount = 54;
	}
	else
	{
		triangleCount = 48;
	}
	DreamUIGeometrySetArrayNum(triangles, triangleCount);
	if (InTriangleChanged)
	{
		int wSeg = 3, hSeg = 3;
		int vStartIndex = 0;
		int triangleArrayIndex = 0;
		for (int h = 0; h < hSeg; h++)
		{
			for (int w = 0; w < wSeg; w++)
			{
				if (!fillCenter)
					if (h == 1 && w == 1)continue;
				int vIndex = vStartIndex + w;
				triangles[triangleArrayIndex++] = vIndex;
				triangles[triangleArrayIndex++] = vIndex + wSeg + 2;
				triangles[triangleArrayIndex++] = vIndex + wSeg + 1;

				triangles[triangleArrayIndex++] = vIndex;
				triangles[triangleArrayIndex++] = vIndex + 1;
				triangles[triangleArrayIndex++] = vIndex + wSeg + 2;
			}
			vStartIndex += wSeg + 1;
		}
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto verticesCount = 16;
	DreamUIGeometrySetArrayNum(vertices, verticesCount);
	DreamUIGeometrySetArrayNum(originVertices, verticesCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		if (InVertexPositionChanged)
		{
			//pivot offset
			float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
			CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
			float geoWidth = halfW * 2;
			float geoHeight = halfH * 2;
			//vertices
			float x0, x1, x2, x3, y0, y1, y2, y3;
			float widthBorder = (spriteInfo.Border.Left + spriteInfo.Border.Right) * pixelsPerUnitMultiplier;
			float heightBorder = (spriteInfo.Border.Top + spriteInfo.Border.Bottom) * pixelsPerUnitMultiplier;
			float widthScale = geoWidth < widthBorder ? geoWidth / widthBorder : 1.0f;
			float heightScale = geoHeight < heightBorder ? geoHeight / heightBorder : 1.0f;
			x0 = (-halfW + pivotOffsetX);
			x1 = (x0 + spriteInfo.Border.Left * widthScale * pixelsPerUnitMultiplier);
			x3 = (halfW + pivotOffsetX);
			x2 = (x3 - spriteInfo.Border.Right * widthScale * pixelsPerUnitMultiplier);
			y0 = (-halfH + pivotOffsetY);
			y1 = (y0 + spriteInfo.Border.Bottom * heightScale * pixelsPerUnitMultiplier);
			y3 = (halfH + pivotOffsetY);
			y2 = (y3 - spriteInfo.Border.Top * heightScale * pixelsPerUnitMultiplier);

			originVertices[0].Position = FVector3f(0, x0, y0);
			originVertices[1].Position = FVector3f(0, x1, y0);
			originVertices[2].Position = FVector3f(0, x2, y0);
			originVertices[3].Position = FVector3f(0, x3, y0);

			originVertices[4].Position = FVector3f(0, x0, y1);
			originVertices[5].Position = FVector3f(0, x1, y1);
			originVertices[6].Position = FVector3f(0, x2, y1);
			originVertices[7].Position = FVector3f(0, x3, y1);

			originVertices[8].Position = FVector3f(0, x0, y2);
			originVertices[9].Position = FVector3f(0, x1, y2);
			originVertices[10].Position = FVector3f(0, x2, y2);
			originVertices[11].Position = FVector3f(0, x3, y2);

			originVertices[12].Position = FVector3f(0, x0, y3);
			originVertices[13].Position = FVector3f(0, x1, y3);
			originVertices[14].Position = FVector3f(0, x2, y3);
			originVertices[15].Position = FVector3f(0, x3, y3);

			//snap pixel
			if (pixelPerfect)
			{
				AdjustPixelPerfectPos(originVertices, 0, verticesCount, renderCanvas, uiComp);
			}
		}

		if (InVertexUVChanged)
		{
			vertices[0].TextureCoordinate[0] = FVector2f(spriteInfo.MinUV.X, spriteInfo.MaxUV.Y);
			vertices[1].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMinUV.X, spriteInfo.MaxUV.Y);
			vertices[2].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMaxUV.X, spriteInfo.MaxUV.Y);
			vertices[3].TextureCoordinate[0] = FVector2f(spriteInfo.MaxUV.X, spriteInfo.MaxUV.Y);

			vertices[4].TextureCoordinate[0] = FVector2f(spriteInfo.MinUV.X, spriteInfo.BorderMaxUV.Y);
			vertices[5].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMinUV.X, spriteInfo.BorderMaxUV.Y);
			vertices[6].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMaxUV.X, spriteInfo.BorderMaxUV.Y);
			vertices[7].TextureCoordinate[0] = FVector2f(spriteInfo.MaxUV.X, spriteInfo.BorderMaxUV.Y);

			vertices[8].TextureCoordinate[0] = FVector2f(spriteInfo.MinUV.X, spriteInfo.BorderMinUV.Y);
			vertices[9].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMinUV.X, spriteInfo.BorderMinUV.Y);
			vertices[10].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMaxUV.X, spriteInfo.BorderMinUV.Y);
			vertices[11].TextureCoordinate[0] = FVector2f(spriteInfo.MaxUV.X, spriteInfo.BorderMinUV.Y);

			vertices[12].TextureCoordinate[0] = FVector2f(spriteInfo.MinUV.X, spriteInfo.MinUV.Y);
			vertices[13].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMinUV.X, spriteInfo.MinUV.Y);
			vertices[14].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMaxUV.X, spriteInfo.MinUV.Y);
			vertices[15].TextureCoordinate[0] = FVector2f(spriteInfo.MaxUV.X, spriteInfo.MinUV.Y);
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
#pragma endregion

#pragma region UISprite_Tiled
void FDreamUIGeometry::UpdateUIRectTiledVertex(FDreamUIGeometry* uiGeo,
	const FDreamUISpriteInfo& spriteInfo, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, float width, float height, const FVector2f& pivot, const int& widthRectCount, const int& heightRectCount, float widthRemainedRectSize, float heightRemainedRectSize, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	int rectangleCount = widthRectCount * heightRectCount;
	auto& triangles = uiGeo->Triangles;
	auto triangleCount = 6 * rectangleCount;
	DreamUIGeometrySetArrayNum(triangles, triangleCount);
	if (InTriangleChanged)
	{
		for (int i = 0, j = 0, triangleIndicesIndex = 0; i < rectangleCount; i++, j += 4)
		{
			triangles[triangleIndicesIndex++] = j;
			triangles[triangleIndicesIndex++] = j + 3;
			triangles[triangleIndicesIndex++] = j + 2;
			triangles[triangleIndicesIndex++] = j;
			triangles[triangleIndicesIndex++] = j + 1;
			triangles[triangleIndicesIndex++] = j + 3;
		}
	}
	
	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto verticesCount = 4 * rectangleCount;
	DreamUIGeometrySetArrayNum(vertices, verticesCount);
	DreamUIGeometrySetArrayNum(originVertices, verticesCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		if (InVertexPositionChanged)
		{
			//pivot offset
			float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
			CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
			//vertices
			int vertIndex = 0;
			float startX = (-halfW + pivotOffsetX);
			float startY = (-halfH + pivotOffsetY);
			float x = startX, y = startY;
			for (int heightRectIndex = 1; heightRectIndex <= heightRectCount; heightRectIndex++)
			{
				float realHeight = heightRectIndex == heightRectCount ? heightRemainedRectSize : spriteInfo.Height;
				for (int widthRectIndex = 1; widthRectIndex <= widthRectCount; widthRectIndex++)
				{
					float realWidth = widthRectIndex == widthRectCount ? (widthRemainedRectSize) : spriteInfo.Width;
					originVertices[vertIndex++].Position = FVector3f(0, x, y);
					originVertices[vertIndex++].Position = FVector3f(0, x + realWidth, y);
					originVertices[vertIndex++].Position = FVector3f(0, x, y + realHeight);
					originVertices[vertIndex++].Position = FVector3f(0, x + realWidth, y + realHeight);

					x += spriteInfo.Width;
				}
				x = startX;
				y += spriteInfo.Height;
			}
			//snap pixel
			if (pixelPerfect)
			{
				AdjustPixelPerfectPos(originVertices, 0, verticesCount, renderCanvas, uiComp);
			}
		}

		if (InVertexUVChanged)
		{
			int vertIndex = 0;
			float remainedUV3X = spriteInfo.BorderMinUV.X + (spriteInfo.BorderMaxUV.X - spriteInfo.BorderMinUV.X) * widthRemainedRectSize / spriteInfo.Width;
			float remainedUV3Y = spriteInfo.BorderMaxUV.Y + (spriteInfo.BorderMinUV.Y - spriteInfo.BorderMaxUV.Y) * heightRemainedRectSize / spriteInfo.Height;
			for (int heightRectIndex = 1; heightRectIndex <= heightRectCount; heightRectIndex++)
			{
				float realUV3Y = heightRectIndex == heightRectCount ? remainedUV3Y : spriteInfo.BorderMaxUV.Y;
				for (int widthRectIndex = 1; widthRectIndex <= widthRectCount; widthRectIndex++)
				{
					float realUV3X = widthRectIndex == widthRectCount ? remainedUV3X : spriteInfo.BorderMaxUV.X;
					vertices[vertIndex++].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMinUV.X, spriteInfo.BorderMaxUV.Y);
					vertices[vertIndex++].TextureCoordinate[0] = FVector2f(realUV3X, spriteInfo.BorderMaxUV.Y);
					vertices[vertIndex++].TextureCoordinate[0] = FVector2f(spriteInfo.BorderMinUV.X, realUV3Y);
					vertices[vertIndex++].TextureCoordinate[0] = FVector2f(realUV3X, realUV3Y);
				}
			}
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
#pragma endregion

#pragma region UISprite_Fill_Horizontal_Vertial
void FDreamUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
	, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, bool horizontalOrVertical
	, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	auto triangleCount = 6;
	DreamUIGeometrySetArrayNum(triangles, 6);
	if (InTriangleChanged)
	{
		triangles[0] = 0;
		triangles[1] = 3;
		triangles[2] = 2;
		triangles[3] = 0;
		triangles[4] = 1;
		triangles[5] = 3;
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto verticesCount = 4;
	DreamUIGeometrySetArrayNum(vertices, 4);
	DreamUIGeometrySetArrayNum(originVertices, verticesCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		if (InVertexPositionChanged || InVertexUVChanged)
		{
			//pivot offset
			float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
			CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
			//positions
			float posMinX = -halfW + pivotOffsetX;
			float posMinY = -halfH + pivotOffsetY;
			float posMaxX = halfW + pivotOffsetX;
			float posMaxY = halfH + pivotOffsetY;
			//uvs
			float uvMinX = spriteInfo.MinUV.X;
			float uvMinY = spriteInfo.MaxUV.Y;
			float uvMaxX = spriteInfo.MaxUV.X;
			float uvMaxY = spriteInfo.MinUV.Y;

			if (InVertexPositionChanged)
			{
				originVertices[0].Position = FVector3f(0, posMinX, posMinY);
				originVertices[1].Position = FVector3f(0, posMaxX, posMinY);
				originVertices[2].Position = FVector3f(0, posMinX, posMaxY);
				originVertices[3].Position = FVector3f(0, posMaxX, posMaxY);

				//snap pixel
				if (pixelPerfect)
				{
					AdjustPixelPerfectPos(originVertices, 0, verticesCount, renderCanvas, uiComp);

					posMinX = originVertices[0].Position.Y;
					posMinY = originVertices[0].Position.Z;
					posMaxX = originVertices[3].Position.Y;
					posMaxY = originVertices[3].Position.Z;
				}
			}
			if (horizontalOrVertical)
			{
				if (flipDirection)
				{
					if (InVertexPositionChanged)
					{
						float value = FMath::Lerp(posMinX, posMaxX, fillAmount);
						originVertices[1].Position.Y = originVertices[3].Position.Y = value;
					}
					if (InVertexUVChanged)
					{
						float value = FMath::Lerp(uvMinX, uvMaxX, fillAmount);
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(value, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(value, uvMaxY);
					}
				}
				else
				{
					if (InVertexPositionChanged)
					{
						float value = FMath::Lerp(posMaxX, posMinX, fillAmount);
						originVertices[0].Position.Y = originVertices[2].Position.Y = value;
					}
					if (InVertexUVChanged)
					{
						float value = FMath::Lerp(uvMaxX, uvMinX, fillAmount);
						vertices[0].TextureCoordinate[0] = FVector2f(value, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(value, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
			}
			else
			{
				if (flipDirection)
				{
					if (InVertexPositionChanged)
					{
						float value = FMath::Lerp(posMinY, posMaxY, fillAmount);
						originVertices[2].Position.Z = originVertices[3].Position.Z = value;
					}
					if (InVertexUVChanged)
					{
						float value = FMath::Lerp(uvMinY, uvMaxY, fillAmount);
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, value);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, value);
					}
				}
				else
				{
					if (InVertexPositionChanged)
					{
						float value = FMath::Lerp(posMaxY, posMinY, fillAmount);
						originVertices[0].Position.Z = originVertices[1].Position.Z = value;
					}
					if (InVertexUVChanged)
					{
						float value = FMath::Lerp(uvMaxY, uvMinY, fillAmount);
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, value);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, value);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
			}
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
#pragma endregion
#pragma region UISprite_Fill_Radial90
void FDreamUIGeometry::UpdateUIRectFillRadial90Vertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
	, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EDreamUISpriteFillOriginType_Radial90 originType
	, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	auto triangleCount = 6;
	DreamUIGeometrySetArrayNum(triangles, 6);
	if (InTriangleChanged)
	{
		triangles[0] = 0;
		triangles[1] = 3;
		triangles[2] = 2;
		triangles[3] = 0;
		triangles[4] = 1;
		triangles[5] = 3;
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto verticesCount = 4;
	DreamUIGeometrySetArrayNum(vertices, 4);
	DreamUIGeometrySetArrayNum(originVertices, verticesCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		//pivot offset
		float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
		CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
		//positions
		float posMinX = -halfW + pivotOffsetX;
		float posMinY = -halfH + pivotOffsetY;
		float posMaxX = halfW + pivotOffsetX;
		float posMaxY = halfH + pivotOffsetY;
		//uvs
		float uvMinX = spriteInfo.MinUV.X;
		float uvMinY = spriteInfo.MaxUV.Y;
		float uvMaxX = spriteInfo.MaxUV.X;
		float uvMaxY = spriteInfo.MinUV.Y;

		if (InVertexPositionChanged)
		{
			originVertices[0].Position = FVector3f(0, posMinX, posMinY);
			originVertices[1].Position = FVector3f(0, posMaxX, posMinY);
			originVertices[2].Position = FVector3f(0, posMinX, posMaxY);
			originVertices[3].Position = FVector3f(0, posMaxX, posMaxY);
			//snap pixel
			if (pixelPerfect)
			{
				AdjustPixelPerfectPos(originVertices, 0, verticesCount, renderCanvas, uiComp);

				posMinX = originVertices[0].Position.Y;
				posMinY = originVertices[0].Position.Z;
				posMaxX = originVertices[3].Position.Y;
				posMaxY = originVertices[3].Position.Z;
			}
		}
		switch (originType)
		{
		case EDreamUISpriteFillOriginType_Radial90::BottomLeft:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = FVector3f(0, posMaxX, FMath::Lerp(posMaxY, posMinY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = originVertices[3].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[1].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMaxY);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = originVertices[3].Position = FVector3f(0, posMaxX, FMath::Lerp(posMinY, posMaxY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
					}
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial90::TopLeft:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = originVertices[1].Position = FVector3f(0, posMaxX, FMath::Lerp(posMaxY, posMinY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = FVector3f(0, posMaxX, FMath::Lerp(posMinY, posMaxY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
					}
				}
				else
				{
					float lerpVaue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = originVertices[1].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpVaue), posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpVaue), uvMinY);
					}
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial90::TopRight:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = FVector3f(0, posMinX, FMath::Lerp(posMinY, posMaxY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = originVertices[0].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[2].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMinY);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = originVertices[0].Position = FVector3f(0, posMinX, FMath::Lerp(posMaxY, posMinY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[1].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
					}
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial90::BottomRight:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = originVertices[2].Position = FVector3f(0, posMinX, FMath::Lerp(posMinY, posMaxY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[3].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
					}
				}
			}
			else
			{
				if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = FVector3f(0, posMinX, FMath::Lerp(posMaxY, posMinY, lerpValue));
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 2.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = originVertices[2].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[0].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMaxY);
					}
				}
			}
		}
		break;
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
#pragma endregion
#pragma region UISprite_Fill_Radial180
void FDreamUIGeometry::UpdateUIRectFillRadial180Vertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
	, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EDreamUISpriteFillOriginType_Radial180 originType
	, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	auto triangleCount = 9;
	DreamUIGeometrySetArrayNum(triangles, 9);
	if (InTriangleChanged)
	{
		switch (originType)
		{
		case EDreamUISpriteFillOriginType_Radial180::Bottom:
		{
			triangles[0] = 4;
			triangles[1] = 2;
			triangles[2] = 0;

			triangles[3] = 4;
			triangles[4] = 3;
			triangles[5] = 2;

			triangles[6] = 4;
			triangles[7] = 1;
			triangles[8] = 3;
		}
		break;
		case EDreamUISpriteFillOriginType_Radial180::Left:
		{
			triangles[0] = 4;
			triangles[1] = 3;
			triangles[2] = 2;

			triangles[3] = 4;
			triangles[4] = 1;
			triangles[5] = 3;

			triangles[6] = 4;
			triangles[7] = 0;
			triangles[8] = 1;
		}
		break;
		case EDreamUISpriteFillOriginType_Radial180::Top:
		{
			triangles[0] = 4;
			triangles[1] = 1;
			triangles[2] = 3;

			triangles[3] = 4;
			triangles[4] = 0;
			triangles[5] = 1;

			triangles[6] = 4;
			triangles[7] = 2;
			triangles[8] = 0;
		}
		break;
		case EDreamUISpriteFillOriginType_Radial180::Right:
		{
			triangles[0] = 4;
			triangles[1] = 0;
			triangles[2] = 1;

			triangles[3] = 4;
			triangles[4] = 2;
			triangles[5] = 0;

			triangles[6] = 4;
			triangles[7] = 3;
			triangles[8] = 2;
		}
		break;
		}
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto verticesCount = 5;
	DreamUIGeometrySetArrayNum(vertices, 5);
	DreamUIGeometrySetArrayNum(originVertices, verticesCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		//pivot offset
		float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
		CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
		//positions
		float posMinX = -halfW + pivotOffsetX;
		float posMinY = -halfH + pivotOffsetY;
		float posMaxX = halfW + pivotOffsetX;
		float posMaxY = halfH + pivotOffsetY;
		//uvs
		float uvMinX = spriteInfo.MinUV.X;
		float uvMinY = spriteInfo.MaxUV.Y;
		float uvMaxX = spriteInfo.MaxUV.X;
		float uvMaxY = spriteInfo.MinUV.Y;

		if (InVertexPositionChanged)
		{
			originVertices[0].Position = FVector3f(0, posMinX, posMinY);
			originVertices[1].Position = FVector3f(0, posMaxX, posMinY);
			originVertices[2].Position = FVector3f(0, posMinX, posMaxY);
			originVertices[3].Position = FVector3f(0, posMaxX, posMaxY);
			//snap pixel
			if (pixelPerfect)
			{
				AdjustPixelPerfectPos(originVertices, 0, verticesCount - 1, renderCanvas, uiComp);

				posMinX = originVertices[0].Position.Y;
				posMinY = originVertices[0].Position.Z;
				posMaxX = originVertices[3].Position.Y;
				posMaxY = originVertices[3].Position.Z;
			}
		}
		switch (originType)
		{
		case EDreamUISpriteFillOriginType_Radial180::Bottom:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = FVector3f(0, posMaxX, FMath::Lerp(posMaxY, posMinY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMinY);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = originVertices[3].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMaxY);
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[1].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMinY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = originVertices[3].Position = originVertices[2].Position = FVector3f(0, posMinX, FMath::Lerp(posMinY, posMaxY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMinY);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = FVector3f(0, posMinX, FMath::Lerp(posMaxY, posMinY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMinY);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = originVertices[2].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMaxY);
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[0].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMinY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = originVertices[2].Position = originVertices[3].Position = FVector3f(0, posMaxX, FMath::Lerp(posMinY, posMaxY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMinY);
					}
					if (InVertexUVChanged)
					{
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[0].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMinY);
					}
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial180::Left:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMinY);
						originVertices[4].Position = FVector3f(0, posMinX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMinX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = originVertices[1].Position = FVector3f(0, posMaxX, FMath::Lerp(posMaxY, posMinY, lerpValue));
						originVertices[4].Position = FVector3f(0, posMinX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMinX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[0].Position = originVertices[1].Position = originVertices[3].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMaxY);
						originVertices[4].Position = FVector3f(0, posMinX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[0].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMinX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMaxY);
						originVertices[4].Position = FVector3f(0, posMinX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMinX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = originVertices[3].Position = FVector3f(0, posMaxX, FMath::Lerp(posMinY, posMaxY, lerpValue));
						originVertices[4].Position = FVector3f(0, posMinX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[4].TextureCoordinate[0] = FVector2f(uvMinX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = originVertices[3].Position = originVertices[1].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMinY);
						originVertices[4].Position = FVector3f(0, posMinX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[2].TextureCoordinate[0] = vertices[3].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMinY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMinX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial180::Top:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = FVector3f(0, posMinX, FMath::Lerp(posMinY, posMaxY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMaxY);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = originVertices[0].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMinY);
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[2].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[2].Position = originVertices[0].Position = originVertices[1].Position = FVector3f(0, posMaxX, FMath::Lerp(posMaxY, posMinY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[2].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMaxY);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = FVector3f(0, posMaxX, FMath::Lerp(posMinY, posMaxY, lerpValue));
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMaxY);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = originVertices[1].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMinY);
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[3].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMaxY);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = originVertices[1].Position = originVertices[0].Position = FVector3f(0, posMinX, FMath::Lerp(posMaxY, posMinY, lerpValue));
						originVertices[2].Position = FVector3f(0, posMinX, posMaxY);
						originVertices[4].Position = FVector3f(0, (posMinX + posMaxX) * 0.5f, posMaxY);
					}
					if (InVertexUVChanged)
					{
						vertices[3].TextureCoordinate[0] = vertices[1].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f((uvMinX + uvMaxX) * 0.5f, uvMaxY);
					}
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial180::Right:
		{
			if (flipDirection)
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMaxY);
						originVertices[4].Position = FVector3f(0, posMaxX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMaxX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = originVertices[2].Position = FVector3f(0, posMinX, FMath::Lerp(posMinY, posMaxY, lerpValue));
						originVertices[4].Position = FVector3f(0, posMaxX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[3].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMinY, uvMaxY, lerpValue));
						vertices[4].TextureCoordinate[0] = FVector2f(uvMaxX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[3].Position = originVertices[2].Position = originVertices[0].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMinY);
						originVertices[4].Position = FVector3f(0, posMaxX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[3].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMaxX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
			}
			else
			{
				if (fillAmount >= 0.666666666f)
				{
					float lerpValue = (fillAmount - 0.666666666f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = FVector3f(0, FMath::Lerp(posMinX, posMaxX, lerpValue), posMinY);
						originVertices[4].Position = FVector3f(0, posMaxX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
						vertices[1].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMinX, uvMaxX, lerpValue), uvMinY);
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMaxX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else if (fillAmount >= 0.33333333f)
				{
					float lerpValue = (fillAmount - 0.33333333f) * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = originVertices[0].Position = FVector3f(0, posMinX, FMath::Lerp(posMaxY, posMinY, lerpValue));
						originVertices[4].Position = FVector3f(0, posMaxX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[1].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, FMath::Lerp(uvMaxY, uvMinY, lerpValue));
						vertices[2].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMaxX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
				else
				{
					float lerpValue = fillAmount * 3.0f;
					if (InVertexPositionChanged)
					{
						originVertices[1].Position = originVertices[0].Position = originVertices[2].Position = FVector3f(0, FMath::Lerp(posMaxX, posMinX, lerpValue), posMaxY);
						originVertices[4].Position = FVector3f(0, posMaxX, (posMinY + posMaxY) * 0.5f);
					}
					if (InVertexUVChanged)
					{
						vertices[1].TextureCoordinate[0] = vertices[0].TextureCoordinate[0] = vertices[2].TextureCoordinate[0] = FVector2f(FMath::Lerp(uvMaxX, uvMinX, lerpValue), uvMaxY);
						vertices[3].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
						vertices[4].TextureCoordinate[0] = FVector2f(uvMaxX, (uvMinY + uvMaxY) * 0.5f);
					}
				}
			}
		}
		break;
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
#pragma endregion
#pragma region UISprite_Fill_Radial360
void FDreamUIGeometry::UpdateUIRectFillRadial360Vertex(FDreamUIGeometry* uiGeo, float width, float height, const FVector2f& pivot
	, const FDreamUISpriteInfo& spriteInfo, bool flipDirection, float fillAmount, EDreamUISpriteFillOriginType_Radial360 originType
	, UDreamCanvas* renderCanvas, UDreamVisual* uiComp, FColor color,
	bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
{
	auto& triangles = uiGeo->Triangles;
	auto triangleCount = 24;
	DreamUIGeometrySetArrayNum(triangles, 24);
	if (InTriangleChanged)
	{
		triangles[0] = 4;
		triangles[1] = 1;
		triangles[2] = 2;

		triangles[3] = 4;
		triangles[4] = 0;
		triangles[5] = 1;

		triangles[6] = 4;
		triangles[7] = 3;
		triangles[8] = 0;

		triangles[9] = 4;
		triangles[10] = 6;
		triangles[11] = 3;

		triangles[12] = 4;
		triangles[13] = 7;
		triangles[14] = 6;

		triangles[15] = 4;
		triangles[16] = 8;
		triangles[17] = 7;

		triangles[18] = 4;
		triangles[19] = 5;
		triangles[20] = 8;

		triangles[21] = 4;
		triangles[22] = 2;
		triangles[23] = 5;

		switch (originType)
		{
		case EDreamUISpriteFillOriginType_Radial360::Bottom:
			triangles[1] = 9;
			break;
		case EDreamUISpriteFillOriginType_Radial360::Right:
			triangles[19] = 9;
			break;
		case EDreamUISpriteFillOriginType_Radial360::Top:
			triangles[13] = 9;
			break;
		case EDreamUISpriteFillOriginType_Radial360::Left:
			triangles[7] = 9;
			break;
		}
	}

	bool pixelPerfect = uiComp->GetShouldAffectByPixelSnapping() && uiComp->GetWidget()->GetPixelSnappingInHierarchy();
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto verticesCount = 10;
	DreamUIGeometrySetArrayNum(vertices, verticesCount);
	DreamUIGeometrySetArrayNum(originVertices, verticesCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		//pivot offset
		float pivotOffsetX = 0, pivotOffsetY = 0, halfW = 0, halfH = 0;
		CalculateOffsetAndSize(width, height, pivot, spriteInfo, pivotOffsetX, pivotOffsetY, halfW, halfH);
		//positions
		float posMinX = -halfW + pivotOffsetX;
		float posMinY = -halfH + pivotOffsetY;
		float posMaxX = halfW + pivotOffsetX;
		float posMaxY = halfH + pivotOffsetY;
		float posHalfX = (posMinX + posMaxX) * 0.5f;
		float posHalfY = (posMinY + posMaxY) * 0.5f;
		//uvs
		float uvMinX = spriteInfo.MinUV.X;
		float uvMinY = spriteInfo.MaxUV.Y;
		float uvMaxX = spriteInfo.MaxUV.X;
		float uvMaxY = spriteInfo.MinUV.Y;
		float uvHalfX = (uvMinX + uvMaxX) * 0.5f;
		float uvHalfY = (uvMinY + uvMaxY) * 0.5f;

		//reset position
		{
			originVertices[0].Position = FVector3f(0, posMinX, posMinY);
			originVertices[2].Position = FVector3f(0, posMaxX, posMinY);
			originVertices[6].Position = FVector3f(0, posMinX, posMaxY);
			originVertices[8].Position = FVector3f(0, posMaxX, posMaxY);
			//snap pixel
			if (pixelPerfect)
			{
				AdjustPixelPerfectPos_For_UIRectFillRadial360(originVertices, renderCanvas, uiComp);

				posMinX = originVertices[0].Position.Y;
				posMaxX = originVertices[2].Position.Y;
				posMinY = originVertices[0].Position.Z;
				posMaxY = originVertices[6].Position.Z;
				posHalfX = (posMinX + posMaxX) * 0.5f;
				posHalfY = (posMinY + posMaxY) * 0.5f;
			}

			originVertices[1].Position = FVector3f(0, posHalfX, posMinY);
			originVertices[3].Position = FVector3f(0, posMinX, posHalfY);
			originVertices[4].Position = FVector3f(0, posHalfX, posHalfY);
			originVertices[5].Position = FVector3f(0, posMaxX, posHalfY);
			originVertices[7].Position = FVector3f(0, posHalfX, posMaxY);
		}
		//reset uv
		{
			vertices[0].TextureCoordinate[0] = FVector2f(uvMinX, uvMinY);
			vertices[1].TextureCoordinate[0] = FVector2f(uvHalfX, uvMinY);
			vertices[2].TextureCoordinate[0] = FVector2f(uvMaxX, uvMinY);
			vertices[3].TextureCoordinate[0] = FVector2f(uvMinX, uvHalfY);
			vertices[4].TextureCoordinate[0] = FVector2f(uvHalfX, uvHalfY);
			vertices[5].TextureCoordinate[0] = FVector2f(uvMaxX, uvHalfY);
			vertices[6].TextureCoordinate[0] = FVector2f(uvMinX, uvMaxY);
			vertices[7].TextureCoordinate[0] = FVector2f(uvHalfX, uvMaxY);
			vertices[8].TextureCoordinate[0] = FVector2f(uvMaxX, uvMaxY);
		}

		auto setPosAndUv = [&](int changeIndex, bool xory, float posFrom, float uvFrom, float lerpValue, const TArray<int>& inVertIndexArray) {
			auto& pos = originVertices[changeIndex].Position;
			auto& uv = vertices[changeIndex].TextureCoordinate[0];
			if (xory)
			{
				pos.Y = FMath::Lerp(posFrom, pos.Y, lerpValue);
				uv.X = FMath::Lerp(uvFrom, uv.X, lerpValue);
			}
			else
			{
				pos.Z = FMath::Lerp(posFrom, pos.Z, lerpValue);
				uv.Y = FMath::Lerp(uvFrom, uv.Y, lerpValue);
			}
			for (int i : inVertIndexArray)
			{
				originVertices[i].Position = pos;
				vertices[i].TextureCoordinate[0] = uv;
			}
		};
		switch (originType)
		{
		case EDreamUISpriteFillOriginType_Radial360::Bottom:
		{
			originVertices[9].Position = originVertices[1].Position;
			vertices[9].TextureCoordinate[0] = vertices[1].TextureCoordinate[0];
			if (flipDirection)
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(9, true, posMaxX, uvMaxX, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(2, false, posHalfY, uvHalfY, lerpValue, { 9 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(5, false, posMaxY, uvMaxY, lerpValue, { 9, 2 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(8, true, posHalfX, uvHalfX, lerpValue, { 9, 2, 5 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(7, true, posMinX, uvMinX, lerpValue, { 9, 2, 5, 8 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(6, false, posHalfY, uvHalfY, lerpValue, { 9, 2, 5, 8, 7 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(3, false, posMinY, uvMinY, lerpValue, { 9, 2, 5, 8, 7, 6 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(0, true, posHalfX, uvHalfX, lerpValue, { 9, 2, 5, 8, 7, 6, 3 });
				}
			}
			else
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(1, true, posMinX, uvMinX, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(0, false, posHalfY, uvHalfY, lerpValue, { 1 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(3, false, posMaxY, uvMaxY, lerpValue, { 1, 0 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(6, true, posHalfX, uvHalfX, lerpValue, { 1, 0, 3 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(7, true, posMaxX, uvMaxX, lerpValue, { 1, 0, 3, 6 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(8, false, posHalfY, uvHalfY, lerpValue, { 1, 0, 3, 6, 7 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(5, false, posMinY, uvMinY, lerpValue, { 1, 0, 3, 6, 7, 8 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(2, true, posHalfX, uvHalfX, lerpValue, { 1, 0, 3, 6, 7, 8, 5 });
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial360::Right:
		{
			originVertices[9].Position = originVertices[5].Position;
			vertices[9].TextureCoordinate[0] = vertices[5].TextureCoordinate[0];
			if (flipDirection)
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(9, false, posMaxY, uvMaxY, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(8, true, posHalfX, uvHalfX, lerpValue, { 9 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(7, true, posMinX, uvMinX, lerpValue, { 9, 8 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(6, false, posHalfY, uvHalfY, lerpValue, { 9, 8, 7 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(3, false, posMinY, uvMinY, lerpValue, { 9, 8, 7, 6 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(0, true, posHalfX, uvHalfX, lerpValue, { 9, 8, 7, 6, 3 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(1, true, posMaxX, uvMaxX, lerpValue, { 9, 8, 7, 6, 3, 0 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(2, false, posHalfY, uvHalfY, lerpValue, { 9, 8, 7, 6, 3, 0, 1 });
				}
			}
			else
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(5, false, posMinY, uvMinY, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(2, true, posHalfX, uvHalfX, lerpValue, { 5 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(1, true, posMinX, uvMinX, lerpValue, { 5, 2 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(0, false, posHalfY, uvHalfY, lerpValue, { 5, 2, 1 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(3, false, posMaxY, uvMaxY, lerpValue, { 5, 2, 1, 0 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(6, true, posHalfX, uvHalfX, lerpValue, { 5, 2, 1, 0, 3 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(7, true, posMaxX, uvMaxX, lerpValue, { 5, 2, 1, 0, 3, 6 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(8, false, posHalfY, uvHalfY, lerpValue, { 5, 2, 1, 0, 3, 6, 7 });
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial360::Top:
		{
			originVertices[9].Position = originVertices[7].Position;
			vertices[9].TextureCoordinate[0] = vertices[7].TextureCoordinate[0];
			if (flipDirection)
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(9, true, posMinX, uvMinX, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(6, false, posHalfY, uvHalfY, lerpValue, { 9 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(3, false, posMinY, uvMinY, lerpValue, { 9, 6 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(0, true, posHalfX, uvHalfX, lerpValue, { 9, 6, 3 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(1, true, posMaxX, uvMaxX, lerpValue, { 9, 6, 3, 0 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(2, false, posHalfY, uvHalfY, lerpValue, { 9, 6, 3, 0, 1 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(5, false, posMaxY, uvMaxY, lerpValue, { 9, 6, 3, 0, 1, 2 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(8, true, posHalfX, uvHalfX, lerpValue, { 9, 6, 3, 0, 1, 2, 5 });
				}
			}
			else
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(7, true, posMaxX, uvMaxX, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(8, false, posHalfY, uvHalfY, lerpValue, { 7 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(5, false, posMinY, uvMinY, lerpValue, { 7, 8 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(2, true, posHalfX, uvHalfX, lerpValue, { 7, 8, 5 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(1, true, posMinX, uvMinX, lerpValue, { 7, 8, 5, 2 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(0, false, posHalfY, uvHalfY, lerpValue, { 7, 8, 5, 2, 1 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(3, false, posMaxY, uvMaxY, lerpValue, { 7, 8, 5, 2, 1, 0 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(6, true, posHalfX, uvHalfX, lerpValue, { 7, 8, 5, 2, 1, 0, 3 });
				}
			}
		}
		break;
		case EDreamUISpriteFillOriginType_Radial360::Left:
		{
			originVertices[9].Position = originVertices[3].Position;
			vertices[9].TextureCoordinate[0] = vertices[3].TextureCoordinate[0];
			if (flipDirection)
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(9, false, posMinY, uvMinY, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(0, true, posHalfX, uvHalfX, lerpValue, { 9 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(1, true, posMaxX, uvMaxX, lerpValue, { 9, 0 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(2, false, posHalfY, uvHalfY, lerpValue, { 9, 0, 1 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(5, false, posMaxY, uvMaxY, lerpValue, { 9, 0, 1, 2 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(8, true, posHalfX, uvHalfX, lerpValue, { 9, 0, 1, 2, 5 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(7, true, posMinX, uvMinX, lerpValue, { 9, 0, 1, 2, 5, 8 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(6, false, posHalfY, uvHalfY, lerpValue, { 9, 0, 1, 2, 5, 8, 7 });
				}
			}
			else
			{
				if (fillAmount >= 0.875f)
				{
					float lerpValue = (fillAmount - 0.875f) * 8.0f;
					setPosAndUv(3, false, posMaxY, uvMaxY, lerpValue, {});
				}
				else if (fillAmount >= 0.75f)
				{
					float lerpValue = (fillAmount - 0.75f) * 8.0f;
					setPosAndUv(6, true, posHalfX, uvHalfX, lerpValue, { 3 });
				}
				else if (fillAmount >= 0.625f)
				{
					float lerpValue = (fillAmount - 0.625f) * 8.0f;
					setPosAndUv(7, true, posMaxX, uvMaxX, lerpValue, { 3, 6 });
				}
				else if (fillAmount >= 0.5f)
				{
					float lerpValue = (fillAmount - 0.5f) * 8.0f;
					setPosAndUv(8, false, posHalfY, uvHalfY, lerpValue, { 3, 6, 7 });
				}
				else if (fillAmount >= 0.375f)
				{
					float lerpValue = (fillAmount - 0.375f) * 8.0f;
					setPosAndUv(5, false, posMinY, uvMinY, lerpValue, { 3, 6, 7, 8 });
				}
				else if (fillAmount >= 0.25f)
				{
					float lerpValue = (fillAmount - 0.25f) * 8.0f;
					setPosAndUv(2, true, posHalfX, uvHalfX, lerpValue, { 3, 6, 7, 8, 5 });
				}
				else if (fillAmount >= 0.125f)
				{
					float lerpValue = (fillAmount - 0.125f) * 8.0f;
					setPosAndUv(1, true, posMinX, uvMinX, lerpValue, { 3, 6, 7, 8, 5, 2 });
				}
				else
				{
					float lerpValue = fillAmount * 8.0f;
					setPosAndUv(0, false, posHalfY, uvHalfY, lerpValue, { 3, 6, 7, 8, 5, 2, 1 });
				}
			}
		}
		break;
		}

		if (InVertexColorChanged)
		{
			UpdateUIColor(uiGeo, color);
		}

		//additional data
		{
			//normal & tangent
			if (renderCanvas->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < originVertices.Num(); i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}
#pragma endregion



void FDreamUIGeometry::OffsetVertices(TArray<FDreamUIOriginVertexData>& vertices, int count, float offsetX, float offsetY)
{
	for (int i = 0; i < count; i++)
	{
		auto& vertex = vertices[i].Position;
		vertex.Y += offsetX;
		vertex.Z += offsetY;
	}
}
void FDreamUIGeometry::UpdateUIColor(FDreamUIGeometry* uiGeo, FColor color)
{
	auto& vertices = uiGeo->Vertices;
	for (int i = 0; i < vertices.Num(); i++)
	{
		vertices[i].Color = color;
	}
}

void FDreamUIGeometry::CalculatePivotOffset(
	float width, float height, const FVector2f& pivot
	, float& pivotOffsetX, float& pivotOffsetY
)
{
	pivotOffsetX = width * (0.5f - pivot.X);//width * 0.5f *(1 - pivot.X * 2)
	pivotOffsetY = height * (0.5f - pivot.Y);//height * 0.5f *(1 - pivot.Y * 2)
}

void FDreamUIGeometry::CalculateOffsetAndSize(
	float width, float height, const FVector2f& pivot, const FDreamUISpriteInfo& spriteInfo
	, float& pivotOffsetX, float& pivotOffsetY, float& halfWidth, float& halfHeight
)
{
	CalculatePivotOffset(width, height, pivot, pivotOffsetX, pivotOffsetY);

	if (spriteInfo.HasPadding())
	{
		float widthScale = width / spriteInfo.GetSourceWidth();
		float heightScale = height / spriteInfo.GetSourceHeight();
		float geoWidth = spriteInfo.Width * widthScale;
		float geoHeight = spriteInfo.Height * heightScale;
		pivotOffsetX += (-width + geoWidth) * 0.5f + spriteInfo.Padding.Left * widthScale;
		pivotOffsetY += (-height + geoHeight) * 0.5f + spriteInfo.Padding.Bottom * heightScale;
		halfWidth = geoWidth * 0.5f;
		halfHeight = geoHeight * 0.5f;
	}
	else
	{
		halfWidth = width * 0.5f;
		halfHeight = height * 0.5f;
	}
}


void FDreamUIGeometry::TransformVertices(UDreamCanvas* canvas, UDreamVisual* item, FDreamUIGeometry* uiGeo)
{
	auto& vertices = uiGeo->Vertices;
	auto& originVertices = uiGeo->OriginVertices;
	auto vertexCount = vertices.Num();
	auto originVertexCount = originVertices.Num();
	if (originVertexCount > vertexCount)
	{
		originVertices.RemoveAt(vertexCount, originVertexCount - vertexCount);
	}
	else if (originVertexCount < vertexCount)
	{
		originVertices.AddDefaulted(vertexCount - originVertexCount);
	}

	auto inverseCanvasTf = canvas->GetWidget()->GetWorldTransform().Inverse();
	const auto& itemTf = item->GetWidget()->GetWorldTransform();
	FTransform itemToCanvasTf;
	FTransform::Multiply(&itemToCanvasTf, &itemTf, &inverseCanvasTf);
	uiGeo->TransformRelativeToCanvas = itemToCanvasTf;
	auto itemToCanvasTf2D = UDreamCanvas::ConvertTo2DTransform(itemToCanvasTf);
	FVector2D itemMin, itemMax;
	UDreamCanvas::CalculateVisual2DBounds(item, itemToCanvasTf2D, itemMin, itemMax);
	uiGeo->BoundsMin2DInCanvasSpace = itemMin;
	uiGeo->BoundsMax2DInCanvasSpace = itemMax;

	if (item->GetWidget()->HasPerspectiveApplied())
	{
		// Inside a perspective scope the widget is drawn somewhere its FTransform does not describe,
		// so the positions and the bounds both have to come from the remapped geometry. The bounds
		// matter as much as the vertices: they drive batching overlap and culling, and bounds that
		// still described the un-foreshortened rect would cull widgets that are plainly on screen.
		const FMatrix ItemToCanvasMatrix = item->GetWidget()->GetWorldMatrix() * inverseCanvasTf.ToMatrixWithScale();
		FVector2D RemappedMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D RemappedMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
		for (int i = 0; i < vertexCount; i++)
		{
			const FVector Remapped = ItemToCanvasMatrix.TransformPosition(FVector(originVertices[i].Position));
			vertices[i].Position = FVector3f(Remapped);
			RemappedMin.X = FMath::Min(RemappedMin.X, Remapped.Y);
			RemappedMin.Y = FMath::Min(RemappedMin.Y, Remapped.Z);
			RemappedMax.X = FMath::Max(RemappedMax.X, Remapped.Y);
			RemappedMax.Y = FMath::Max(RemappedMax.Y, Remapped.Z);
		}
		if (vertexCount > 0)
		{
			uiGeo->BoundsMin2DInCanvasSpace = RemappedMin;
			uiGeo->BoundsMax2DInCanvasSpace = RemappedMax;
		}
	}
	else
	{
		// Untouched: FMatrix and FTransform do not agree bit for bit, and every existing expectation
		// about geometry -- pixel snapping above all -- was formed against this line.
		for (int i = 0; i < vertexCount; i++)
		{
			vertices[i].Position = FVector3f(itemToCanvasTf.TransformPosition(FVector(originVertices[i].Position)));
		}
	}

	if (canvas->GetActualRequireNormalAndTangent())
	{
		for (int i = 0; i < vertexCount; i++)
		{
			vertices[i].TangentZ = itemToCanvasTf.TransformVector(FVector(originVertices[i].Normal));
			vertices[i].TangentZ.Vector.W = -127;

			vertices[i].TangentX = itemToCanvasTf.TransformVector(FVector(originVertices[i].Tangent));
		}
	}
}


