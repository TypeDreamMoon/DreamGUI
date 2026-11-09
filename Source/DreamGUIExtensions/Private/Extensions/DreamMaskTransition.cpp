// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/DreamMaskTransition.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "DreamUIBPLibrary.h"
#include "DreamTweenBPLibrary.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace DreamMaskTransitionLocal
{
	/** Ink and Shatter measure order at these sizes; the checker's two-by-two squares at this. */
	constexpr int32 InkExtent = 256;
	constexpr int32 ShatterExtent = 512;
	constexpr int32 CheckerExtent = 64;

	float Rand(uint32 X, uint32 Y, uint32 Seed)
	{
		return static_cast<float>(FDreamUIClipMaskMath::Hash(X, Y, Seed) & 0xFFFFFFu) / 16777215.0f;
	}

	/** The width a box of InHalfSize spans along the direction InRadians makes with +x: how far a pattern turned so reaches. */
	float ExtentAlong(FVector2f InHalfSize, float InRadians)
	{
		return 2.0f * (InHalfSize.X * FMath::Abs(FMath::Cos(InRadians)) + InHalfSize.Y * FMath::Abs(FMath::Sin(InRadians)));
	}

	/**
	 * The UV a pattern is read at, for a point InPixel of the clip (from its centre, y down), as the mask's own arithmetic
	 * makes it (FDreamUIClipMaskMath::PackMask and EvaluateMaskedLevel): turned by InRadians, scaled, moved by InOffset.
	 */
	FVector2f PatternUV(FVector2f InPixel, FVector2f InSize, float InRadians, FVector2D InScale, FVector2D InOffset)
	{
		const FVector2f Half = InSize * 0.5f;
		const float Kx = 1.0f / FMath::Max(ExtentAlong(Half, InRadians) * static_cast<float>(InScale.X), 0.0001f);
		const float Ky = 1.0f / FMath::Max(ExtentAlong(Half, InRadians + UE_HALF_PI) * static_cast<float>(InScale.Y), 0.0001f);
		const float S = FMath::Sin(InRadians);
		const float C = FMath::Cos(InRadians);
		const FVector2f Turned(C * InPixel.X + S * InPixel.Y, -S * InPixel.X + C * InPixel.Y);
		return FVector2f(Turned.X * Kx, Turned.Y * Ky) + FVector2f(0.5f, 0.5f) + FVector2f(InOffset);
	}

	/** The offset that puts a pattern's centre (UV 0.5, 0.5) at InCentre of the rect (0..1, y down). */
	FVector2D CentreOffset(FVector2D InCentre, FVector2f InSize, float InRadians, FVector2D InScale)
	{
		const FVector2f Pixel((InCentre.X - 0.5) * InSize.X, (InCentre.Y - 0.5) * InSize.Y);
		const FVector2f AtCentre = PatternUV(Pixel, InSize, InRadians, InScale, FVector2D::ZeroVector);
		return FVector2D(0.5f - AtCentre.X, 0.5f - AtCentre.Y);
	}

	/** The lowest and highest a pattern reads over the rect: on a grid, its corners and InCentre included. */
	void MeasurePattern(int32 InPattern, FVector2f InSize, float InRadians, FVector2D InScale, FVector2D InOffset, FVector2D InCentre, float& OutMin, float& OutMax)
	{
		OutMin = MAX_flt;
		OutMax = -MAX_flt;
		auto Visit = [&](FVector2f InAt)
		{
			const FVector2f Pixel((InAt.X - 0.5f) * InSize.X, (InAt.Y - 0.5f) * InSize.Y);
			const float Value = FDreamUIClipMaskMath::Pattern(InPattern, PatternUV(Pixel, InSize, InRadians, InScale, InOffset), 1.0f, 0u);
			OutMin = FMath::Min(OutMin, Value);
			OutMax = FMath::Max(OutMax, Value);
		};
		constexpr int32 Steps = 16;
		for (int32 Y = 0; Y <= Steps; ++Y)
		{
			for (int32 X = 0; X <= Steps; ++X)
			{
				Visit(FVector2f(static_cast<float>(X) / Steps, static_cast<float>(Y) / Steps));
			}
		}
		const FVector2f Inside(FMath::Clamp(static_cast<float>(InCentre.X), 0.0f, 1.0f), FMath::Clamp(static_cast<float>(InCentre.Y), 0.0f, 1.0f));
		Visit(Inside);
	}

	/** A transient single-channel half-float texture of the order values, row 0 at the top. */
	UTexture2D* MakeOrderTexture(int32 InWidth, int32 InHeight, const TArray<float>& InValues, bool bInNearest, bool bInWrap)
	{
		if (InWidth <= 0 || InHeight <= 0 || InValues.Num() != InWidth * InHeight)
		{
			return nullptr;
		}
		UTexture2D* Texture = UTexture2D::CreateTransient(InWidth, InHeight, PF_R16F, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("DreamMaskTransitionOrder")));
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		Texture->SRGB = false;
		Texture->Filter = bInNearest ? TF_Nearest : TF_Bilinear;
		Texture->AddressX = bInWrap ? TA_Wrap : TA_Clamp;
		Texture->AddressY = bInWrap ? TA_Wrap : TA_Clamp;
		Texture->NeverStream = true;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FFloat16* Texels = static_cast<FFloat16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < InValues.Num(); ++Index)
		{
			Texels[Index] = FFloat16(InValues[Index]);
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	/** A mask that hides everything, and is hit nowhere. */
	FDreamClipMask MakeHidden()
	{
		FDreamClipMask Mask;
		Mask.Source = EDreamClipMaskSource::Procedural;
		Mask.Pattern = EDreamClipMaskPattern::Linear;
		Mask.Threshold = 4.0f;
		Mask.Softness = 0.0f;
		Mask.HitTest = EDreamClipMaskHitTest::Mask;
		return Mask;
	}

	float Ease(EDreamTweenEase InEase, float InTime)
	{
		const FDreamTweenFunction Function = UDreamTweener::GetEaseFunction(InEase);
		return Function.IsBound() ? Function.Execute(1.0f, 0.0f, InTime, 1.0f) : InTime;
	}
}

