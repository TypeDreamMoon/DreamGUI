// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexImage.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIImageBrush.h"
#include "Core/LexUIDrawCall.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUISpriteData.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "Utils/LexUIUtils.h"

#if WITH_EDITOR
void ULexImage::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == FName("ResourceObject"))
	{
		if (bHasAddToSprite)
		{
			auto OldLexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject());
			OldLexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}
void ULexImage::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	static const FName BrushName = GET_MEMBER_NAME_CHECKED(ULexImage, Brush);

	if (MemberName == BrushName)
	{
		
	}
}
#endif

ULexUISpriteData_BaseObject* ULexImage::SpriteRenderGetSprite_Implementation() const
{
	if (auto LexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		return LexSprite;
	}
	return nullptr;
}

void ULexImage::ApplyAtlasTextureScaleUp_Implementation()
{
	check(bHasAddToSprite);
	auto& vertices = UIGeometry->Vertices;
	if (vertices.Num() != 0)
	{
		for (int i = 0; i < vertices.Num(); i++)
		{
			auto& uv = vertices[i];
			uv.TextureCoordinate[0].X *= 0.5f;
			uv.TextureCoordinate[0].Y *= 0.5f;
		}
	}
	auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
	UIGeometry->Texture = LexSprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = UIGeometry->Texture;
		DrawCall->bTextureChanged = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
	MarkVerticesDirty(false, true, true, false);
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

void ULexImage::ApplyAtlasTextureChange_Implementation()
{
	check(bHasAddToSprite);
	auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
	UIGeometry->Texture = LexSprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = UIGeometry->Texture;
		DrawCall->bTextureChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

ULexImage::ULexImage(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	Brush.SetResourceObject(ULexUISpriteData::GetDefaultWhiteSolid());
	Brush.DrawAs = ELexUIImageBrushDrawType::Image;
}

UTexture* ULexImage::GetTextureToCreateGeometry()
{
	if (auto Texture = Cast<UTexture>(Brush.GetResourceObject()))
	{
		return Texture;
	}
	if (auto SlateTextureAtlas = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject()))
	{
		return SlateTextureAtlas->GetSlateAtlasData().AtlasTexture;
	}
	if (auto LexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		if (!bHasAddToSprite)
		{
			LexSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
		return LexSprite->GetAtlasTexture();
	}
	return FLexUIUtils::GetDefaultWhiteTexture();
}
UMaterialInterface* ULexImage::GetMaterialToCreateGeometry()
{
	if (auto Material = Cast<UMaterialInterface>(Brush.GetResourceObject()))
	{
		return Material;
	}
	return nullptr;
}

void ULexImage::OnUpdateGeometry(FLexUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = this->GetWidget();
	auto RenderSize = Widget->GetSize();
	auto RenderCanvas = Widget->GetRenderCanvas();
	auto FinalColor = FLexUIUtils::MultiplyColor(Brush.TintColor, this->GetFinalColor());

	auto& triangles = InMesh.Triangles;
	bool pixelSnapping = this->GetShouldAffectByPixelSnapping() && Widget->GetFinalPixelSnapping();
	auto& vertices = InMesh.Vertices;
	auto& originVertices = InMesh.OriginVertices;
	
	switch (Brush.DrawAs)
	{
	case ELexUIImageBrushDrawType::None:
		return;
	case ELexUIImageBrushDrawType::Image:
		{
			DRAW_AS_IMAGE:
			FLexUIGeometry::LexUIGeometrySetArrayNum(triangles, 6);
			if (InTriangleChanged)
			{
				triangles[0] = 0;
				triangles[1] = 3;
				triangles[2] = 2;
				triangles[3] = 0;
				triangles[4] = 1;
				triangles[5] = 3;
			}
	
			FLexUIGeometry::LexUIGeometrySetArrayNum(vertices, 4);
			FLexUIGeometry::LexUIGeometrySetArrayNum(originVertices, 4);
			
			if (InVertexPositionChanged)
			{
				//offset and size
				float halfW = RenderSize.X * 0.5f, halfH = RenderSize.Y * 0.5f;
				//positions
				float minX = -halfW;
				float minY = -halfH;
				float maxX = halfW;
				float maxY = halfH;
				originVertices[0].Position = FVector3f(0, minX, minY);
				originVertices[1].Position = FVector3f(0, maxX, minY);
				originVertices[2].Position = FVector3f(0, minX, maxY);
				originVertices[3].Position = FVector3f(0, maxX, maxY);
				//snap pixel
				if (pixelSnapping)
				{
					FLexUIGeometry::AdjustPixelPerfectPos(originVertices, 0, 4, RenderCanvas, this);
				}
			}

			if (InVertexUVChanged)
			{
				if (bHasAddToSprite)
				{
					auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
					auto& SpriteInfo = LexSprite->GetSpriteInfo();
					vertices[0].TextureCoordinate[0] = SpriteInfo.GetUV0();
					vertices[1].TextureCoordinate[0] = SpriteInfo.GetUV1();
					vertices[2].TextureCoordinate[0] = SpriteInfo.GetUV2();
					vertices[3].TextureCoordinate[0] = SpriteInfo.GetUV3();
				}
				else
				{
					vertices[0].TextureCoordinate[0] = FVector2f(Brush.UVRegion.X, Brush.UVRegion.W);
					vertices[1].TextureCoordinate[0] = FVector2f(Brush.UVRegion.Z, Brush.UVRegion.W);
					vertices[2].TextureCoordinate[0] = FVector2f(Brush.UVRegion.X, Brush.UVRegion.Y);
					vertices[3].TextureCoordinate[0] = FVector2f(Brush.UVRegion.Z, Brush.UVRegion.Y);
				}
			}

			if (InVertexColorChanged)
			{
				FLexUIGeometry::UpdateUIColor(&InMesh, FinalColor);
			}

			if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
			{
				//additional data
				{
					//normal & tangent
					if (RenderCanvas->GetRequireNormalAndTangent())
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
		break;
	case ELexUIImageBrushDrawType::Border:
	case ELexUIImageBrushDrawType::Box:
		{
			if (bHasAddToSprite)
			{
				auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
				if (!LexSprite->GetSpriteInfo().HasBorder())
					goto DRAW_AS_IMAGE;
			}
			else
			{
				if (Brush.Margin.Left == 0 && Brush.Margin.Right == 0 && Brush.Margin.Top == 0 && Brush.Margin.Bottom == 0)
					goto DRAW_AS_IMAGE;
			}
			bool fillCenter = Brush.DrawAs == ELexUIImageBrushDrawType::Box;
			int triangleCount;
			if (fillCenter)
			{
				triangleCount = 54;
			}
			else
			{
				triangleCount = 48;
			}
			FLexUIGeometry::LexUIGeometrySetArrayNum(triangles, triangleCount);
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

			auto verticesCount = 16;
			FLexUIGeometry::LexUIGeometrySetArrayNum(vertices, verticesCount);
			FLexUIGeometry::LexUIGeometrySetArrayNum(originVertices, verticesCount);
			
			if (InVertexPositionChanged)
			{
				//pivot offset
				float halfW = RenderSize.X * 0.5f, halfH = RenderSize.Y * 0.5f;
				float geoWidth, geoHeight;
				float borderLeft, borderTop, borderRight, borderBottom;
				if (bHasAddToSprite)
				{
					auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
					auto& SpriteInfo = LexSprite->GetSpriteInfo();
					geoWidth = halfW * 2;
					geoHeight = halfH * 2;

					borderLeft = SpriteInfo.borderLeft;
					borderRight = SpriteInfo.borderRight;
					borderTop = SpriteInfo.borderTop;
					borderBottom = SpriteInfo.borderBottom;
				}
				else
				{
					geoWidth = RenderSize.X;
					geoHeight = RenderSize.Y;
					
					borderLeft = Brush.Margin.Left * Brush.ImageSize.X;
					borderRight = Brush.Margin.Right * Brush.ImageSize.X;
					borderTop = Brush.Margin.Top * Brush.ImageSize.Y;
					borderBottom = Brush.Margin.Bottom * Brush.ImageSize.Y;
				}
				//vertices
				
				float widthBorder = borderLeft + borderRight;
				float heightBorder = borderTop + borderBottom;
				float widthScale = geoWidth < widthBorder ? geoWidth / widthBorder : 1.0f;
				float heightScale = geoHeight < heightBorder ? geoHeight / heightBorder : 1.0f;
				float x0 = -halfW;
				float x1 = (x0 + borderLeft * widthScale);
				float x3 = halfW;
				float x2 = (x3 - borderRight * widthScale);
				float y0 = -halfH;
				float y1 = (y0 + borderBottom * heightScale);
				float y3 = halfH;
				float y2 = (y3 - borderTop * heightScale);

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
				if (pixelSnapping)
				{
					FLexUIGeometry::AdjustPixelPerfectPos(originVertices, 0, verticesCount, RenderCanvas, this);
				}
			}

			if (InVertexUVChanged)
			{
				float uv0X, uv0Y, uv3X, uv3Y;
				float buv0X, buv0Y, buv3X, buv3Y;
				if (bHasAddToSprite)
				{
					auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
					auto& SpriteInfo = LexSprite->GetSpriteInfo();
					uv0X = SpriteInfo.uv0X;
					uv0Y = SpriteInfo.uv0Y;
					uv3X = SpriteInfo.uv3X;
					uv3Y = SpriteInfo.uv3Y;
					buv0X = SpriteInfo.buv0X;
					buv0Y = SpriteInfo.buv0Y;
					buv3X = SpriteInfo.buv3X;
					buv3Y = SpriteInfo.buv3Y;
				}
				else
				{
					uv0X = Brush.UVRegion.X;
					uv0Y = Brush.UVRegion.W;
					uv3X = Brush.UVRegion.Z;
					uv3Y = Brush.UVRegion.Y;
					float uvWidth = uv3X - uv0X, uvHeight = uv0Y - uv3Y;
					buv0X = uv0X + Brush.Margin.Left * uvWidth;
					buv0Y = uv0Y - Brush.Margin.Bottom * uvHeight;
					buv3X = uv3X - Brush.Margin.Right * uvWidth;
					buv3Y = uv3Y + Brush.Margin.Top * uvHeight;
				}
				vertices[0].TextureCoordinate[0] = FVector2f(uv0X, uv0Y);
				vertices[1].TextureCoordinate[0] = FVector2f(buv0X, uv0Y);
				vertices[2].TextureCoordinate[0] = FVector2f(buv3X, uv0Y);
				vertices[3].TextureCoordinate[0] = FVector2f(uv3X, uv0Y);

				vertices[4].TextureCoordinate[0] = FVector2f(uv0X, buv0Y);
				vertices[5].TextureCoordinate[0] = FVector2f(buv0X, buv0Y);
				vertices[6].TextureCoordinate[0] = FVector2f(buv3X, buv0Y);
				vertices[7].TextureCoordinate[0] = FVector2f(uv3X, buv0Y);

				vertices[8].TextureCoordinate[0] = FVector2f(uv0X, buv3Y);
				vertices[9].TextureCoordinate[0] = FVector2f(buv0X, buv3Y);
				vertices[10].TextureCoordinate[0] = FVector2f(buv3X, buv3Y);
				vertices[11].TextureCoordinate[0] = FVector2f(uv3X, buv3Y);

				vertices[12].TextureCoordinate[0] = FVector2f(uv0X, uv3Y);
				vertices[13].TextureCoordinate[0] = FVector2f(buv0X, uv3Y);
				vertices[14].TextureCoordinate[0] = FVector2f(buv3X, uv3Y);
				vertices[15].TextureCoordinate[0] = FVector2f(uv3X, uv3Y);
			}

			if (InVertexColorChanged)
			{
				FLexUIGeometry::UpdateUIColor(&InMesh, FinalColor);
			}

			if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
			{
				//additional data
				{
					//normal & tangent
					if (RenderCanvas->GetRequireNormalAndTangent())
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
		break;
	}
}

void ULexImage::PostInitProperties()
{
	Super::PostInitProperties();
}

void ULexImage::BeginDestroy()
{
	Super::BeginDestroy();
	
	if (bHasAddToSprite)
	{
		if (auto LexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject()))
		{
			LexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

void ULexImage::SetBrush(const FLexUIImageBrush& Value)
{
	auto OldLexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject());
	if (OldLexSprite != nullptr)
	{
		//remove from old
		if (bHasAddToSprite)
		{
			OldLexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
	
	MarkVerticesDirty(true, true, true, Brush.TintColor != Value.TintColor);
	MarkTextureDirty();
	MarkMaterialDirty();
	Brush = Value;
}

void ULexImage::SetBrush_LexUISprite(ULexUISpriteData_BaseObject* Value)
{
	auto OldLexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject());
	auto NewLexSprite = Value;
	//handle Sprite
	if (OldLexSprite != nullptr && NewLexSprite != nullptr)
	{
		if(OldLexSprite->GetAtlasTexture() != NewLexSprite->GetAtlasTexture())
		{
			//remove from old
			if (bHasAddToSprite)
			{
				OldLexSprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
			//add to new
			{
				NewLexSprite->AddUISprite(this);
				bHasAddToSprite = true;
			}
			MarkTextureDirty();
		}
		MarkUVDirty();
		Brush.SetResourceObject(Value);
		return;
	}
	if (OldLexSprite != nullptr)
	{
		//remove from old
		if (bHasAddToSprite)
		{
			OldLexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
	if (NewLexSprite != nullptr)
	{
		NewLexSprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
	MarkVerticesDirty(false, false, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value);
}

void ULexImage::SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value)
{
	auto OldSlateSprite = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject());
	auto NewSlateSprite = Value;
	if (OldSlateSprite != nullptr && NewSlateSprite != nullptr)
	{
		if (OldSlateSprite->GetSlateAtlasData().AtlasTexture != NewSlateSprite->GetSlateAtlasData().AtlasTexture)
		{
			MarkTextureDirty();
		}
		MarkUVDirty();
		Brush.SetResourceObject(Value.GetObject());
		return;
	}

	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value.GetObject());
}

void ULexImage::SetBrushTintColor(FColor Value)
{
	if (Brush.TintColor != Value)
	{
		Brush.TintColor = Value;
		MarkColorDirty();
	}
}
