// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIImageBrush.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteData.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUIWidgetRegistry.h"

// A Filled brush reaches the same geometry builders UDreamSprite fills with, so the two elements cut
// an image the same way. The brush carries its own enum rather than including a component header for
// one, and these keep the two spellings from drifting apart.
static_assert((int32)EDreamUIImageBrushFillMethod::Horizontal == (int32)EDreamUISpriteFillMethod::Horizontal, "brush and sprite fill methods must agree");
static_assert((int32)EDreamUIImageBrushFillMethod::Vertical == (int32)EDreamUISpriteFillMethod::Vertical, "brush and sprite fill methods must agree");
static_assert((int32)EDreamUIImageBrushFillMethod::Radial90 == (int32)EDreamUISpriteFillMethod::Radial90, "brush and sprite fill methods must agree");
static_assert((int32)EDreamUIImageBrushFillMethod::Radial180 == (int32)EDreamUISpriteFillMethod::Radial180, "brush and sprite fill methods must agree");
static_assert((int32)EDreamUIImageBrushFillMethod::Radial360 == (int32)EDreamUISpriteFillMethod::Radial360, "brush and sprite fill methods must agree");

void UDreamImage::BuildRoundedBoxGeometry(FDreamUIGeometry* UIGeo
	, float Width, float Height, const FVector2f& Pivot
	, const FDreamUISpriteInfo& SpriteInfo, const FVector4f& InCornerRadius, int32 InCornerSegments
	, FColor Color, bool bRequireNormalAndTangent
	, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged
)
	{
		const int32 Segments = FMath::Clamp(InCornerSegments, 1, 32);
		const int32 PointsPerCorner = Segments + 1;//both ends of the arc belong to it
		const int32 RingCount = PointsPerCorner * 4;
		const int32 VertexCount = RingCount + 1;//the centre the fan turns around

		auto& Triangles = UIGeo->Triangles;
		FDreamUIGeometry::DreamUIGeometrySetArrayNum(Triangles, RingCount * 3);
		if (InTriangleChanged)
		{
			for (int32 i = 0; i < RingCount; i++)
			{
				Triangles[i * 3 + 0] = 0;
				Triangles[i * 3 + 1] = (FDreamUIMeshIndex)(1 + i);
				Triangles[i * 3 + 2] = (FDreamUIMeshIndex)(1 + ((i + 1) % RingCount));
			}
		}

		auto& Vertices = UIGeo->Vertices;
		auto& OriginVertices = UIGeo->OriginVertices;
		FDreamUIGeometry::DreamUIGeometrySetArrayNum(Vertices, VertexCount);
		FDreamUIGeometry::DreamUIGeometrySetArrayNum(OriginVertices, VertexCount);
		if (!InVertexUVChanged && !InVertexPositionChanged && !InVertexColorChanged)
		{
			return;
		}

		float PivotOffsetX = 0, PivotOffsetY = 0, HalfW = 0, HalfH = 0;
		FDreamUIGeometry::CalculateOffsetAndSize(Width, Height, Pivot, SpriteInfo, PivotOffsetX, PivotOffsetY, HalfW, HalfH);
		const float MinX = -HalfW + PivotOffsetX;
		const float MinY = -HalfH + PivotOffsetY;
		const float MaxX = HalfW + PivotOffsetX;
		const float MaxY = HalfH + PivotOffsetY;
		const float RectWidth = MaxX - MinX;
		const float RectHeight = MaxY - MinY;
		//a radius larger than half the shorter side would fold the outline through itself; clamped, the
		//limit case is a capsule, which is the shape every rounded-rect implementation gives there
		const float MaxRadius = FMath::Max(0.0f, FMath::Min(RectWidth, RectHeight) * 0.5f);
		const float RadiusTopLeft = FMath::Clamp(InCornerRadius.X, 0.0f, MaxRadius);
		const float RadiusTopRight = FMath::Clamp(InCornerRadius.Y, 0.0f, MaxRadius);
		const float RadiusBottomRight = FMath::Clamp(InCornerRadius.Z, 0.0f, MaxRadius);
		const float RadiusBottomLeft = FMath::Clamp(InCornerRadius.W, 0.0f, MaxRadius);

		//arc centre and start angle per corner, in ring order: bottom-left, bottom-right, top-right, top-left
		const FVector2f ArcCenters[4] = {
			FVector2f(MinX + RadiusBottomLeft, MinY + RadiusBottomLeft),
			FVector2f(MaxX - RadiusBottomRight, MinY + RadiusBottomRight),
			FVector2f(MaxX - RadiusTopRight, MaxY - RadiusTopRight),
			FVector2f(MinX + RadiusTopLeft, MaxY - RadiusTopLeft),
		};
		const float ArcRadii[4] = { RadiusBottomLeft, RadiusBottomRight, RadiusTopRight, RadiusTopLeft };
		const float ArcStartAngles[4] = { PI, PI * 1.5f, 0.0f, PI * 0.5f };

		const float InvRectWidth = RectWidth > 0.0f ? 1.0f / RectWidth : 0.0f;
		const float InvRectHeight = RectHeight > 0.0f ? 1.0f / RectHeight : 0.0f;
		//left-bottom of a sprite rect is (MinU, MaxV): V runs the other way from Y, as GetUV0..3 say
		auto UVForPosition = [&](float InX, float InY)
		{
			const float U = (InX - MinX) * InvRectWidth;
			const float V = (InY - MinY) * InvRectHeight;
			return FVector2f(
				FMath::Lerp(SpriteInfo.MinUV.X, SpriteInfo.MaxUV.X, U),
				FMath::Lerp(SpriteInfo.MaxUV.Y, SpriteInfo.MinUV.Y, V));
		};

		if (InVertexPositionChanged || InVertexUVChanged)
		{
			const FVector2f Center((MinX + MaxX) * 0.5f, (MinY + MaxY) * 0.5f);
			if (InVertexPositionChanged)
			{
				OriginVertices[0].Position = FVector3f(0, Center.X, Center.Y);
			}
			if (InVertexUVChanged)
			{
				Vertices[0].TextureCoordinate[0] = UVForPosition(Center.X, Center.Y);
			}
			int32 VertIndex = 1;
			for (int32 Corner = 0; Corner < 4; Corner++)
			{
				const FVector2f ArcCenter = ArcCenters[Corner];
				const float Radius = ArcRadii[Corner];
				const float StartAngle = ArcStartAngles[Corner];
				for (int32 Step = 0; Step < PointsPerCorner; Step++)
				{
					//quarter turn per corner, sampled inclusive of both ends
					const float Angle = StartAngle + (PI * 0.5f) * ((float)Step / (float)Segments);
					const float X = ArcCenter.X + Radius * FMath::Cos(Angle);
					const float Y = ArcCenter.Y + Radius * FMath::Sin(Angle);
					if (InVertexPositionChanged)
					{
						OriginVertices[VertIndex].Position = FVector3f(0, X, Y);
					}
					if (InVertexUVChanged)
					{
						Vertices[VertIndex].TextureCoordinate[0] = UVForPosition(X, Y);
					}
					VertIndex++;
				}
			}
			// No pixel snapping here, unlike the rect builders: their vertices ARE the rect corners, so
			// snapping them moves the whole edge onto a pixel. Snapping points along an arc moves each
			// one a different way and dents the curve.
		}

		if (InVertexColorChanged)
		{
			FDreamUIGeometry::UpdateUIColor(UIGeo, Color);
		}

		if (bRequireNormalAndTangent)
		{
			for (int32 i = 0; i < OriginVertices.Num(); i++)
			{
				OriginVertices[i].Normal = FVector3f(-1, 0, 0);
				OriginVertices[i].Tangent = FVector3f(0, 1, 0);
			}
		}
	}