void FDreamMaskTransitionMath::Normalize(TArray<float>& InOutValues)
{
	if (InOutValues.Num() == 0)
	{
		return;
	}
	float Min = MAX_flt, Max = -MAX_flt;
	for (const float Value : InOutValues)
	{
		Min = FMath::Min(Min, Value);
		Max = FMath::Max(Max, Value);
	}
	const float Range = Max - Min;
	for (float& Value : InOutValues)
	{
		Value = Range > 0.0f ? (Value - Min) / Range : 0.0f;
	}
}

float FDreamMaskTransitionMath::Fbm(FVector2f InPoint, uint32 InSeed)
{
	using namespace DreamMaskTransitionLocal;
	float Sum = 0.0f;
	float Amplitude = 0.5f;
	float Total = 0.0f;
	FVector2f Point = InPoint;
	for (int32 Octave = 0; Octave < 4; ++Octave)
	{
		const int32 X0 = FMath::FloorToInt(Point.X);
		const int32 Y0 = FMath::FloorToInt(Point.Y);
		const float Fx = Point.X - X0;
		const float Fy = Point.Y - Y0;
		const float Sx = Fx * Fx * (3.0f - 2.0f * Fx);
		const float Sy = Fy * Fy * (3.0f - 2.0f * Fy);
		const uint32 OctaveSeed = InSeed * 4u + static_cast<uint32>(Octave);
		const float A = Rand(static_cast<uint32>(X0), static_cast<uint32>(Y0), OctaveSeed);
		const float B = Rand(static_cast<uint32>(X0 + 1), static_cast<uint32>(Y0), OctaveSeed);
		const float C = Rand(static_cast<uint32>(X0), static_cast<uint32>(Y0 + 1), OctaveSeed);
		const float D = Rand(static_cast<uint32>(X0 + 1), static_cast<uint32>(Y0 + 1), OctaveSeed);
		const float Value = FMath::Lerp(FMath::Lerp(A, B, Sx), FMath::Lerp(C, D, Sx), Sy);
		Sum += (Value - 0.5f) * Amplitude;
		Total += Amplitude;
		Amplitude *= 0.5f;
		Point = Point * 2.0f + FVector2f(17.0f, 31.0f);
	}
	return Total > 0.0f ? Sum / Total : 0.0f;
}

