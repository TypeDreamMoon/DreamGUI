// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdRaster.h"

FIntRect DreamPsdRaster::FindOpaqueBounds(const FDreamPsdImage& InImage)
{
	if (InImage.IsEmpty())
	{
		return FIntRect();
	}
	int32 MinX = InImage.Width, MinY = InImage.Height, MaxX = -1, MaxY = -1;
	for (int32 y = 0; y < InImage.Height; ++y)
	{
		const FColor* Row = InImage.Pixels.GetData() + y * InImage.Width;
		for (int32 x = 0; x < InImage.Width; ++x)
		{
			if (Row[x].A > 0)
			{
				MinX = FMath::Min(MinX, x);
				MaxX = FMath::Max(MaxX, x);
				MinY = FMath::Min(MinY, y);
				MaxY = FMath::Max(MaxY, y);
			}
		}
	}
	if (MaxX < 0)
	{
		return FIntRect();
	}
	return FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
}

FDreamPsdImage DreamPsdRaster::Crop(const FDreamPsdImage& InImage, const FIntRect& InRect)
{
	FDreamPsdImage Out;
	const FIntRect R(FMath::Max(0, InRect.Min.X), FMath::Max(0, InRect.Min.Y), FMath::Min(InImage.Width, InRect.Max.X), FMath::Min(InImage.Height, InRect.Max.Y));
	if (R.Width() <= 0 || R.Height() <= 0)
	{
		return Out;
	}
	Out.Init(R.Width(), R.Height(), FColor(0, 0, 0, 0));
	for (int32 y = 0; y < R.Height(); ++y)
	{
		FMemory::Memcpy(Out.Pixels.GetData() + y * Out.Width, InImage.Pixels.GetData() + (R.Min.Y + y) * InImage.Width + R.Min.X, sizeof(FColor) * Out.Width);
	}
	return Out;
}

namespace DreamPsdRasterLocal
{
	struct FPathEdge
	{
		FVector2D A;
		FVector2D B;
	};

	void Flatten(const FDreamPsdSubpath& InSub, TArray<FPathEdge>& OutEdges)
	{
		const int32 N = InSub.Knots.Num();
		if (N < 2)
		{
			return;
		}
		const int32 Segments = InSub.bClosed ? N : N - 1;
		FVector2D First = InSub.Knots[0].Anchor;
		for (int32 i = 0; i < Segments; ++i)
		{
			const FDreamPsdPathKnot& K0 = InSub.Knots[i];
			const FDreamPsdPathKnot& K1 = InSub.Knots[(i + 1) % N];
			const FVector2D P0 = K0.Anchor, P1 = K0.Out, P2 = K1.In, P3 = K1.Anchor;
			// Enough steps that a segment is under a pixel long or so, at least a few.
			const double Length = (P1 - P0).Size() + (P2 - P1).Size() + (P3 - P2).Size();
			const bool bStraight = (P1 - P0).IsNearlyZero() && (P3 - P2).IsNearlyZero();
			const int32 Steps = bStraight ? 1 : FMath::Clamp((int32)(Length / 2.0), 4, 256);
			FVector2D Previous = P0;
			for (int32 s = 1; s <= Steps; ++s)
			{
				const double T = double(s) / Steps;
				const double U = 1.0 - T;
				const FVector2D P = P0 * (U * U * U) + P1 * (3.0 * U * U * T) + P2 * (3.0 * U * T * T) + P3 * (T * T * T);
				OutEdges.Add({ Previous, P });
				Previous = P;
			}
		}
		if (!InSub.bClosed)
		{
			// An open subpath is filled as if closed by a straight line, as the editor fills it.
			OutEdges.Add({ InSub.Knots.Last().Anchor, First });
		}
	}

