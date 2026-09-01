// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamPolygon.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"


UDreamPolygon::UDreamPolygon(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	
}

void UDreamPolygon::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	Sides = FMath::Max(Sides, FullCycle ? 3 : 1);

	auto& triangles = InGeo.Triangles;
	auto triangleCount = Sides * 3;
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(triangles, triangleCount);
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
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(vertices, vertexCount);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(originVertices, vertexCount);
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
			FDreamUIGeometry::CalculatePivotOffset(width, height, pivot, pivotOffsetX, pivotOffsetY);
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
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				auto& SpriteInfo = DreamSprite->GetSpriteInfo();
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
			case EDreamPolygonUVType::SpriteRect:
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
			case EDreamPolygonUVType::HeightCenter:
			{
				vertices[0].TextureCoordinate[0] = FVector2f(MinUV.X, (MaxUV.Y + MinUV.Y) * 0.5f);
				FVector2f otherUV(MaxUV.X, (MaxUV.Y + MinUV.Y) * 0.5f);
				for (int i = 1; i < vertexCount; i++)
				{
					vertices[i].TextureCoordinate[0] = otherUV;
				}
			}
			break;
			case EDreamPolygonUVType::StretchSpriteHeight:
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
			FDreamUIGeometry::UpdateUIColor(&InGeo, GetFinalColor());
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

/*
 * Closing the cycle raises the floor under Sides from one to three, so the count has to be re-run
 * through the clamp here as well as in SetSides. Without it the pair had a hole:
 * SetFullCycle(false), SetSides(1), SetFullCycle(true) left a closed polygon claiming a single
 * side -- a state its own invariant forbids. Nothing drew wrong, because OnUpdateGeometry re-clamps
 * before it uses the number, but GetSides is BlueprintCallable and anything sizing an offset array
 * from it was told 1.
 */
void UDreamPolygon::SetFullCycle(bool value) {
	if (FullCycle != value)
	{
		FullCycle = value;
		Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
		MarkVerticesDirty(true, true, true, false);
	}
}
void UDreamPolygon::SetStartAngle(float value) {
	if (StartAngle != value)
	{
		StartAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void UDreamPolygon::SetEndAngle(float value) {
	if (EndAngle != value)
	{
		EndAngle = value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void UDreamPolygon::SetSides(int value) {
	if (Sides != value)
	{
		Sides = value;
		Sides = FMath::Max(Sides, FullCycle ? 3 : 1);
		MarkVerticesDirty(true, true, true, true);
	}
}
void UDreamPolygon::SetUVType(EDreamPolygonUVType value)
{
	if (UVType != value)
	{
		UVType = value;
		MarkVertexUVDirty();
	}
}
/*
 * The length this refuses to differ from is the one the SHAPE implies, not the one the member
 * happens to be holding. That is the fix, and the difference is the whole defect.
 *
 * Measured against the stored array, the requirement was unsatisfiable in the obvious order. The
 * array is only ever sized inside OnUpdateGeometry, so it starts empty and stays empty until the
 * polygon has been drawn once -- and the natural authoring sequence, pick a side count then hand
 * over one offset per side, was rejected every time with a message naming a length no Blueprint
 * could produce. The only working route was GetVertexOffsetArray_Direct, which is not a UFUNCTION
 * and therefore not reachable from Blueprint at all.
 *
 * Measured against Sides it is a question the caller can answer in advance, which is what makes the
 * refusal a contract rather than a deadlock. The count is the same expression the geometry pass
 * sizes the array with: one offset per ring vertex, plus the extra vertex an open fan needs to
 * close its far edge.
 *
 * Still a refusal and not a resize: an array of the wrong length is an author mistake about which
 * corners they are moving, and silently padding it with the default would put the shape somewhere
 * nobody asked for while reporting success.
 */
void UDreamPolygon::SetVertexOffsetArray(const TArray<float>& value)
{
	const int32 ExpectedCount = FullCycle ? Sides : (Sides + 1);
	if (value.Num() == ExpectedCount)
	{
		VertexOffsetArray = value;
		MarkVertexPositionDirty();
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Array count not equal! Expected:%d for %d sides, value:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ExpectedCount, Sides, value.Num());
	}
}
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWidgetRegistry.h"
UDreamTweener* UDreamPolygon::StartAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, EDreamTweenEase easeType /* = EDreamTweenEase::OutCubic */)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamPolygon::GetStartAngle), FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamPolygon::SetStartAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamPolygon::EndAngleTo(float endValue, float duration /* = 0.5f */, float delay /* = 0.0f */, EDreamTweenEase easeType /* = EDreamTweenEase::OutCubic */)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamPolygon::GetEndAngle), FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamPolygon::SetEndAngle), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(easeType)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}

/*
 * A polygon has no natural size, and it has to say so out loud.
 *
 * It derives from UDreamImage purely to reuse the brush as a source of pixels to paint with, and
 * UDreamImage answers the measure question with Brush.ImageSize -- the size of that source. For an
 * image that is the whole point; for a polygon it is a number about the texture being sampled,
 * which has nothing to do with how big the shape is. The vertices come from the rect instead
 * (OnUpdateGeometry: halfW = Widget->GetWidth() * 0.5f), so the shape is whatever size it is given.
 *
 * Inherited, the wrong answer is silent and it is confident: a decagon drawn 400px across, sitting
 * in an Auto slot with the default 32x32 brush, gets measured at 32 and squeezed to a dot. Nothing
 * logs, and the brush is the last place anyone would look.
 *
 * So: abstain. A size-driven shape has no opinion about its own size, and -1 lets the layout fall
 * back to the authored rect, which IS what the polygon is going to draw itself into.
 */
float UDreamPolygon::GetPreferredWidth() const
{
	return -1;
}

float UDreamPolygon::GetPreferredHeight() const
{
	return -1;
}

DECLARE_DREAM_GUI_VISUAL("Polygon", UDreamPolygon)