void FDreamMaskTransitionMath::MakeCheckerTile(int32 InExtent, TArray<float>& OutOrder)
{
	InExtent = FMath::Max(2, InExtent & ~1);
	const int32 Cell = InExtent / 2;
	OutOrder.SetNumUninitialized(InExtent * InExtent);
	for (int32 Y = 0; Y < InExtent; ++Y)
	{
		for (int32 X = 0; X < InExtent; ++X)
		{
			// One colour of squares first, each wiping in from its left; then the other.
			const int32 Parity = ((X / Cell) + (Y / Cell)) & 1;
			const float Along = (static_cast<float>(X % Cell) + 0.5f) / Cell;
			OutOrder[Y * InExtent + X] = 0.5f * Parity + 0.5f * Along;
		}
	}
}

void FDreamMaskTransitionMath::MakeInk(int32 InWidth, int32 InHeight, float InAspect, FVector2f InCentre, int32 InSeed, TArray<float>& OutOrder)
{
	OutOrder.SetNumUninitialized(InWidth * InHeight);
	const float Aspect = FMath::Max(InAspect, 0.01f);
	const uint32 Seed = static_cast<uint32>(FMath::Max(InSeed, 0));
	for (int32 Y = 0; Y < InHeight; ++Y)
	{
		for (int32 X = 0; X < InWidth; ++X)
		{
			// In units of the widget's height, so that the blot is round on any widget.
			const FVector2f At(((X + 0.5f) / InWidth - InCentre.X) * Aspect, (Y + 0.5f) / InHeight - InCentre.Y);
			const float Distance = At.Size();
			// The edge is pushed out and in by noise, more the farther it has spread, and the noise is pulled along by
			// noise of its own so that the edge runs out in tendrils.
			const FVector2f Warp(Fbm(At * 2.0f, Seed + 101u), Fbm(At * 2.0f + FVector2f(5.2f, 1.3f), Seed + 202u));
			const float Ragged = Fbm(At * 4.0f + Warp * 1.5f, Seed);
			OutOrder[Y * InWidth + X] = Distance + Ragged * (0.25f + 0.6f * Distance);
		}
	}
	Normalize(OutOrder);
}

void FDreamMaskTransitionMath::MakeShatter(int32 InWidth, int32 InHeight, float InAspect, FVector2f InCentre, int32 InShards, int32 InSeed, TArray<float>& OutOrder)
{
	using namespace DreamMaskTransitionLocal;
	OutOrder.SetNumUninitialized(InWidth * InHeight);
	const float Aspect = FMath::Max(InAspect, 0.01f);
	const uint32 Seed = static_cast<uint32>(FMath::Max(InSeed, 0));
	const int32 Shards = FMath::Max(InShards, 1);
	// A grid of about InShards cells, as square as the widget allows, each with one seed somewhere in its middle.
	const int32 Columns = FMath::Max(1, FMath::RoundToInt(FMath::Sqrt(Shards * Aspect)));
	const int32 Rows = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Shards) / Columns));
	const FVector2f CellSize(Aspect / Columns, 1.0f / Rows);
	TArray<FVector2f> Seeds;
	TArray<float> Order;
	Seeds.SetNumUninitialized(Columns * Rows);
	Order.SetNumUninitialized(Columns * Rows);
	const FVector2f Centre(InCentre.X * Aspect, InCentre.Y);
	float Farthest = 0.0f;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const int32 Index = Row * Columns + Column;
			Seeds[Index] = FVector2f((Column + 0.2f + 0.6f * Rand(Column, Row, Seed)) * CellSize.X, (Row + 0.2f + 0.6f * Rand(Column, Row, Seed + 1u)) * CellSize.Y);
			Farthest = FMath::Max(Farthest, FVector2f::Distance(Seeds[Index], Centre));
		}
	}
	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		Order[Index] = 0.65f * FVector2f::Distance(Seeds[Index], Centre) / FMath::Max(Farthest, 0.0001f)
			+ 0.35f * Rand(static_cast<uint32>(Index), 7u, Seed + 2u);
	}
	const float CellDiagonal = CellSize.Size();
	for (int32 Y = 0; Y < InHeight; ++Y)
	{
		for (int32 X = 0; X < InWidth; ++X)
		{
			const FVector2f At((X + 0.5f) / InWidth * Aspect, (Y + 0.5f) / InHeight);
			const int32 CellX = FMath::Clamp(FMath::FloorToInt(At.X / CellSize.X), 0, Columns - 1);
			const int32 CellY = FMath::Clamp(FMath::FloorToInt(At.Y / CellSize.Y), 0, Rows - 1);
			// A seed sits in the middle three fifths of its cell, so the nearest is one of the nine cells around.
			float Nearest = MAX_flt;
			int32 Owner = 0;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					const int32 Cx = CellX + Dx, Cy = CellY + Dy;
					if (Cx < 0 || Cy < 0 || Cx >= Columns || Cy >= Rows)
					{
						continue;
					}
					const float Distance = FVector2f::DistSquared(At, Seeds[Cy * Columns + Cx]);
					if (Distance < Nearest)
					{
						Nearest = Distance;
						Owner = Cy * Columns + Cx;
					}
				}
			}
			// A shard grows from its seed out to its edges over a short while of its own.
			OutOrder[Y * InWidth + X] = Order[Owner] + 0.03f * FMath::Sqrt(Nearest) / FMath::Max(CellDiagonal, 0.0001f);
		}
	}
	Normalize(OutOrder);
}