	/** Coverage 0..1 over the region, non-zero or even-odd. */
	void Fill(const TArray<FPathEdge>& InEdges, const FIntRect& InRegion, int32 InSubRows, bool bInEvenOdd, TArray<float>& OutCoverage)
	{
		const int32 W = InRegion.Width();
		const int32 H = InRegion.Height();
		OutCoverage.SetNumZeroed(W * H);
		struct FCross { double X; int32 Dir; };
		TArray<FCross> Crossings;
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 s = 0; s < InSubRows; ++s)
			{
				const double SampleY = InRegion.Min.Y + y + (s + 0.5) / InSubRows;
				Crossings.Reset();
				for (const FPathEdge& E : InEdges)
				{
					const double Y0 = E.A.Y, Y1 = E.B.Y;
					if ((SampleY >= Y0 && SampleY < Y1) || (SampleY >= Y1 && SampleY < Y0))
					{
						const double T = (SampleY - Y0) / (Y1 - Y0);
						Crossings.Add({ E.A.X + T * (E.B.X - E.A.X), Y1 > Y0 ? 1 : -1 });
					}
				}
				Crossings.Sort([](const FCross& A, const FCross& B) { return A.X < B.X; });
				int32 Winding = 0;
				for (int32 c = 0; c + 1 < Crossings.Num(); ++c)
				{
					Winding += bInEvenOdd ? 1 : Crossings[c].Dir;
					const bool bInside = bInEvenOdd ? (Winding % 2) != 0 : Winding != 0;
					if (!bInside)
					{
						continue;
					}
					// The span [X0, X1), in region pixels, added with exact coverage at its ends.
					const double X0 = FMath::Clamp(Crossings[c].X - InRegion.Min.X, 0.0, double(W));
					const double X1 = FMath::Clamp(Crossings[c + 1].X - InRegion.Min.X, 0.0, double(W));
					if (X1 <= X0)
					{
						continue;
					}
					const int32 I0 = (int32)FMath::FloorToDouble(X0);
					const int32 I1 = (int32)FMath::FloorToDouble(X1);
					float* Row = OutCoverage.GetData() + y * W;
					const float Weight = 1.0f / InSubRows;
					if (I0 == I1)
					{
						if (I0 < W) { Row[I0] += float(X1 - X0) * Weight; }
						continue;
					}
					Row[I0] += float(I0 + 1 - X0) * Weight;
					for (int32 i = I0 + 1; i < I1 && i < W; ++i)
					{
						Row[i] += Weight;
					}
					if (I1 < W) { Row[I1] += float(X1 - I1) * Weight; }
				}
			}
		}
		for (float& V : OutCoverage)
		{
			V = FMath::Clamp(V, 0.0f, 1.0f);
		}
	}

	FDreamPsdGray ToGray(const TArray<float>& InCoverage, int32 W, int32 H)
	{
		FDreamPsdGray Out;
		Out.Init(W, H, 0);
		for (int32 i = 0; i < W * H; ++i)
		{
			Out.Pixels[i] = (uint8)FMath::Clamp(FMath::RoundToInt(InCoverage[i] * 255.0f), 0, 255);
		}
		return Out;
	}
}

FDreamPsdGray DreamPsdRaster::FillPath(const FDreamPsdPath& InPath, const FIntRect& InRegion, int32 InSubRows)
{
	using namespace DreamPsdRasterLocal;
	const int32 W = FMath::Max(0, InRegion.Width());
	const int32 H = FMath::Max(0, InRegion.Height());
	TArray<float> Result;
	Result.Init(InPath.bStartsFilled ? 1.0f : 0.0f, W * H);
	if (W == 0 || H == 0)
	{
		return ToGray(Result, W, H);
	}
	const bool bLegacy = InPath.Subpaths.ContainsByPredicate([](const FDreamPsdSubpath& S) { return S.Operation == EDreamPsdPathOperation::Legacy; });
	if (bLegacy)
	{
		TArray<FPathEdge> Edges;
		for (const FDreamPsdSubpath& Sub : InPath.Subpaths)
		{
			Flatten(Sub, Edges);
		}
		TArray<float> Coverage;
		Fill(Edges, InRegion, InSubRows, true, Coverage);
		for (int32 i = 0; i < W * H; ++i)
		{
			Result[i] = InPath.bStartsFilled ? 1.0f - Coverage[i] : Coverage[i];
		}
		return ToGray(Result, W, H);
	}
	for (const FDreamPsdSubpath& Sub : InPath.Subpaths)
	{
		TArray<FPathEdge> Edges;
		Flatten(Sub, Edges);
		TArray<float> Coverage;
		Fill(Edges, InRegion, InSubRows, false, Coverage);
		for (int32 i = 0; i < W * H; ++i)
		{
			const float A = Result[i], B = Coverage[i];
			switch (Sub.Operation)
			{
			case EDreamPsdPathOperation::Combine: Result[i] = A + B - A * B; break;
			case EDreamPsdPathOperation::Subtract: Result[i] = A * (1.0f - B); break;
			case EDreamPsdPathOperation::Intersect: Result[i] = A * B; break;
			case EDreamPsdPathOperation::Exclude: Result[i] = A + B - 2.0f * A * B; break;
			default: Result[i] = FMath::Max(A, B); break;
			}
		}
	}
	return ToGray(Result, W, H);
}