void UDreamImage::CalculateTileCount(float InRectSize, float InTileSize, int32& OutCount, float& OutRemainedSize)
{
	// The convention the tiled builder expects, and the one UDreamSprite computes the same way: the
	// count INCLUDES the partial tile at the far edge, and the remainder is how much of that last
	// tile is shown. A rect exactly N tiles wide therefore reports N+1 with a remainder of zero,
	// which draws N full tiles and one of no width -- harmless, and what keeps the arithmetic free of
	// a special case at every exact multiple.
	if (InRectSize <= 0.0f || InTileSize <= 0.0f)
	{
		OutCount = 0;
		OutRemainedSize = 0.0f;
		return;
	}
	const float CountFloat = InRectSize / InTileSize;
	OutCount = (int32)CountFloat + 1;
	OutRemainedSize = (CountFloat - (OutCount - 1)) * InTileSize;
}

#if WITH_EDITOR
void UDreamImage::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	// Null means "an undo is about to restore everything", which no per-property branch below can
	// answer. See the note on UDreamWidget::PreEditChange.
	if (PropertyAboutToChange == nullptr)
	{
		return;
	}

	const FName PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == FDreamUIImageBrush::GetPropertyName_ResourceObject())
	{
		UnregisterFromSprite();
	}
}
void UDreamImage::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	static const FName BrushName = GET_MEMBER_NAME_CHECKED(UDreamImage, Brush);

	if (MemberName == BrushName || PropertyName == FDreamUIImageBrush::GetPropertyName_ResourceObject())
	{
		if (auto Widget = GetWidget())
		{
			UDreamWidget::MarkLayoutForRebuild(Widget);
		}
	}
}
#endif