float FDreamMaskTransitionMath::Threshold(float InProgress, float InMin, float InMax, float InSoftness, bool bInHide)
{
	// Showing, the mask shows where the value is above the threshold, and the threshold falls from past the highest value
	// (nothing shows) to below the lowest (everything does). Hiding is the exact complement, read from the source run the
	// other way (1 - value): the threshold is 1 minus the showing one.
	const float Progress = FMath::Clamp(InProgress, 0.0f, 1.0f);
	const float Span = (InMax - InMin) + 2.0f * InSoftness;
	const float Showing = InMax + InSoftness - Progress * Span;
	return bInHide ? 1.0f - Showing : Showing;
}

UDreamMaskTransition::UDreamMaskTransition()
{
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
}

#if WITH_EDITOR
void UDreamMaskTransition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Refresh();
}
#endif

void UDreamMaskTransition::OnRegister()
{
	Super::OnRegister();
	Refresh();
}

void UDreamMaskTransition::OnUnregister()
{
	Stop();
	StopDriving();
	Super::OnUnregister();
}

void UDreamMaskTransition::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	if (WidthChanged || HeightChanged)
	{
		Refresh();
	}
}

bool UDreamMaskTransition::IsFullyShown() const
{
	return bInvert ? Progress <= 0.0f : Progress >= 1.0f;
}

bool UDreamMaskTransition::IsFullyHidden() const
{
	return bInvert ? Progress >= 1.0f : Progress <= 0.0f;
}

void UDreamMaskTransition::StopDriving()
{
	if (bDriving)
	{
		if (UDreamWidget* Widget = DrivenWidget.Get())
		{
			Widget->SetDrivenClipMask(this, nullptr);
		}
	}
	bDriving = false;
	DrivenWidget.Reset();
}

UTexture* UDreamMaskTransition::PrepareOrderTexture(FVector2f InSize)
{
	using namespace DreamMaskTransitionLocal;
	if (Preset != EDreamMaskTransitionPreset::Ink && Preset != EDreamMaskTransitionPreset::Shatter && Preset != EDreamMaskTransitionPreset::Checker)
	{
		return nullptr;
	}
	// The checker's tile is the same for every size; ink and shards are made for the widget's shape, to the hundredth.
	const float Aspect = InSize.Y > 0.0f ? InSize.X / InSize.Y : 1.0f;
	const bool bShaped = Preset != EDreamMaskTransitionPreset::Checker;
	uint32 Key = GetTypeHash(static_cast<uint8>(Preset));
	if (bShaped)
	{
		Key = HashCombine(Key, GetTypeHash(FMath::RoundToInt(Aspect * 100.0f)));
		Key = HashCombine(Key, GetTypeHash(FMath::RoundToInt(Centre.X * 1000.0)));
		Key = HashCombine(Key, GetTypeHash(FMath::RoundToInt(Centre.Y * 1000.0)));
		Key = HashCombine(Key, GetTypeHash(Seed));
		Key = HashCombine(Key, GetTypeHash(Preset == EDreamMaskTransitionPreset::Shatter ? Count : 0));
	}
	if (OrderTexture != nullptr && OrderTextureKey == Key)
	{
		return OrderTexture;
	}
	TArray<float> Order;
	UTexture2D* Made = nullptr;
	switch (Preset)
	{
	case EDreamMaskTransitionPreset::Ink:
		FDreamMaskTransitionMath::MakeInk(InkExtent, InkExtent, Aspect, FVector2f(Centre), Seed, Order);
		Made = MakeOrderTexture(InkExtent, InkExtent, Order, false, false);
		break;
	case EDreamMaskTransitionPreset::Shatter:
		FDreamMaskTransitionMath::MakeShatter(ShatterExtent, ShatterExtent, Aspect, FVector2f(Centre), Count, Seed, Order);
		Made = MakeOrderTexture(ShatterExtent, ShatterExtent, Order, false, false);
		break;
	default:
		FDreamMaskTransitionMath::MakeCheckerTile(CheckerExtent, Order);
		// Nearest, so that where one square ends and the next begins does not blend the end of one sweep with the start of the next.
		Made = MakeOrderTexture(CheckerExtent, CheckerExtent, Order, true, true);
		break;
	}
	OrderTexture = Made;
	OrderTextureKey = Key;
	return OrderTexture;
}