void DreamPsdRaster::Blur(FDreamPsdGray& InOut, float InSigma)
{
	if (InSigma < 0.25f || InOut.IsEmpty())
	{
		return;
	}
	const int32 Radius = FMath::Max(1, FMath::CeilToInt(InSigma * 3.0f));
	TArray<float> Kernel;
	Kernel.SetNum(Radius * 2 + 1);
	float Sum = 0.0f;
	for (int32 i = -Radius; i <= Radius; ++i)
	{
		Kernel[i + Radius] = FMath::Exp(-(i * i) / (2.0f * InSigma * InSigma));
		Sum += Kernel[i + Radius];
	}
	for (float& K : Kernel)
	{
		K /= Sum;
	}
	const int32 W = InOut.Width, H = InOut.Height;
	TArray<float> Temp;
	Temp.SetNumZeroed(W * H);
	// Edges are extended (clamped), so a mask that is white to its border stays white there.
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			float V = 0.0f;
			for (int32 k = -Radius; k <= Radius; ++k)
			{
				V += Kernel[k + Radius] * InOut.Pixels[y * W + FMath::Clamp(x + k, 0, W - 1)];
			}
			Temp[y * W + x] = V;
		}
	}
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			float V = 0.0f;
			for (int32 k = -Radius; k <= Radius; ++k)
			{
				V += Kernel[k + Radius] * Temp[FMath::Clamp(y + k, 0, H - 1) * W + x];
			}
			InOut.Pixels[y * W + x] = (uint8)FMath::Clamp(FMath::RoundToInt(V), 0, 255);
		}
	}
}

FDreamPsdGray DreamPsdRaster::BakeLayerMask(const FDreamPsdLayerMask& InMask, const FIntRect& InRegion, float InFeatherScale)
{
	FDreamPsdGray Out;
	Out.Init(InRegion.Width(), InRegion.Height(), InMask.DefaultColor);
	const FIntRect& R = InMask.Rect;
	for (int32 y = 0; y < Out.Height; ++y)
	{
		const int32 DocY = InRegion.Min.Y + y;
		if (DocY < R.Min.Y || DocY >= R.Max.Y)
		{
			continue;
		}
		for (int32 x = 0; x < Out.Width; ++x)
		{
			const int32 DocX = InRegion.Min.X + x;
			if (DocX >= R.Min.X && DocX < R.Max.X && !InMask.Pixels.IsEmpty())
			{
				Out.Pixels[y * Out.Width + x] = InMask.Pixels.At(DocX - R.Min.X, DocY - R.Min.Y);
			}
		}
	}
	if (InMask.bInvertOnBlend)
	{
		for (uint8& V : Out.Pixels)
		{
			V = 255 - V;
		}
	}
	Blur(Out, InMask.Feather * InFeatherScale);
	if (InMask.Density < 1.0f)
	{
		for (uint8& V : Out.Pixels)
		{
			V = (uint8)FMath::Clamp(FMath::RoundToInt(255.0f - InMask.Density * (255.0f - V)), 0, 255);
		}
	}
	return Out;
}

FDreamPsdGray DreamPsdRaster::BakeVectorMask(const FDreamPsdVectorMask& InMask, const FIntRect& InRegion, float InFeatherScale)
{
	FDreamPsdGray Out = FillPath(InMask.Path, InRegion);
	if (InMask.bInvert)
	{
		for (uint8& V : Out.Pixels)
		{
			V = 255 - V;
		}
	}
	Blur(Out, InMask.Feather * InFeatherScale);
	if (InMask.Density < 1.0f)
	{
		for (uint8& V : Out.Pixels)
		{
			V = (uint8)FMath::Clamp(FMath::RoundToInt(255.0f - InMask.Density * (255.0f - V)), 0, 255);
		}
	}
	return Out;
}

