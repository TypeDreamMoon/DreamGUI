// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Psd/DreamPsdTestDocuments.h"
#include "Core/DreamUIBlendMath.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "Import/DreamPsdMapper.h"
#include "Psd/DreamPsdRaster.h"
#include "Psd/DreamPsdReader.h"

namespace DreamPsdTestDocuments
{
	FDreamPsdImage MakeImage(int32 InWidth, int32 InHeight, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt)
	{
		FDreamPsdImage Out;
		Out.Init(InWidth, InHeight, FColor(0, 0, 0, 0));
		for (int32 y = 0; y < InHeight; ++y)
		{
			for (int32 x = 0; x < InWidth; ++x)
			{
				Out.Pixels[y * InWidth + x] = InColourAt(x, y);
			}
		}
		return Out;
	}

	FDreamPsdGray MakeGray(int32 InWidth, int32 InHeight, TFunctionRef<uint8(int32 X, int32 Y)> InValueAt)
	{
		FDreamPsdGray Out;
		Out.Init(InWidth, InHeight, 0);
		for (int32 y = 0; y < InHeight; ++y)
		{
			for (int32 x = 0; x < InWidth; ++x)
			{
				Out.Pixels[y * InWidth + x] = InValueAt(x, y);
			}
		}
		return Out;
	}

	FDreamPsdWriteLayer PixelLayer(const FString& InName, int32 InId, const FIntRect& InRect, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt)
	{
		FDreamPsdWriteLayer L;
		L.Name = InName;
		L.LayerId = InId;
		L.Rect = InRect;
		L.Pixels = MakeImage(InRect.Width(), InRect.Height(), InColourAt);
		return L;
	}

	FDreamPsdWriteLayer GroupDivider(int32 InId)
	{
		FDreamPsdWriteLayer L;
		L.Name = TEXT("</Layer group>");
		L.LayerId = InId;
		L.SectionType = 3;
		return L;
	}

	FDreamPsdWriteLayer GroupRecord(const FString& InName, int32 InId, const FString& InBlendKey, uint8 InOpacity)
	{
		FDreamPsdWriteLayer L;
		L.Name = InName;
		L.LayerId = InId;
		L.SectionType = 1;
		L.BlendKey = InBlendKey;
		L.Opacity = InOpacity;
		return L;
	}

	FDreamPsdPath RoundedRectPath(const FBox2D& InBox, float InRadius)
	{
		return RoundedRectPath(InBox, FVector4(InRadius, InRadius, InRadius, InRadius));
	}

	FDreamPsdPath RoundedRectPath(const FBox2D& B, const FVector4& InRadii)
	{
		// Corners as cubic arcs (the usual 0.5523 handle), clockwise from the top edge's left end. Radii: top left, top
		// right, bottom right, bottom left.
		const double Rtl = InRadii.X, Rtr = InRadii.Y, Rbr = InRadii.Z, Rbl = InRadii.W;
		const double K = 0.5522847498;
		FDreamPsdSubpath Sub;
		auto Knot = [&Sub](const FVector2D& In, const FVector2D& Anchor, const FVector2D& Out) { Sub.Knots.Add({ In, Anchor, Out, true }); };
		const double L = B.Min.X, T = B.Min.Y, Ri = B.Max.X, Bo = B.Max.Y;
		Knot(FVector2D(L + Rtl - K * Rtl, T), FVector2D(L + Rtl, T), FVector2D(L + Rtl, T));
		Knot(FVector2D(Ri - Rtr, T), FVector2D(Ri - Rtr, T), FVector2D(Ri - Rtr + K * Rtr, T));
		Knot(FVector2D(Ri, T + Rtr - K * Rtr), FVector2D(Ri, T + Rtr), FVector2D(Ri, T + Rtr));
		Knot(FVector2D(Ri, Bo - Rbr), FVector2D(Ri, Bo - Rbr), FVector2D(Ri, Bo - Rbr + K * Rbr));
		Knot(FVector2D(Ri - Rbr + K * Rbr, Bo), FVector2D(Ri - Rbr, Bo), FVector2D(Ri - Rbr, Bo));
		Knot(FVector2D(L + Rbl, Bo), FVector2D(L + Rbl, Bo), FVector2D(L + Rbl - K * Rbl, Bo));
		Knot(FVector2D(L, Bo - Rbl + K * Rbl), FVector2D(L, Bo - Rbl), FVector2D(L, Bo - Rbl));
		Knot(FVector2D(L, T + Rtl), FVector2D(L, T + Rtl), FVector2D(L, T + Rtl - K * Rtl));
		FDreamPsdPath Path;
		Path.Subpaths.Add(Sub);
		return Path;
	}