bool UDreamMaskTransition::MakeMask(FVector2f InSize, FDreamClipMask& OutMask)
{
	using namespace DreamMaskTransitionLocal;
	OutMask = FDreamClipMask();
	if (InSize.X <= 0.0f || InSize.Y <= 0.0f)
	{
		return false;
	}
	EDreamMaskTransitionPreset Shape = Preset;
	if (Shape == EDreamMaskTransitionPreset::Texture && Texture == nullptr)
	{
		Shape = EDreamMaskTransitionPreset::Wipe;
	}
	// The order each preset reads: low shows first. Its range over the rect is [Min, Max].
	float Min = 0.0f, Max = 1.0f;
	bool bProcedural = true;
	const float Radians = FMath::DegreesToRadians(Angle);
	switch (Shape)
	{
	case EDreamMaskTransitionPreset::Wipe:
		OutMask.Source = EDreamClipMaskSource::Procedural;
		OutMask.Pattern = EDreamClipMaskPattern::Linear;
		OutMask.Rotation = Angle;
		break;
	case EDreamMaskTransitionPreset::Clock:
		OutMask.Source = EDreamClipMaskSource::Procedural;
		OutMask.Pattern = EDreamClipMaskPattern::Angular;
		OutMask.Rotation = Angle;
		OutMask.Offset = CentreOffset(Centre, InSize, Radians, OutMask.Scale);
		break;
	case EDreamMaskTransitionPreset::Iris:
	case EDreamMaskTransitionPreset::Diamond:
	{
		OutMask.Source = EDreamClipMaskSource::Procedural;
		OutMask.Pattern = Shape == EDreamMaskTransitionPreset::Iris ? EDreamClipMaskPattern::Radial : EDreamClipMaskPattern::Diamond;
		// As tall as it is wide, on any widget: a circle, a square diamond.
		OutMask.Scale = FVector2D(1.0, InSize.X / InSize.Y);
		OutMask.Offset = CentreOffset(Centre, InSize, 0.0f, OutMask.Scale);
		MeasurePattern(static_cast<int32>(OutMask.Pattern), InSize, 0.0f, OutMask.Scale, OutMask.Offset, Centre, Min, Max);
		break;
	}
	case EDreamMaskTransitionPreset::Blinds:
		OutMask.Source = EDreamClipMaskSource::Procedural;
		OutMask.Pattern = EDreamClipMaskPattern::Blinds;
		OutMask.Frequency = static_cast<float>(FMath::Max(Count, 1));
		OutMask.Rotation = Angle;
		break;
	case EDreamMaskTransitionPreset::Dissolve:
		OutMask.Source = EDreamClipMaskSource::Procedural;
		OutMask.Pattern = EDreamClipMaskPattern::Noise;
		OutMask.Seed = Seed;
		// Square specks Grain pixels across.
		OutMask.Scale = FVector2D(1.0, InSize.X / InSize.Y);
		OutMask.Frequency = FMath::Max(1.0f, InSize.X / FMath::Max(Grain, 1.0f));
		break;
	case EDreamMaskTransitionPreset::Checker:
	{
		bProcedural = false;
		OutMask.Source = EDreamClipMaskSource::Texture;
		OutMask.Texture = PrepareOrderTexture(InSize);
		OutMask.Channel = EDreamClipMaskChannel::Red;
		OutMask.Fill = EDreamClipMaskFill::Tile;
		// The tile is two squares across; a square is the widget's width over Count.
		const double Square = InSize.X / FMath::Max(Count, 1);
		const double TileScale = 2.0 * Square / CheckerExtent;
		OutMask.Scale = FVector2D(TileScale, TileScale);
		break;
	}
	case EDreamMaskTransitionPreset::Ink:
	case EDreamMaskTransitionPreset::Shatter:
		bProcedural = false;
		OutMask.Source = EDreamClipMaskSource::Texture;
		OutMask.Texture = PrepareOrderTexture(InSize);
		OutMask.Channel = EDreamClipMaskChannel::Red;
		OutMask.Fill = EDreamClipMaskFill::Stretch;
		break;
	case EDreamMaskTransitionPreset::Texture:
	default:
		bProcedural = false;
		OutMask.Source = EDreamClipMaskSource::Texture;
		OutMask.Texture = Texture;
		OutMask.Channel = Channel;
		OutMask.Fill = EDreamClipMaskFill::Stretch;
		break;
	}
	if (OutMask.Source == EDreamClipMaskSource::Texture && OutMask.Texture == nullptr)
	{
		return false;
	}
	// The mask shows where the value it reads is above the threshold; the order shows low first, so the mask reads it run
	// the other way (1 - order) unless Reverse says the order itself.
	OutMask.bReverse = !bReverse;
	const float ReadMin = OutMask.bReverse ? 1.0f - Max : Min;
	const float ReadMax = OutMask.bReverse ? 1.0f - Min : Max;
	const float Soft = FMath::Max(Softness * (ReadMax - ReadMin), 0.0001f);
	OutMask.Threshold = FDreamMaskTransitionMath::Threshold(Progress, ReadMin, ReadMax, Soft, bInvert);
	OutMask.Softness = Soft;
	if (bInvert)
	{
		OutMask.bReverse = !OutMask.bReverse;
	}
	OutMask.HitTest = bHitTestMask && bProcedural ? EDreamClipMaskHitTest::Mask : EDreamClipMaskHitTest::Bounds;
	return true;
}