void UDreamImage::UnregisterFromSprite()
{
	if (bHasAddToSprite)
	{
		if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
		{
			DreamSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

UDreamUISpriteData_BaseObject* UDreamImage::SpriteRenderGetSprite_Implementation() const
{
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		return DreamSprite;
	}
	return nullptr;
}

void UDreamImage::ApplyAtlasTextureChange_Implementation()
{
	// Who the atlas broadcasts a repack to is decided by the sprite's registration list, and what
	// the brush holds today is decided by whoever set it last -- so "this component is registered"
	// does not imply "this brush still holds a sprite". check() asserted it did and then read the
	// answer through a C-style cast, which reinterprets whatever object is actually there: a hard
	// crash in Development and Test packages, and in Shipping (DO_CHECK == 0) a silent read through
	// a bogus pointer. Cast<> asks the question the check claimed to have answered.
	UDreamUISpriteData_BaseObject* DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject());
	if (DreamSprite == nullptr)
	{
		return;
	}
	UIGeometry->Texture = DreamSprite->GetAtlasTexture();
	// A repack can move the sprite and change what its info says, and this is the notification that
	// it happened; the sprite data is being initialised on the line above regardless.
	CacheSpriteSourceSize();
	if (UDreamWidget* OwningWidget = GetWidget())
	{
		OwningWidget->MarkCanvasUpdate(true);
	}
}

UDreamImage::UDreamImage(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	Brush.SetResourceObject(UDreamUISpriteData::GetDefaultWhiteSolid());
	Brush.DrawAs = EDreamUIImageBrushDrawType::Image;
}

UTexture* UDreamImage::GetTextureToCreateGeometry()
{
	if (auto Texture = Cast<UTexture>(Brush.GetResourceObject()))
	{
		return Texture;
	}
	if (auto SlateTextureAtlas = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject()))
	{
		return SlateTextureAtlas->GetSlateAtlasData().AtlasTexture;
	}
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		if (!bHasAddToSprite)
		{
			DreamSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
		return DreamSprite->GetAtlasTexture();
	}
	return FDreamUIUtils::GetDefaultWhiteTexture();
}
UMaterialInterface* UDreamImage::GetMaterialToCreateGeometry()
{
	if (auto Material = Cast<UMaterialInterface>(Brush.GetResourceObject()))
	{
		return Material;
	}
	return nullptr;
}