	FDreamPsdPath EllipsePath(const FBox2D& B)
	{
		const FVector2D C = B.GetCenter();
		const FVector2D H = B.GetExtent();
		const double Kx = 0.5522847498 * H.X, Ky = 0.5522847498 * H.Y;
		FDreamPsdSubpath Sub;
		Sub.Knots.Add({ FVector2D(C.X - Kx, C.Y - H.Y), FVector2D(C.X, C.Y - H.Y), FVector2D(C.X + Kx, C.Y - H.Y), true });
		Sub.Knots.Add({ FVector2D(C.X + H.X, C.Y - Ky), FVector2D(C.X + H.X, C.Y), FVector2D(C.X + H.X, C.Y + Ky), true });
		Sub.Knots.Add({ FVector2D(C.X + Kx, C.Y + H.Y), FVector2D(C.X, C.Y + H.Y), FVector2D(C.X - Kx, C.Y + H.Y), true });
		Sub.Knots.Add({ FVector2D(C.X - H.X, C.Y + Ky), FVector2D(C.X - H.X, C.Y), FVector2D(C.X - H.X, C.Y - Ky), true });
		FDreamPsdPath Path;
		Path.Subpaths.Add(Sub);
		return Path;
	}

	FColor Disc(int32 X, int32 Y, const FVector2D& InCentre, float InRadius, FColor InColour)
	{
		const float D = (float)(FVector2D(X + 0.5, Y + 0.5) - InCentre).Size();
		const float Coverage = FMath::Clamp(InRadius - D + 0.5f, 0.0f, 1.0f);
		FColor C = InColour;
		C.A = (uint8)FMath::RoundToInt(InColour.A * Coverage);
		return C;
	}