void UDreamMaskTransition::Refresh()
{
	using namespace DreamMaskTransitionLocal;
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || !bIsRegisteredWithWidget || !bEnabled || IsFullyShown())
	{
		StopDriving();
		return;
	}
	FDreamClipMask Mask;
	if (IsFullyHidden() || !MakeMask(FVector2f(Widget->GetWidth(), Widget->GetHeight()), Mask))
	{
		Mask = MakeHidden();
	}
	if (bDriving && DrivenWidget.Get() == Widget)
	{
		if (Mask == DrivenMask)
		{
			return;
		}
		const bool bTextureChanged = Mask.GetActiveTexture() != DrivenMask.GetActiveTexture();
		DrivenMask = Mask;
		Widget->MarkDrivenClipMaskChanged(bTextureChanged);
		return;
	}
	StopDriving();
	DrivenMask = Mask;
	Widget->SetDrivenClipMask(this, &DrivenMask);
	bDriving = true;
	DrivenWidget = Widget;
}

void UDreamMaskTransition::SetEnabled(bool Value)
{
	if (bEnabled != Value)
	{
		bEnabled = Value;
		Refresh();
	}
}

void UDreamMaskTransition::SetProgress(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (Progress != Value)
	{
		Progress = Value;
		Refresh();
	}
}

void UDreamMaskTransition::SetPreset(EDreamMaskTransitionPreset Value) { if (Preset != Value) { Preset = Value; Refresh(); } }
void UDreamMaskTransition::SetSoftness(float Value) { Softness = FMath::Clamp(Value, 0.0f, 0.5f); Refresh(); }
void UDreamMaskTransition::SetInvert(bool Value) { if (bInvert != Value) { bInvert = Value; Refresh(); } }
void UDreamMaskTransition::SetReverse(bool Value) { if (bReverse != Value) { bReverse = Value; Refresh(); } }
void UDreamMaskTransition::SetCentre(FVector2D Value) { Centre = Value; Refresh(); }
void UDreamMaskTransition::SetAngle(float Value) { Angle = Value; Refresh(); }
void UDreamMaskTransition::SetCount(int32 Value) { Count = FMath::Clamp(Value, 1, 256); Refresh(); }
void UDreamMaskTransition::SetGrain(float Value) { Grain = FMath::Max(Value, 1.0f); Refresh(); }
void UDreamMaskTransition::SetSeed(int32 Value) { Seed = FMath::Max(Value, 0); Refresh(); }
void UDreamMaskTransition::SetTexture(UTexture* Value, EDreamClipMaskChannel InChannel) { Texture = Value; Channel = InChannel; Refresh(); }

