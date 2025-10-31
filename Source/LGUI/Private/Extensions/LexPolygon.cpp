// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LexPolygon.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "LTweenManager.h"
#include "Core/Components/LexCanvas.h"



ULexPolygon::ULexPolygon(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	
}

void ULexPolygon::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	Sides = FMath::Max(Sides, FullCycle ? 3 : 1);

	auto& triangles = InGeo.Triangles;
	auto triangleCount = Sides * 3;
	FLexUIGeometry::LexUIGeometrySetArrayNum(triangles, triangleCount);
	if (InTriangleChanged)
	{
		int index = 0;
		if (FullCycle)
		{
			for (int i = 0; i < Sides - 1; i++)
			{
				triangles[index++] = 0;
				triangles[index++] = i + 1;
				triangles[index++] = i + 2;
			}
			triangles[index++] = 0;
			triangles[index++] = Sides;
			triangles[index++] = 1;
		}
		else
		{
			for (int i = 0; i < Sides; i++)
			{
				triangles[index++] = 0;
				triangles[index++] = i + 1;
				triangles[index++] = i + 2;
			}
		}
	}

	auto Widget = GetWidget();
	auto& vertices = InGeo.Vertices;
	auto& originVertices = InGeo.OriginVertices;
	int vertexCount = (FullCycle ? 1 : 2) + Sides;
	FLexUIGeometry::LexUIGeometrySetArrayNum(vertices, vertexCount);
	FLexUIGeometry::LexUIGeometrySetArrayNum(originVertices, vertexCount);
	if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
	{
		//vert offset
		int VertexOffsetCount = FullCycle ? Sides : (Sides + 1);
		if (VertexOffsetArray.Num() != VertexOffsetCount)
		{
			if (VertexOffsetArray.Num() > VertexOffsetCount)
			{
				VertexOffsetArray.SetNumZeroed(VertexOffsetCount);
			}
			else
			{
				for (int i = VertexOffsetArray.Num(); i < VertexOffsetCount; i++)
				{
					VertexOffsetArray.Add(1.0f);
				}
			}
		}

		float calcStartAngle = StartAngle, calcEndAngle = EndAngle;
		if (InVertexPositionChanged)
		{
			auto width = Widget->GetWidth();
			auto height = Widget->GetHeight();
			auto pivot = FVector2f(Widget->GetPivot());
			//pivot offset
			float pivotOffsetX = 0, pivotOffsetY = 0;
			FLexUIGeometry::CalculatePivotOffset(width, height, pivot, pivotOffsetX, pivotOffsetY);
			float halfW = width * 0.5f;
			float halfH = height * 0.5f;

			if (FullCycle)calcEndAngle = calcStartAngle + 360.0f;
			float singleAngle = FMath::DegreesToRadians((calcEndAngle - calcStartAngle) / Sides);
			float angle = FMath::DegreesToRadians(calcStartAngle);

			float sin = FMath::Sin(angle);
			float cos = FMath::Cos(angle);

			float x = pivotOffsetX;
			float y = pivotOffsetY;
			originVertices[0].Position = FVector3f(0, x, y);

			for (int i = 0, count = Sides; i < count; i++)
			{
				sin = FMath::Sin(angle);
				cos = FMath::Cos(angle);
				x = cos * halfW * VertexOffsetArray[i] + pivotOffsetX;
				y = sin * halfH * VertexOffsetArray[i] + pivotOffsetY;
				originVertices[i + 1].Position = FVector3f(0, x, y);
				angle += singleAngle;
			}
			if (!FullCycle)
			{
				sin = FMath::Sin(angle);
				cos = FMath::Cos(angle);
				x = cos * halfW * VertexOffsetArray[Sides] + pivotOffsetX;
				y = sin * halfH * VertexOffsetArray[Sides] + pivotOffsetY;
				originVertices[Sides + 1].Position = FVector3f(0, x, y);
			}
		}

		if (InVertexUVChanged)
		{
			FVector2f MinUV;
			FVector2f MaxUV;
			if (bHasAddToSprite)
			{
				auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
				auto& SpriteInfo = LexSprite->GetSpriteInfo();
				MinUV = FVector2f(SpriteInfo.MinUV.X, SpriteInfo.MaxUV.Y);
				MaxUV = FVector2f(SpriteInfo.MaxUV.X, SpriteInfo.MinUV.Y);
			}
			else
			{
				MinUV = FVector2f(Brush.UVRegion.X, Brush.UVRegion.Y);
				MaxUV = FVector2f(Brush.UVRegion.Z, Brush.UVRegion.W);
			}
			// auto spriteInfo = this->GetSprite()->GetSpriteInfo();
			switch (UVType)
			{
			case ELexPolygonUVType::SpriteRect:
			{
				if (FullCycle)calcEndAngle = calcStartAngle + 360.0f;
				float singleAngle = FMath::DegreesToRadians((calcEndAngle - calcStartAngle) / Sides);
				float angle = FMath::DegreesToRadians(calcStartAngle);

				float sin = FMath::Sin(angle);
				float cos = FMath::Cos(angle);

				float halfUVWidth = (MaxUV.X - MinUV.X) * 0.5f;
				float halfUVHeight = (MinUV.Y - MaxUV.Y) * 0.5f;
				float centerUVX = (MinUV.X + MaxUV.X) * 0.5f;
				float centerUVY = (MaxUV.Y + MinUV.Y) * 0.5f;

				float x = centerUVX;
				float y = centerUVY;
				vertices[0].TextureCoordinate[0] = FVector2f(x, y);

				int count = FullCycle ? Sides : (Sides + 1);
				for (int i = 0; i < count; i++)
				{
					sin = FMath::Sin(angle);
					cos = FMath::Cos(angle);
					x = cos * halfUVWidth + centerUVX;
					y = sin * halfUVHeight + centerUVY;
					vertices[i + 1].TextureCoordinate[0] = FVector2f(x, y);
					angle += singleAngle;
				}
			}
			break;
			case ELexPolygonUVType::HeightCenter:
			{
				vertices[0].TextureCoordinate[0] = FVector2f(MinUV.X, (MaxUV.Y + MinUV.Y) * 0.5f);
				FVector2f otherUV(MaxUV.X, (MaxUV.Y + MinUV.Y) * 0.5f);
				for (int i = 1; i < vertexCount; i++)
				{
					vertices[i].TextureCoordinate[0] = otherUV;
				}
			}
			break;
			case ELexPolygonUVType::StretchSpriteHeight:
			{
				vertices[0].TextureCoordinate[0] = FVector2f(MinUV.X, (MaxUV.Y + MinUV.Y) * 0.5f);
				float uvX = MaxUV.X;
				float uvY = MaxUV.Y;
				float uvYInterval = (MinUV.Y - MaxUV.Y) / (vertexCount - 2);
				for (int i = 1; i < vertexCount; i++)
				{
					auto& uv = vertices[i].TextureCoordinate[0];
					uv.X = uvX;
					uv.Y = uvY;
					uvY += uvYInterval;
				}
			}
			break;
			}
		}

		if (InVertexColorChanged)
		{
			FLexUIGeometry::UpdateUIColor(&InGeo, GetFinalColor());
		}

		//additional data
		{
			//normal & tangent
			if (Widget->GetRenderCanvas()->GetActualRequireNormalAndTangent())
			{
				for (int i = 0; i < vertexCount; i++)
				{
					originVertices[i].Normal = FVector3f(-1, 0, 0);
					originVertices[i].Tangent = FVector3f(0, 1, 0);
				}
			}
		}
	}
}