	FDreamPsdWriteDocument MakeGallery(int32 InDepth, bool bInLarge, uint16 InCompression)
	{
		FDreamPsdWriteDocument Doc;
		Doc.Width = 256;
		Doc.Height = 192;
		Doc.Depth = InDepth;
		Doc.bLarge = bInLarge;
		TArray<FDreamPsdWriteLayer>& Layers = Doc.Layers;

		Layers.Add(PixelLayer(TEXT("Background"), 1, FIntRect(0, 0, 256, 192), [](int32 X, int32 Y)
		{
			const float T = Y / 191.0f;
			const bool bCell = ((X / 16) + (Y / 16)) % 2 == 0;
			const int32 Lift = bCell ? 14 : 0;
			return FColor((uint8)FMath::RoundToInt(40 + 10 * T + Lift), (uint8)FMath::RoundToInt(70 + 70 * T + Lift), (uint8)FMath::RoundToInt(120 + 10 * T + Lift), 255);
		}));
		Layers.Add(PixelLayer(TEXT("Disc"), 2, FIntRect(24, 24, 120, 120), [](int32 X, int32 Y) { return Disc(X, Y, FVector2D(48, 48), 40.0f, FColor(240, 140, 40, 255)); }));
		{
			FDreamPsdWriteLayer Stripes = PixelLayer(TEXT("Stripes"), 3, FIntRect(16, 16, 128, 128), [](int32 X, int32 Y)
			{
				return ((X + Y) / 8) % 2 == 0 ? FColor(255, 255, 255, 180) : FColor(40, 40, 200, 140);
			});
			Stripes.bClipping = true;
			Layers.Add(Stripes);
		}
		{
			FDreamPsdWriteLayer Shade = PixelLayer(TEXT("Shade"), 4, FIntRect(148, 20, 228, 100), [](int32, int32) { return FColor(200, 120, 255, 255); });
			Shade.BlendKey = TEXT("mul ");
			Layers.Add(Shade);
		}
		{
			FDreamPsdWriteLayer Glow = PixelLayer(TEXT("Glow"), 5, FIntRect(168, 56, 244, 132), [](int32, int32) { return FColor(250, 230, 90, 255); });
			Glow.BlendKey = TEXT("over");
			Glow.Opacity = 204;
			Layers.Add(Glow);
		}
		{
			FDreamPsdWriteLayer Masked = PixelLayer(TEXT("Masked"), 6, FIntRect(20, 128, 110, 180), [](int32, int32) { return FColor(220, 60, 60, 255); });
			Masked.bMask = true;
			Masked.MaskRect = Masked.Rect;
			Masked.MaskPixels = MakeGray(90, 52, [](int32 X, int32) { return X < 45 ? (uint8)255 : (uint8)FMath::Clamp(FMath::RoundToInt(255.0f * (1.0f - (X - 45) / 45.0f)), 0, 255); });
			Layers.Add(Masked);
		}
		// An isolated group at 60 %: its two squares overlap without showing through each other.
		Layers.Add(GroupDivider(7));
		Layers.Add(PixelLayer(TEXT("Green"), 8, FIntRect(120, 110, 170, 160), [](int32, int32) { return FColor(60, 200, 90, 255); }));
		Layers.Add(PixelLayer(TEXT("Yellow"), 9, FIntRect(140, 125, 190, 175), [](int32, int32) { return FColor(250, 220, 60, 255); }));
		Layers.Add(GroupRecord(TEXT("Panel"), 10, TEXT("norm"), 153));
		// A pass-through group under an elliptic vector mask.
		Layers.Add(GroupDivider(11));
		Layers.Add(PixelLayer(TEXT("Checker"), 12, FIntRect(90, 2, 160, 70), [](int32 X, int32 Y)
		{
			return ((X / 8) + (Y / 8)) % 2 == 0 ? FColor(230, 230, 230, 255) : FColor(60, 60, 60, 255);
		}));
		{
			FDreamPsdWriteLayer Frame = GroupRecord(TEXT("Frame"), 13);
			const FBox2D Box(FVector2D(96, 8), FVector2D(152, 64));
			Frame.bVectorMask = true;
			Frame.VectorPath = EllipsePath(Box);
			Frame.LiveShape.Kind = EDreamPsdLiveShapeKind::Ellipse;
			Frame.LiveShape.Box = Box;
			Layers.Add(Frame);
		}
		{
			// A rounded rectangle: its pixels are what the shape renders to, as the editor stores them.
			const FBox2D Box(FVector2D(196, 146), FVector2D(248, 182));
			const FDreamPsdPath Path = RoundedRectPath(Box, 12.0f);
			const FIntRect Rect(196, 146, 248, 182);
			const FDreamPsdGray Coverage = DreamPsdRaster::FillPath(Path, Rect);
			FDreamPsdWriteLayer Pill = PixelLayer(TEXT("Pill"), 14, Rect, [&Coverage](int32 X, int32 Y) { return FColor(150, 90, 220, Coverage.At(X, Y)); });
			Pill.bVectorMask = true;
			Pill.VectorPath = Path;
			Pill.LiveShape.Kind = EDreamPsdLiveShapeKind::RoundedRectangle;
			Pill.LiveShape.Box = Box;
			Pill.LiveShape.Radii = FVector4(12, 12, 12, 12);
			Pill.Fill.Kind = FDreamPsdFill::EKind::Solid;
			Pill.Fill.Color = FLinearColor(150 / 255.0f, 90 / 255.0f, 220 / 255.0f, 1.0f);
			Layers.Add(Pill);
		}
		{
			// A tab: its corners rounded unevenly, which pins the order a block's radii are in.
			const FBox2D Box(FVector2D(112, 164), FVector2D(140, 190));
			const FVector4 Radii(12, 3, 12, 3);
			const FDreamPsdPath Path = RoundedRectPath(Box, Radii);
			const FIntRect Rect(112, 164, 140, 190);
			const FDreamPsdGray Coverage = DreamPsdRaster::FillPath(Path, Rect);
			FDreamPsdWriteLayer Tab = PixelLayer(TEXT("Tab"), 18, Rect, [&Coverage](int32 X, int32 Y) { return FColor(240, 240, 250, Coverage.At(X, Y)); });
			Tab.bVectorMask = true;
			Tab.VectorPath = Path;
			Tab.LiveShape.Kind = EDreamPsdLiveShapeKind::RoundedRectangle;
			Tab.LiveShape.Box = Box;
			Tab.LiveShape.Radii = Radii;
			Tab.Fill.Kind = FDreamPsdFill::EKind::Solid;
			Tab.Fill.Color = FLinearColor(240 / 255.0f, 240 / 255.0f, 250 / 255.0f, 1.0f);
			Layers.Add(Tab);
		}
		{
			// A white fill at half fill, shown through a mask that fades out down the top band.
			FDreamPsdWriteLayer Tint = PixelLayer(TEXT("Tint"), 15, FIntRect(0, 0, 256, 192), [](int32, int32) { return FColor::White; });
			Tint.Fill.Kind = FDreamPsdFill::EKind::Solid;
			Tint.Fill.Color = FLinearColor::White;
			Tint.FillOpacity = 128;
			Tint.bMask = true;
			Tint.MaskRect = FIntRect(0, 0, 256, 24);
			Tint.MaskPixels = MakeGray(256, 24, [](int32, int32 Y) { return (uint8)FMath::RoundToInt(255.0f * (1.0f - Y / 24.0f)); });
			Layers.Add(Tint);
		}
		{
			FDreamPsdWriteLayer Ghost = PixelLayer(TEXT("Ghost"), 16, FIntRect(10, 10, 40, 40), [](int32, int32) { return FColor(255, 0, 255, 255); });
			Ghost.bVisible = false;
			Layers.Add(Ghost);
		}
		{
			FDreamPsdWriteLayer Notes = PixelLayer(TEXT("Notes !skip"), 17, FIntRect(200, 10, 250, 40), [](int32, int32) { return FColor(255, 255, 0, 255); });
			Notes.bVisible = false;
			Layers.Add(Notes);
		}
		for (FDreamPsdWriteLayer& L : Layers)
		{
			L.Compression = InCompression;
		}
		return Doc;
	}