UDreamTweener* UDreamMaskTransition::PlayFromTo(float InFrom, float InTo, float InDuration, EDreamTweenEase InEase)
{
	using namespace DreamMaskTransitionLocal;
	Stop();
	SetProgress(InFrom);
	TWeakObjectPtr<UDreamMaskTransition> WeakThis(this);
	UDreamTweener* Tween = UDreamTweenBPLibrary::VirtualCall(this, FMath::Max(InDuration, 0.0f), 0.0f,
		[]() {},
		[WeakThis, InFrom, InTo, InEase](float InTime)
		{
			if (UDreamMaskTransition* Self = WeakThis.Get())
			{
				Self->SetProgress(FMath::Lerp(InFrom, InTo, Ease(InEase, InTime)));
			}
		},
		[WeakThis, InTo]()
		{
			if (UDreamMaskTransition* Self = WeakThis.Get())
			{
				Self->SetProgress(InTo);
			}
		});
	if (Tween == nullptr)
	{
		// No tweens in this world (none is running its manager): the end at once, as a tween of no time would end.
		SetProgress(InTo);
	}
	PlayingTween = Tween;
	return Tween;
}

UDreamTweener* UDreamMaskTransition::PlayForward(float InDuration, EDreamTweenEase InEase)
{
	return PlayFromTo(Progress, 1.0f, InDuration, InEase);
}

UDreamTweener* UDreamMaskTransition::PlayBackward(float InDuration, EDreamTweenEase InEase)
{
	return PlayFromTo(Progress, 0.0f, InDuration, InEase);
}

void UDreamMaskTransition::Stop()
{
	if (UDreamTweener* Tween = PlayingTween.Get())
	{
		UDreamTweenBPLibrary::KillIfIsTweening(this, Tween);
	}
	PlayingTween.Reset();
}

UDreamMaskTransition* UDreamMaskTransition::FindOrAdd(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	if (UDreamMaskTransition* Found = InWidget->GetComponent<UDreamMaskTransition>())
	{
		return Found;
	}
	return InWidget->AddComponent<UDreamMaskTransition>();
}

UDreamTweener* UDreamMaskTransition::CrossFade(UDreamWidget* InFrom, UDreamWidget* InTo, EDreamMaskTransitionPreset InPreset, float InDuration, EDreamTweenEase InEase)
{
	using namespace DreamMaskTransitionLocal;
	UDreamMaskTransition* Out = FindOrAdd(InFrom);
	UDreamMaskTransition* In = FindOrAdd(InTo);
	UDreamMaskTransition* Leader = In != nullptr ? In : Out;
	if (Leader == nullptr)
	{
		return nullptr;
	}
	for (UDreamMaskTransition* Each : { Out, In })
	{
		if (Each != nullptr)
		{
			Each->Stop();
			Each->Preset = InPreset;
		}
	}
	if (Out != nullptr && In != nullptr)
	{
		// The two read one shape exactly when they are set alike: the one going takes the settings of the one coming.
		Out->Softness = In->Softness;
		Out->bReverse = In->bReverse;
		Out->Centre = In->Centre;
		Out->Angle = In->Angle;
		Out->Count = In->Count;
		Out->Grain = In->Grain;
		Out->Seed = In->Seed;
		Out->Texture = In->Texture;
		Out->Channel = In->Channel;
	}
	if (Out != nullptr)
	{
		Out->bInvert = true;
		Out->Progress = -1.0f;
		Out->SetProgress(0.0f);
	}
	if (In != nullptr)
	{
		In->bInvert = false;
		In->Progress = -1.0f;
		In->SetProgress(0.0f);
	}
	TWeakObjectPtr<UDreamMaskTransition> WeakOut(Out), WeakIn(In);
	auto Apply = [WeakOut, WeakIn](float InProgress)
	{
		if (UDreamMaskTransition* Going = WeakOut.Get())
		{
			Going->SetProgress(InProgress);
		}
		if (UDreamMaskTransition* Coming = WeakIn.Get())
		{
			Coming->SetProgress(InProgress);
		}
	};
	UDreamTweener* Tween = UDreamTweenBPLibrary::VirtualCall(Leader, FMath::Max(InDuration, 0.0f), 0.0f,
		[]() {},
		[Apply, InEase](float InTime) { Apply(Ease(InEase, InTime)); },
		[Apply]() { Apply(1.0f); });
	if (Tween == nullptr)
	{
		// No tweens in this world: swapped at once.
		Apply(1.0f);
	}
	for (UDreamMaskTransition* Each : { Out, In })
	{
		if (Each != nullptr)
		{
			Each->PlayingTween = Tween;
		}
	}
	return Tween;
}