void UDreamImage::OnUpdateGeometry(FDreamUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = this->GetWidget();
	auto RenderSize = Widget->GetSize();
	auto Pivot = Widget->GetPivot();
	auto RenderCanvas = Widget->GetRenderCanvas();
	auto FinalColor = FDreamUIUtils::MultiplyColor(Brush.TintColor, this->GetFinalColor());

	switch (Brush.DrawAs)
	{
	case EDreamUIImageBrushDrawType::None:
		return;
	case EDreamUIImageBrushDrawType::Image:
		{
			DRAW_AS_IMAGE:
			FDreamUISpriteInfo SpriteInfo;
			if (bHasAddToSprite)
			{
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				SpriteInfo = DreamSprite->GetSpriteInfo();
			}
			else
			{
				SpriteInfo.Width = Brush.ImageSize.X;
				SpriteInfo.Height = Brush.ImageSize.Y;
				SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, Brush.UVRegion);
			}
			FDreamUIGeometry::UpdateUIRectSimpleVertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
			, SpriteInfo, RenderCanvas, this, FinalColor
			, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	case EDreamUIImageBrushDrawType::Border:
	case EDreamUIImageBrushDrawType::Box:
		{
			if (bHasAddToSprite)
			{
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				if (!DreamSprite->GetSpriteInfo().HasBorder())
					goto DRAW_AS_IMAGE;
			}
			else
			{
				if (Brush.Margin.Left == 0 && Brush.Margin.Right == 0 && Brush.Margin.Top == 0 && Brush.Margin.Bottom == 0)
					goto DRAW_AS_IMAGE;
			}
			bool bFillCenter = Brush.DrawAs == EDreamUIImageBrushDrawType::Box;
			FDreamUISpriteInfo SpriteInfo;
			if (bHasAddToSprite)
			{
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				SpriteInfo = DreamSprite->GetSpriteInfo();
			}
			else
			{
				SpriteInfo.Width = Brush.ImageSize.X;
				SpriteInfo.Height = Brush.ImageSize.Y;
				SpriteInfo.Border.Left = Brush.Margin.Left * Brush.ImageSize.X;
				SpriteInfo.Border.Right = Brush.Margin.Right * Brush.ImageSize.X;
				SpriteInfo.Border.Top = Brush.Margin.Top * Brush.ImageSize.Y;
				SpriteInfo.Border.Bottom = Brush.Margin.Bottom * Brush.ImageSize.Y;
				SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, Brush.UVRegion);
				SpriteInfo.ApplyBorderUV(1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height);
			}
			FDreamUIGeometry::UpdateUIRectBorderVertex(&InMesh, bFillCenter, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
				, SpriteInfo, RenderCanvas, this, FinalColor
				, Brush.PixelsPerUnitMultiplier
				, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	case EDreamUIImageBrushDrawType::Tiled:
		{
			const FDreamUISpriteInfo SpriteInfo = GetBrushSpriteInfo();
			// The tile is the image at its own size, so a rect smaller than one tile, or a tile with
			// no size to repeat, has nothing to tile and draws as a plain image instead of dividing
			// by zero.
			const float TileWidth = (float)SpriteInfo.Width;
			const float TileHeight = (float)SpriteInfo.Height;
			if (TileWidth <= 0.0f || TileHeight <= 0.0f || RenderSize.X <= 0.0f || RenderSize.Y <= 0.0f)
			{
				goto DRAW_AS_IMAGE;
			}
			int32 WidthCount = 0, HeightCount = 0;
			float RemainedWidth = 0.0f, RemainedHeight = 0.0f;
			CalculateTileCount(RenderSize.X, TileWidth, WidthCount, RemainedWidth);
			CalculateTileCount(RenderSize.Y, TileHeight, HeightCount, RemainedHeight);
			FDreamUIGeometry::UpdateUIRectTiledVertex(&InMesh, SpriteInfo, RenderCanvas, this
				, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
				, WidthCount, HeightCount, RemainedWidth, RemainedHeight, FinalColor
				, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	case EDreamUIImageBrushDrawType::Filled:
		{
			const FDreamUISpriteInfo SpriteInfo = GetBrushSpriteInfo();
			const float FillAmount = FMath::Clamp(Brush.FillAmount, 0.0f, 1.0f);
			switch (Brush.FillMethod)
			{
			default:
			case EDreamUIImageBrushFillMethod::Horizontal:
			case EDreamUIImageBrushFillMethod::Vertical:
				FDreamUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
					, SpriteInfo, Brush.FillDirectionFlip, FillAmount
					, Brush.FillMethod == EDreamUIImageBrushFillMethod::Horizontal
					, RenderCanvas, this, FinalColor
					, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
				break;
			case EDreamUIImageBrushFillMethod::Radial90:
				FDreamUIGeometry::UpdateUIRectFillRadial90Vertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
					, SpriteInfo, Brush.FillDirectionFlip, FillAmount
					, (EDreamUISpriteFillOriginType_Radial90)Brush.FillOrigin
					, RenderCanvas, this, FinalColor
					, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
				break;
			case EDreamUIImageBrushFillMethod::Radial180:
				FDreamUIGeometry::UpdateUIRectFillRadial180Vertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
					, SpriteInfo, Brush.FillDirectionFlip, FillAmount
					, (EDreamUISpriteFillOriginType_Radial180)Brush.FillOrigin
					, RenderCanvas, this, FinalColor
					, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
				break;
			case EDreamUIImageBrushFillMethod::Radial360:
				FDreamUIGeometry::UpdateUIRectFillRadial360Vertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
					, SpriteInfo, Brush.FillDirectionFlip, FillAmount
					, (EDreamUISpriteFillOriginType_Radial360)Brush.FillOrigin
					, RenderCanvas, this, FinalColor
					, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
				break;
			}
		}
		break;
	case EDreamUIImageBrushDrawType::RoundedBox:
		{
			const FDreamUISpriteInfo SpriteInfo = GetBrushSpriteInfo();
			BuildRoundedBoxGeometry(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
				, SpriteInfo, Brush.CornerRadius, Brush.CornerSegments
				, FinalColor, RenderCanvas != nullptr && RenderCanvas->GetActualRequireNormalAndTangent()
				, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	}
}

FDreamUISpriteInfo UDreamImage::GetBrushSpriteInfo()const
{
	// What the geometry builders need to know about the resource: its authored size and the rectangle
	// of the atlas it occupies. A sprite answers both; anything else (a texture, a material, nothing
	// at all) is drawn at the brush's own ImageSize over the brush's UV region, which is the same
	// fallback the Image and Box paths have always used, in one place now that four paths want it.
	FDreamUISpriteInfo SpriteInfo;
	if (bHasAddToSprite)
	{
		if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
		{
			return DreamSprite->GetSpriteInfo();
		}
	}
	SpriteInfo.Width = (uint16)FMath::Max(0.0f, Brush.ImageSize.X);
	SpriteInfo.Height = (uint16)FMath::Max(0.0f, Brush.ImageSize.Y);
	if (SpriteInfo.Width > 0 && SpriteInfo.Height > 0)
	{
		SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, Brush.UVRegion);
	}
	return SpriteInfo;
}

void UDreamImage::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamImage::BeginDestroy()
{
	Super::BeginDestroy();
	//unregister from sprite
	UnregisterFromSprite();
}

void UDreamImage::OnRegister()
{
	Super::OnRegister();
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		if (!bHasAddToSprite)
		{
			DreamSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
	//so that a brush restored from a package measures right on the first layout rather than the second
	CacheSpriteSourceSize();
}

void UDreamImage::OnUnregister()
{
	Super::OnUnregister();
	UnregisterFromSprite();
}

void UDreamImage::SetBrush(const FDreamUIImageBrush& Value)
{
	UnregisterFromSprite();
	
	MarkVerticesDirty(true, true, true, Brush.TintColor != Value.TintColor);
	MarkTextureDirty();
	MarkMaterialDirty();
	Brush = Value;
	CacheSpriteSourceSize();
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrush_DreamUISprite(UDreamUISpriteData_BaseObject* Value)
{
	auto OldDreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject());
	auto NewDreamSprite = Value;
	//handle Sprite
	if (OldDreamSprite != nullptr && NewDreamSprite != nullptr)
	{
		if(OldDreamSprite->GetAtlasTexture() != NewDreamSprite->GetAtlasTexture())
		{
			MarkTextureDirty();
		}
		// The registration list is keyed on the sprite data (its packing tag / packing atlas), not
		// on the atlas texture, so two sprites that happen to share an atlas right now can still
		// need a different registration -- and a component that was never registered
		// (bHasAddToSprite == false) would otherwise keep drawing with the brush-size fallback UVs
		// instead of the sprite's. So the hand-over runs whether or not the atlas texture changed.
		//remove from old
		if (bHasAddToSprite)
		{
			OldDreamSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
		//add to new
		{
			NewDreamSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
		MarkVertexUVDirty();
		Brush.SetResourceObject(Value);
		CacheSpriteSourceSize();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
		return;
	}
	if (OldDreamSprite != nullptr)
	{
		//remove from old
		if (bHasAddToSprite)
		{
			OldDreamSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
	if (NewDreamSprite != nullptr)
	{
		NewDreamSprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
	MarkVerticesDirty(false, false, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value);
	CacheSpriteSourceSize();
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value)
{
	//remove from old sprite
	UnregisterFromSprite();
	
	auto OldSlateSprite = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject());
	auto NewSlateSprite = Value;
	if (OldSlateSprite != nullptr && NewSlateSprite != nullptr)
	{
		if (OldSlateSprite->GetSlateAtlasData().AtlasTexture != NewSlateSprite->GetSlateAtlasData().AtlasTexture)
		{
			MarkTextureDirty();
		}
		MarkVertexUVDirty();
		Brush.SetResourceObject(Value.GetObject());
		CacheSpriteSourceSize();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
		return;
	}

	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value.GetObject());
	CacheSpriteSourceSize();
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrush_Texture(UTexture* Value)
{
	//remove from old sprite
	UnregisterFromSprite();
	
	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value);
	CacheSpriteSourceSize();
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}
void UDreamImage::SetBrush_Material(UTexture* Value)
{
	//remove from old sprite
	UnregisterFromSprite();
	
	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	MarkMaterialDirty();
	Brush.SetResourceObject(Value);
	CacheSpriteSourceSize();
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrushFillAmount(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(Brush.FillAmount, Value))
	{
		Brush.FillAmount = Value;
		//the cut changes the triangles as well as the positions and the UVs, and nothing else
		MarkVerticesDirty(true, true, true, false);
	}
}

void UDreamImage::SetBrushTintColor(FColor Value)
{
	if (Brush.TintColor != Value)
	{
		Brush.TintColor = Value;
		MarkColorDirty();
	}
}

void UDreamImage::CacheSpriteSourceSize()
{
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		const FDreamUISpriteInfo& SpriteInfo = DreamSprite->GetSpriteInfo();
		const float Width = SpriteInfo.GetSourceWidth();
		const float Height = SpriteInfo.GetSourceHeight();
		// A sprite that reports nothing has not been packed yet, or has no texture behind it. Zero is
		// an assertion in the preferred-size protocol and this is not the component making one.
		CachedSpriteSourceSize = FVector2f(Width > 0.0f ? Width : -1.0f, Height > 0.0f ? Height : -1.0f);
		return;
	}
	CachedSpriteSourceSize = FVector2f(-1.0f, -1.0f);
}

void UDreamImage::OnBeforeCreateOrUpdateGeometry()
{
	Super::OnBeforeCreateOrUpdateGeometry();
	// The geometry build reads the sprite info anyway, so this is the one recurring site where the
	// size refresh is free -- and the one that catches a brush swapped in by something other than a
	// setter, such as the details panel writing the property directly.
	CacheSpriteSourceSize();
}

/*
 * A brush over a sprite has a real natural size -- the pixel size the sprite was authored at -- and
 * it is not Brush.ImageSize. That field is what the brush carries for resources that cannot answer
 * for themselves (a material, a texture the brush deliberately draws at another size), and it stays
 * at whatever it was left at when a sprite is assigned. Returning it for every brush measured every
 * sprite in an Auto slot at the default 32x32, silently and confidently.
 * Negative means "no opinion": the sprite has not been packed yet, and the caller falls back to the
 * authored rect rather than to a number this component made up. That holds for a sprite brush only;
 * a brush over anything else answers with ImageSize, which is the size such a brush is drawn at.
 */
float UDreamImage::GetPreferredWidth() const
{
	if (Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()) != nullptr)
	{
		return CachedSpriteSourceSize.X;
	}
	return Brush.ImageSize.X;
}

float UDreamImage::GetPreferredHeight() const
{
	if (Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()) != nullptr)
	{
		return CachedSpriteSourceSize.Y;
	}
	return Brush.ImageSize.Y;
}

DECLARE_DREAM_GUI_VISUAL("Image", UDreamImage)