FDreamPsdImage DreamPsdRaster::DrawGradient(const FDreamPsdGradientFill& InFill, const FBox2D& InBox, FIntPoint InSize)
{
	FDreamPsdImage Out;
	Out.Init(FMath::Max(1, InSize.X), FMath::Max(1, InSize.Y), FColor::Black);
	const FVector2D BoxSize = InBox.GetSize();
	const FVector2D Centre = InBox.GetCenter() + FVector2D(InFill.Offset.X * BoxSize.X / 100.0, InFill.Offset.Y * BoxSize.Y / 100.0);
	const double Radians = FMath::DegreesToRadians((double)InFill.Angle);
	// The angle is counter-clockwise on screen; y runs down here.
	const FVector2D Direction(FMath::Cos(Radians), -FMath::Sin(Radians));
	const double Scale = FMath::Max(0.01, (double)InFill.Scale);
	const double Span = (FMath::Abs(BoxSize.X * Direction.X) + FMath::Abs(BoxSize.Y * Direction.Y)) * Scale;
	const double Radius = 0.5 * BoxSize.Size() * Scale;
	const FString Style = InFill.Style.TrimEnd();
	for (int32 y = 0; y < Out.Height; ++y)
	{
		for (int32 x = 0; x < Out.Width; ++x)
		{
			const FVector2D P(InBox.Min.X + (x + 0.5) * BoxSize.X / Out.Width, InBox.Min.Y + (y + 0.5) * BoxSize.Y / Out.Height);
			const FVector2D D = P - Centre;
			double T = 0.0;
			if (Style == TEXT("Rdl"))
			{
				T = Radius > 0 ? D.Size() / Radius : 0.0;
			}
			else if (Style == TEXT("Angl"))
			{
				// Around the centre, starting at the angle, clockwise on screen.
				const double A = FMath::Atan2(-D.Y, D.X) - Radians;
				T = 1.0 - FMath::Fmod(FMath::Fmod(A, 2.0 * PI) + 2.0 * PI, 2.0 * PI) / (2.0 * PI);
			}
			else if (Style == TEXT("Rflc"))
			{
				T = Span > 0 ? FMath::Abs(FVector2D::DotProduct(D, Direction)) / (0.5 * Span) : 0.0;
			}
			else if (Style == TEXT("Dmnd"))
			{
				const FVector2D Perp(-Direction.Y, Direction.X);
				T = Radius > 0 ? (FMath::Abs(FVector2D::DotProduct(D, Direction)) + FMath::Abs(FVector2D::DotProduct(D, Perp))) / Radius : 0.0;
			}
			else
			{
				T = Span > 0 ? FVector2D::DotProduct(D, Direction) / Span + 0.5 : 0.0;
			}
			T = FMath::Clamp(T, 0.0, 1.0);
			if (InFill.bReverse)
			{
				T = 1.0 - T;
			}
			const FLinearColor C = InFill.Gradient.Evaluate((float)T);
			auto Q = [](float V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255); };
			Out.Pixels[y * Out.Width + x] = FColor(Q(C.R), Q(C.G), Q(C.B), Q(C.A));
		}
	}
	return Out;
}

void DreamPsdRaster::MultiplyAlpha(FDreamPsdImage& InOutImage, const FDreamPsdGray& InMask)
{
	const int32 Count = FMath::Min(InOutImage.Pixels.Num(), InMask.Pixels.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		InOutImage.Pixels[i].A = (uint8)((InOutImage.Pixels[i].A * InMask.Pixels[i] + 127) / 255);
	}
}

void DreamPsdRaster::MultiplyMask(FDreamPsdGray& InOutMask, const FDreamPsdGray& InOther)
{
	const int32 Count = FMath::Min(InOutMask.Pixels.Num(), InOther.Pixels.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		InOutMask.Pixels[i] = (uint8)((InOutMask.Pixels[i] * InOther.Pixels[i] + 127) / 255);
	}
}

bool DreamPsdRaster::IsAllOpaque(const FDreamPsdGray& InMask)
{
	for (uint8 V : InMask.Pixels)
	{
		if (V != 255)
		{
			return false;
		}
	}
	return true;
}