UDreamMaskTransition* UDreamMaskTransition::PlayScreenTransition(UObject* WorldContextObject, EDreamMaskTransitionPreset InPreset, float InCoverSeconds, float InHoldSeconds, float InUncoverSeconds,
	FDreamScreenTransitionEvent OnCovered, FDreamScreenTransitionEvent OnFinished, FLinearColor InColour, int32 InSortOrder)
{
	return PlayScreenTransition(WorldContextObject, InPreset, InCoverSeconds, InHoldSeconds, InUncoverSeconds,
		TFunction<void()>([OnCovered]() { OnCovered.ExecuteIfBound(); }),
		TFunction<void()>([OnFinished]() { OnFinished.ExecuteIfBound(); }),
		InColour, InSortOrder);
}

UDreamMaskTransition* UDreamMaskTransition::PlayScreenTransition(UObject* WorldContextObject, EDreamMaskTransitionPreset InPreset, float InCoverSeconds, float InHoldSeconds, float InUncoverSeconds,
	TFunction<void()> OnCovered, TFunction<void()> OnFinished, FLinearColor InColour, int32 InSortOrder)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	UDreamScreenUISubsystem* Screen = World != nullptr ? UDreamScreenUISubsystem::Get(World) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	UDreamWidget* Cover = UDreamUIBPLibrary::ConstructWidget(World, TEXT("ScreenTransition"), UDreamTexture::StaticClass());
	if (Cover == nullptr)
	{
		return nullptr;
	}
	if (UDreamTexture* Fill = Cast<UDreamTexture>(Cover->GetVisual()))
	{
		Fill->SetColor(InColour.ToFColor(true));
	}
	UDreamMaskTransition* Transition = Cover->AddComponent<UDreamMaskTransition>();
	if (Transition == nullptr)
	{
		return nullptr;
	}
	Transition->Preset = InPreset;
	Transition->Progress = 0.0f;
	Screen->AddToViewport(Cover, InSortOrder);
	Transition->Refresh();
	TWeakObjectPtr<UDreamMaskTransition> WeakTransition(Transition);
	TWeakObjectPtr<UDreamScreenUISubsystem> WeakScreen(Screen);
	TWeakObjectPtr<UDreamWidget> WeakCover(Cover);
	UDreamTweener* Covering = Transition->PlayFromTo(0.0f, 1.0f, InCoverSeconds);
	if (Covering == nullptr)
	{
		// No tweens in this world: covered and uncovered at once.
		if (OnCovered)
		{
			OnCovered();
		}
		Screen->RemoveFromViewport(Cover);
		if (OnFinished)
		{
			OnFinished();
		}
		return nullptr;
	}
	Covering->OnComplete([WeakTransition, WeakScreen, WeakCover, OnCovered, OnFinished, InHoldSeconds, InUncoverSeconds]()
	{
		if (OnCovered)
		{
			OnCovered();
		}
		UDreamMaskTransition* Self = WeakTransition.Get();
		if (Self == nullptr)
		{
			if (OnFinished)
			{
				OnFinished();
			}
			return;
		}
		// Shown either way at this point: hiding at 0 is showing at 1. The same shape then takes the cover away.
		Self->bInvert = true;
		Self->Progress = 0.0f;
		Self->Refresh();
		UDreamTweener* Uncovering = Self->PlayFromTo(0.0f, 1.0f, InUncoverSeconds);
		if (Uncovering == nullptr)
		{
			if (UDreamScreenUISubsystem* Subsystem = WeakScreen.Get())
			{
				if (UDreamWidget* Done = WeakCover.Get())
				{
					Subsystem->RemoveFromViewport(Done);
				}
			}
			if (OnFinished)
			{
				OnFinished();
			}
			return;
		}
		Uncovering->SetDelay(FMath::Max(InHoldSeconds, 0.0f));
		Uncovering->OnComplete([WeakScreen, WeakCover, OnFinished]()
		{
			if (UDreamScreenUISubsystem* Subsystem = WeakScreen.Get())
			{
				if (UDreamWidget* Done = WeakCover.Get())
				{
					Subsystem->RemoveFromViewport(Done);
				}
			}
			if (OnFinished)
			{
				OnFinished();
			}
		});
	});
	return Transition;
}