void ULexPolygon::SetFullCycle(bool value) {
	if (FullCycle != value)
	{
		FullCycle = value;
		MarkVerticesDirty(true, true, true, false);
	}
}
void ULexPolygon::SetStartAngle(float value) {
	if (StartAngle != value)
	{
		StartAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void ULexPolygon::SetEndAngle(float value) {
	if (EndAngle != value)
	{
		EndAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void ULexPolygon::SetSides(int value) {
	if (Sides != value)
	{
		Sides = value;
		Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
		MarkVerticesDirty(true, true, true, true);
	}
}
void ULexPolygon::SetUVType(ELexPolygonUVType value)
{
	if (UVType != value)
	{
		UVType = value;
		MarkUVDirty();
	}
}
void ULexPolygon::SetVertexOffsetArray(const TArray<float>& value)
{
	if (VertexOffsetArray.Num() == value.Num())
	{
		VertexOffsetArray = value;
		MarkVertexPositionDirty();
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Array count not equal! VertexOffsetArray:%d, value:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, VertexOffsetArray.Num(), value.Num());
	}
}
#include "Core/LexUISettings.h"
ULTweener* ULexPolygon::StartAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, ELTweenEase easeType /* = ELTweenEase::OutCubic */)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexPolygon::GetStartAngle), FLTweenFloatSetterFunction::CreateUObject(this, &ULexPolygon::SetStartAngle), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(easeType)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexPolygon::EndAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, ELTweenEase easeType /* = ELTweenEase::OutCubic */)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexPolygon::GetEndAngle), FLTweenFloatSetterFunction::CreateUObject(this, &ULexPolygon::SetEndAngle), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(easeType)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}