	namespace Local
	{
		/** Premultiplied, linear light. */
		struct FBuffer
		{
			int32 W = 0;
			int32 H = 0;
			TArray<FVector4f> P;
		};

		FVector3f Lin(const FColor& C)
		{
			return FDreamUIBlendMath::ToLinear(FVector3f(C.R / 255.0f, C.G / 255.0f, C.B / 255.0f));
		}

		/** One element, straight colour InColour (linear) and alpha A, onto a pixel by Mode. */
		void Blend(FVector4f& D, EDreamUIBlendMode Mode, const FVector3f& InLinear, float A)
		{
			if (A <= 0.0f)
			{
				return;
			}
			if (Mode == EDreamUIBlendMode::Alpha || Mode == EDreamUIBlendMode::Dissolve)
			{
				D = FVector4f(InLinear * A, A) + D * (1.0f - A);
				return;
			}
			if (!DreamUIBlend::ReadsBackdrop(Mode))
			{
				const FVector4f Source = FDreamUIBlendMath::FixedOutput(Mode, InLinear * A, A);
				D = FDreamUIBlendMath::ApplyFixedBlendState(Mode, Source, D);
				return;
			}
			// Read what is behind, in encoded space, by the formula; back to linear.
			const float Ab = D.W;
			const FVector3f Cb = Ab > 0.0f ? FDreamUIBlendMath::ToSrgb(FVector3f(D.X, D.Y, D.Z) / Ab) : FVector3f::ZeroVector;
			const FVector4f R = FDreamUIBlendMath::ReferenceComposite(Mode, Cb, Ab, FDreamUIBlendMath::ToSrgb(InLinear), A);
			const FVector3f Straight = R.W > 0.0f ? FVector3f(R.X, R.Y, R.Z) / R.W : FVector3f::ZeroVector;
			D = FVector4f(FDreamUIBlendMath::ToLinear(Straight) * R.W, R.W);
		}

		EDreamUIBlendMode ModeOf(const FString& InKey)
		{
			const FString Name = FDreamPsdMapper::BlendModeName(InKey);
			const int64 Value = StaticEnum<EDreamUIBlendMode>()->GetValueByNameString(Name.IsEmpty() ? FString(TEXT("Alpha")) : Name);
			return Value == INDEX_NONE ? EDreamUIBlendMode::Alpha : (EDreamUIBlendMode)Value;
		}

		void Composite(const FDreamPsdLayerNode& InNode, FBuffer& Target, const FDreamPsdDocument& Doc, const TArray<float>* InParentMask)
		{
			const FIntRect Canvas(0, 0, Doc.Width, Doc.Height);
			TArray<float> BaseAlpha;
			bool bBaseVisible = false;
			auto MaskOf = [&Doc, &Canvas](const FDreamPsdLayer& L, bool bInVector, TArray<float>& Out)
			{
				Out.Init(1.0f, Doc.Width * Doc.Height);
				if (L.Mask.bPresent && !L.Mask.bDisabled)
				{
					const FDreamPsdGray M = DreamPsdRaster::BakeLayerMask(L.Mask, Canvas);
					for (int32 i = 0; i < Out.Num(); ++i) { Out[i] *= M.Pixels[i] / 255.0f; }
				}
				if (bInVector && L.VectorMask.bPresent && !L.VectorMask.bDisabled)
				{
					const FDreamPsdGray M = DreamPsdRaster::BakeVectorMask(L.VectorMask, Canvas);
					for (int32 i = 0; i < Out.Num(); ++i) { Out[i] *= M.Pixels[i] / 255.0f; }
				}
			};
			for (const FDreamPsdLayerNode& Child : InNode.Children)
			{
				const FDreamPsdLayer& L = Doc.Layers[Child.LayerIndex];
				const bool bShown = L.bVisible && !L.Name.Contains(TEXT("!skip"));
				if (!bShown)
				{
					if (!L.bClipping)
					{
						bBaseVisible = false;
					}
					continue;
				}
				if (Child.bGroup || L.IsGroupStart())
				{
					TArray<float> Mask;
					MaskOf(L, true, Mask);
					if (InParentMask != nullptr)
					{
						for (int32 i = 0; i < Mask.Num(); ++i) { Mask[i] *= (*InParentMask)[i]; }
					}
					if (L.BlendKey == TEXT("pass") && L.Opacity == 255)
					{
						Composite(Child, Target, Doc, &Mask);
					}
					else
					{
						FBuffer Group;
						Group.W = Doc.Width;
						Group.H = Doc.Height;
						Group.P.Init(FVector4f(0, 0, 0, 0), Group.W * Group.H);
						Composite(Child, Group, Doc, nullptr);
						const float Opacity = L.Opacity / 255.0f;
						for (int32 i = 0; i < Target.P.Num(); ++i)
						{
							const float M = Mask[i] * Opacity;
							Target.P[i] = Group.P[i] * M + Target.P[i] * (1.0f - Group.P[i].W * M);
						}
					}
					bBaseVisible = false;
					continue;
				}
				if (!L.HasPixels())
				{
					continue;
				}
				TArray<float> Mask;
				MaskOf(L, L.Kind != EDreamPsdLayerKind::Shape, Mask);
				const EDreamUIBlendMode Mode = ModeOf(L.BlendKey);
				const float Opacity = L.Opacity / 255.0f;
				const float Fill = L.FillOpacity / 255.0f;
				if (!L.bClipping)
				{
					BaseAlpha.Init(0.0f, Doc.Width * Doc.Height);
					bBaseVisible = true;
				}
				for (int32 y = 0; y < L.Rect.Height(); ++y)
				{
					const int32 DocY = L.Rect.Min.Y + y;
					if (DocY < 0 || DocY >= Doc.Height) { continue; }
					for (int32 x = 0; x < L.Rect.Width(); ++x)
					{
						const int32 DocX = L.Rect.Min.X + x;
						if (DocX < 0 || DocX >= Doc.Width) { continue; }
						const int32 i = DocY * Doc.Width + DocX;
						const FColor C = L.Pixels.At(x, y);
						float A = C.A / 255.0f * Mask[i] * Opacity;
						if (!L.bClipping)
						{
							BaseAlpha[i] = A;
						}
						else
						{
							A *= bBaseVisible ? BaseAlpha[i] : 0.0f;
						}
						A *= Fill;
						if (InParentMask != nullptr)
						{
							A *= (*InParentMask)[i];
						}
						Blend(Target.P[i], Mode, Lin(C), A);
					}
				}
			}
		}
	}

	FDreamPsdImage CompositeAsDrawn(const FDreamPsdDocument& InDocument, FColor InBackdrop)
	{
		Local::FBuffer Canvas;
		Canvas.W = InDocument.Width;
		Canvas.H = InDocument.Height;
		Canvas.P.Init(FVector4f(Local::Lin(InBackdrop), 1.0f), Canvas.W * Canvas.H);
		Local::Composite(InDocument.BuildTree(), Canvas, InDocument, nullptr);
		FDreamPsdImage Out;
		Out.Init(Canvas.W, Canvas.H, FColor::Black);
		for (int32 i = 0; i < Canvas.P.Num(); ++i)
		{
			const FVector4f& P = Canvas.P[i];
			const FVector3f Straight = P.W > 0.0f ? FVector3f(P.X, P.Y, P.Z) / P.W : FVector3f::ZeroVector;
			const FVector3f E = FDreamUIBlendMath::ToSrgb(Straight);
			auto Q = [](float V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255); };
			Out.Pixels[i] = FColor(Q(E.X), Q(E.Y), Q(E.Z), Q(P.W));
		}
		return Out;
	}

	TArray<uint8> WriteWithComposite(FDreamPsdWriteDocument& Doc, FColor InBackdrop)
	{
		TArray<uint8> Bytes;
		DreamPsdWriter::Write(Doc, Bytes);
		FDreamPsdDocument Read;
		FDreamPsdMessages Messages;
		DreamPsdReader::Read(Bytes, Read, Messages);
		Doc.Composite = CompositeAsDrawn(Read, InBackdrop);
		DreamPsdWriter::Write(Doc, Bytes);
		return Bytes;
	}
}

#endif
