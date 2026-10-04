// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/DreamGUISettings.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextPaint.h"
#include "Math/RandomStream.h"
#include "Misc/ScopeExit.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUIValueFormat.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include <limits>

/*
 * A text's paints on the CPU: the gradient's CSS spelling, its `.dui` short form, the evaluation every pixel of a painted
 * glyph goes through, and the row it is packed into for the shader.
 *
 * The evaluation is held to numbers worked out from the CSS specification by hand -- the gradient line with its "magic
 * corners", the radial ending shapes, the conic start, hard stops, the spread -- most of them through a ramp from black to
 * white mixed in linear light, whose red channel IS the gradient parameter t: what a check about geometry wants to read,
 * with nothing of the colour maths in the way. The colour maths has checks of its own. The packed row is decoded here
 * the way DreamUIPaint.ush loads it, pixel by pixel, and must evaluate to exactly what Evaluate answers.
 */
namespace DreamTextPaintTestLocal
{
	/** Black to white mixed in linear light: Evaluate's red is then t itself, between the ends. */
	FDreamGradient MakeRamp(EDreamPaintType InType)
	{
		FDreamGradient Ramp;
		Ramp.Type = InType;
		Ramp.Interpolation = EDreamPaintInterpolation::Linear;
		Ramp.Stops = { FDreamGradientStop(0.0f, FColor::Black), FDreamGradientStop(1.0f, FColor::White) };
		return Ramp;
	}

	FDreamGradient MakeTwoStops(const FColor& InFrom, const FColor& InTo, EDreamPaintInterpolation InSpace = EDreamPaintInterpolation::SRGB)
	{
		FDreamGradient Gradient;
		Gradient.Angle = 90.0f;
		Gradient.Interpolation = InSpace;
		Gradient.Stops = { FDreamGradientStop(0.0f, InFrom), FDreamGradientStop(1.0f, InTo) };
		return Gradient;
	}

	/** A gradient whose fields are all away from their defaults: what reading must not leave at a default. */
	FDreamGradient MakeSentinel()
	{
		FDreamGradient Sentinel;
		Sentinel.Type = EDreamPaintType::Conic;
		Sentinel.Angle = 12.0f;
		Sentinel.Center = FVector2f(0.1f, 0.2f);
		Sentinel.Scale = 3.0f;
		Sentinel.Stops = { FDreamGradientStop(0.25f, FColor(1, 2, 3, 4)) };
		return Sentinel;
	}

	/** A value drawn from FRandomStream, now and then one of the awkward ones: zero, a sign, a tiny or a huge magnitude. */
	float MakeAwkwardFloat(FRandomStream& InRandom, float InMin, float InMax)
	{
		const float Special[] = { 0.0f, -0.0f, 1.0f, 0.5f, 1.0e-30f, -3.0e-7f, 1.0e20f, 0.1f, 1.0f / 3.0f };
		if (InRandom.FRand() < 0.15f)
		{
			return Special[InRandom.RandRange(0, static_cast<int32>(UE_ARRAY_COUNT(Special)) - 1)];
		}
		return InRandom.FRandRange(InMin, InMax);
	}

	/**
	 * A random gradient: any type, spread, space, shape and size, half its numbers left at their defaults so the spellings
	 * that leave them out are walked as often as the others, and stops either where CSS would place them or anywhere.
	 */
	FDreamGradient MakeRandomGradient(FRandomStream& InRandom)
	{
		FDreamGradient Gradient;
		Gradient.Type = static_cast<EDreamPaintType>(InRandom.RandRange(0, 5));
		Gradient.Spread = static_cast<EDreamPaintSpread>(InRandom.RandRange(0, 2));
		Gradient.Interpolation = static_cast<EDreamPaintInterpolation>(InRandom.RandRange(0, 2));
		Gradient.Shape = static_cast<EDreamPaintRadialShape>(InRandom.RandRange(0, 1));
		Gradient.Size = static_cast<EDreamPaintRadialSize>(InRandom.RandRange(0, 4));
		if (InRandom.FRand() < 0.5f)
		{
			Gradient.Angle = MakeAwkwardFloat(InRandom, -720.0f, 720.0f);
		}
		if (InRandom.FRand() < 0.5f)
		{
			Gradient.Center = FVector2f(MakeAwkwardFloat(InRandom, -1.0f, 2.0f), MakeAwkwardFloat(InRandom, -1.0f, 2.0f));
		}
		if (InRandom.FRand() < 0.5f)
		{
			Gradient.Radius = FVector2f(MakeAwkwardFloat(InRandom, 0.0f, 2.0f), MakeAwkwardFloat(InRandom, 0.0f, 2.0f));
		}
		if (InRandom.FRand() < 0.5f)
		{
			Gradient.Scale = MakeAwkwardFloat(InRandom, 0.01f, 8.0f);
		}
		if (InRandom.FRand() < 0.5f)
		{
			Gradient.Offset = MakeAwkwardFloat(InRandom, -2.0f, 2.0f);
		}
		const int32 StopCount = InRandom.RandRange(0, 20);
		const bool bEven = InRandom.FRand() < 0.4f;
		for (int32 StopIndex = 0; StopIndex < StopCount; ++StopIndex)
		{
			const float Position = bEven
				? (StopCount == 1 ? 0.0f : static_cast<float>(StopIndex) / static_cast<float>(StopCount - 1))
				: MakeAwkwardFloat(InRandom, -0.5f, 1.5f);
			const FColor Color(static_cast<uint8>(InRandom.RandRange(0, 255)), static_cast<uint8>(InRandom.RandRange(0, 255)),
				static_cast<uint8>(InRandom.RandRange(0, 255)), static_cast<uint8>(InRandom.FRand() < 0.5f ? 255 : InRandom.RandRange(0, 255)));
			Gradient.Stops.Add(FDreamGradientStop(Position, Color));
		}
		return Gradient;
	}

	// ---------------------------------------------------------------- the row, read as the shader reads it

	float DecodeSrgbForTest(float InEncoded)
	{
		const float Encoded = FMath::Clamp(InEncoded, 0.0f, 1.0f);
		return Encoded <= 0.04045f ? Encoded / 12.92f : FMath::Pow((Encoded + 0.055f) / 1.055f, 2.4f);
	}

	FVector3f OklabToLinearForTest(const FVector3f& InLab)
	{
		const float L = InLab.X + 0.3963377774f * InLab.Y + 0.2158037573f * InLab.Z;
		const float M = InLab.X - 0.1055613458f * InLab.Y - 0.0638541728f * InLab.Z;
		const float S = InLab.X - 0.0894841775f * InLab.Y - 1.2914855480f * InLab.Z;
		return FVector3f(
			4.0767416621f * L * L * L - 3.3077115913f * M * M * M + 0.2309699292f * S * S * S,
			-1.2684380046f * L * L * L + 2.6097574011f * M * M * M - 0.3413193965f * S * S * S,
			-0.0041960863f * L * L * L - 0.7034186147f * M * M * M + 1.7076147010f * S * S * S);
	}

	float FracForTest(float InValue)
	{
		return InValue - FMath::FloorToFloat(InValue);
	}

	FVector4f LerpForTest(const FVector4f& InA, const FVector4f& InB, float InAlpha)
	{
		return InA + (InB - InA) * InAlpha;
	}

	/**
	 * A gradient row, loaded pixel by pixel at the places DreamPaintRows names and evaluated by the reference on
	 * FDreamGradient's comment: what DreamUIPaint.ush does with the row PackRow wrote.
	 */
	FLinearColor EvaluatePackedRow(const TArray<FVector4f>& InRow, const FVector2f& InUV, float InAspect, const FDreamPaintAnimation& InAnimation)
	{
		const FVector4f Header = InRow[DreamPaintRows::GradientHeaderPixel];
		const int32 Type = static_cast<int32>(Header.X);
		const int32 Spread = static_cast<int32>(Header.Y);
		const int32 Space = static_cast<int32>(Header.Z);
		const int32 Count = static_cast<int32>(Header.W);
		if (Type == 0 || Count == 0)
		{
			return FLinearColor::White;
		}
		const FVector4f ShapePixel = InRow[DreamPaintRows::GradientShapePixel];
		const FVector4f CenterPixel = InRow[DreamPaintRows::GradientCenterPixel];
		const float Angle = ShapePixel.X;
		const float Scale = ShapePixel.Y;
		const float Offset = ShapePixel.Z;
		const bool bEllipse = static_cast<int32>(ShapePixel.W) == 0;
		const int32 Size = static_cast<int32>(InRow[DreamPaintRows::GradientSizePixel].X);
		auto Position = [&InRow](int32 InStop) { return InRow[DreamPaintRows::GradientPositionsPixel + InStop / 4][InStop % 4]; };
		auto Color = [&InRow](int32 InStop) { return InRow[DreamPaintRows::GradientColorsPixel + InStop]; };
		auto Finish = [Space](const FVector4f& InMixed)
		{
			FVector3f Rgb = InMixed.W > 0.0f ? FVector3f(InMixed.X / InMixed.W, InMixed.Y / InMixed.W, InMixed.Z / InMixed.W) : FVector3f::ZeroVector;
			if (Space == 0)
			{
				Rgb = FVector3f(DecodeSrgbForTest(Rgb.X), DecodeSrgbForTest(Rgb.Y), DecodeSrgbForTest(Rgb.Z));
			}
			else if (Space == 2)
			{
				Rgb = OklabToLinearForTest(Rgb);
			}
			return FLinearColor(FMath::Max(Rgb.X, 0.0f), FMath::Max(Rgb.Y, 0.0f), FMath::Max(Rgb.Z, 0.0f), InMixed.W);
		};
		const float U = InUV.X;
		const float V = InUV.Y;
		if (Type == 5)
		{
			const int32 Last = Count - 1;
			const float Across = FMath::Clamp(U, 0.0f, 1.0f);
			const float Down = FMath::Clamp(V, 0.0f, 1.0f);
			const FVector4f Top = LerpForTest(Color(0), Color(FMath::Min(1, Last)), Across);
			const FVector4f Bottom = LerpForTest(Color(FMath::Min(2, Last)), Color(FMath::Min(3, Last)), Across);
			return Finish(LerpForTest(Top, Bottom, Down));
		}
		auto Radii = [&](const FVector2f& InCentre, bool bInEllipse)
		{
			FVector2f Result(0.0f, 0.0f);
			if (Size == 4)
			{
				Result = bInEllipse ? FVector2f(CenterPixel.Z * InAspect, CenterPixel.W) : FVector2f(CenterPixel.Z * InAspect, CenterPixel.Z * InAspect);
			}
			else
			{
				const bool bClosest = Size == 2 || Size == 3;
				const bool bCorner = Size == 0 || Size == 2;
				const float Left = FMath::Abs(InCentre.X) * InAspect;
				const float Right = FMath::Abs(1.0f - InCentre.X) * InAspect;
				const float Top = FMath::Abs(InCentre.Y);
				const float Bottom = FMath::Abs(1.0f - InCentre.Y);
				const float SideX = bClosest ? FMath::Min(Left, Right) : FMath::Max(Left, Right);
				const float SideY = bClosest ? FMath::Min(Top, Bottom) : FMath::Max(Top, Bottom);
				if (bInEllipse)
				{
					Result = bCorner ? FVector2f(SideX * 1.41421356f, SideY * 1.41421356f) : FVector2f(SideX, SideY);
				}
				else
				{
					const float Circle = bCorner ? FMath::Sqrt(SideX * SideX + SideY * SideY) : (bClosest ? FMath::Min(SideX, SideY) : FMath::Max(SideX, SideY));
					Result = FVector2f(Circle, Circle);
				}
			}
			return FVector2f(FMath::Max(Result.X, 1e-6f), FMath::Max(Result.Y, 1e-6f));
		};
		const float Turn = FMath::DegreesToRadians(Angle + InAnimation.AngleOffset);
		const FVector2f Centre = FVector2f(CenterPixel.X, CenterPixel.Y) + InAnimation.CenterOffset;
		float T = 0.0f;
		if (Type == 1)
		{
			const FVector2f Middle = FVector2f(0.5f, 0.5f) + InAnimation.CenterOffset;
			const FVector2f Point((U - Middle.X) * InAspect, V - Middle.Y);
			T = (Point.X * FMath::Sin(Turn) - Point.Y * FMath::Cos(Turn)) / FMath::Max(FMath::Abs(InAspect * FMath::Sin(Turn)) + FMath::Abs(FMath::Cos(Turn)), 1e-6f) + 0.5f;
		}
		else
		{
			const FVector2f Point((U - Centre.X) * InAspect, V - Centre.Y);
			if (Type == 2)
			{
				const FVector2f R = Radii(Centre, bEllipse);
				T = FVector2f(Point.X / R.X, Point.Y / R.Y).Size();
			}
			else if (Type == 3)
			{
				T = FracForTest((FMath::RadiansToDegrees(FMath::Atan2(Point.X, -Point.Y)) - (Angle + InAnimation.AngleOffset)) / 360.0f);
			}
			else
			{
				const FVector2f R = Radii(Centre, true);
				const FVector2f Q(Point.X * FMath::Cos(Turn) + Point.Y * FMath::Sin(Turn), -Point.X * FMath::Sin(Turn) + Point.Y * FMath::Cos(Turn));
				T = FMath::Abs(Q.X) / R.X + FMath::Abs(Q.Y) / R.Y;
			}
		}
		const float S = FMath::Max(Scale * InAnimation.ScaleMultiplier, 1e-4f);
		const float O = Offset + InAnimation.Phase;
		T = Type == 1 ? (T - 0.5f) / S + 0.5f - O : T / S - O;
		const float First = Position(0);
		const float Span = Position(Count - 1) - First;
		if (Span > 0.0f && Spread == 1)
		{
			T = First + FracForTest((T - First) / Span) * Span;
		}
		else if (Span > 0.0f && Spread == 2)
		{
			const float Cycle = 1.0f - FMath::Abs(FracForTest((T - First) / Span * 0.5f) * 2.0f - 1.0f);
			T = First + Cycle * Span;
		}
		if (Count == 1 || !(T > Position(0)))
		{
			return Finish(Color(0));
		}
		if (T >= Position(Count - 1))
		{
			return Finish(Color(Count - 1));
		}
		for (int32 StopIndex = 0; StopIndex < Count - 1; ++StopIndex)
		{
			if (T < Position(StopIndex + 1))
			{
				return Finish(LerpForTest(Color(StopIndex), Color(StopIndex + 1), (T - Position(StopIndex)) / FMath::Max(Position(StopIndex + 1) - Position(StopIndex), 1e-9f)));
			}
		}
		return Finish(Color(Count - 1));
	}

	bool IsNearlyColor(const FLinearColor& InA, const FLinearColor& InB, float InTolerance)
	{
		auto Near = [InTolerance](float InX, float InY) { return FMath::Abs(InX - InY) <= InTolerance * (1.0f + FMath::Abs(InY)); };
		return Near(InA.R, InB.R) && Near(InA.G, InB.G) && Near(InA.B, InB.B) && Near(InA.A, InB.A);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintCssReadsTest,
	"DreamGUI.Text.Paint.CssIsReadAsChromeReadsItWithStopsWhereTheyWereWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintCssReadsTest::RunTest(const FString& Parameters)
{
	auto Read = [this](const TCHAR* InCss) -> FDreamGradient
	{
		FDreamGradient Gradient;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("'%s' reads"), InCss), FDreamGradient::ParseCss(InCss, Gradient, &Error)))
		{
			AddInfo(Error);
		}
		return Gradient;
	};

	const FDreamGradient ToRight = Read(TEXT("linear-gradient(to right, red, blue)"));
	TestTrue(TEXT("a linear gradient"), ToRight.Type == EDreamPaintType::Linear && ToRight.Spread == EDreamPaintSpread::Pad);
	TestEqual(TEXT("'to right' is 90deg"), ToRight.Angle, 90.0f);
	TestTrue(TEXT("CSS's default space is sRGB"), ToRight.Interpolation == EDreamPaintInterpolation::SRGB);
	if (TestEqual(TEXT("two stops"), ToRight.Stops.Num(), 2))
	{
		TestEqual(TEXT("red"), ToRight.Stops[0].Color, FColor(255, 0, 0, 255));
		TestEqual(TEXT("placed at 0"), ToRight.Stops[0].Position, 0.0f);
		TestEqual(TEXT("blue"), ToRight.Stops[1].Color, FColor(0, 0, 255, 255));
		TestEqual(TEXT("placed at 1"), ToRight.Stops[1].Position, 1.0f);
	}
	TestEqual(TEXT("no direction is 180deg, top to bottom"), Read(TEXT("linear-gradient(red, blue)")).Angle, 180.0f);
	TestEqual(TEXT("'to top' is 0deg"), Read(TEXT("linear-gradient(to top, red, blue)")).Angle, 0.0f);
	TestEqual(TEXT("'to left' is 270deg"), Read(TEXT("linear-gradient(to left, red, blue)")).Angle, 270.0f);
	TestEqual(TEXT("a corner is the square's: 'to top left' is 315deg"), Read(TEXT("linear-gradient(to top left, red, blue)")).Angle, 315.0f);
	TestEqual(TEXT("and the keywords go either way round"), Read(TEXT("linear-gradient(to right bottom, red, blue)")).Angle, 135.0f);
	TestEqual(TEXT("a quarter turn is 90deg"), Read(TEXT("linear-gradient(0.25turn, red, blue)")).Angle, 90.0f);
	TestEqual(TEXT("100grad is 90deg"), Read(TEXT("linear-gradient(100grad, red, blue)")).Angle, 90.0f, 1e-4f);
	TestEqual(TEXT("pi radians is 180deg"), Read(TEXT("linear-gradient(3.14159265rad, red, blue)")).Angle, 180.0f, 1e-4f);
	TestEqual(TEXT("a bare 0 is an angle"), Read(TEXT("linear-gradient(0, red, blue)")).Angle, 0.0f);

	const FDreamGradient Stripes = Read(TEXT("repeating-linear-gradient(45deg, #000 0 10%, #fff 10% 20%)"));
	TestTrue(TEXT("repeating- repeats"), Stripes.Spread == EDreamPaintSpread::Repeat);
	if (TestEqual(TEXT("two positions make two stops of one colour"), Stripes.Stops.Num(), 4))
	{
		TestEqual(TEXT("the band starts at 0"), Stripes.Stops[0].Position, 0.0f);
		TestEqual(TEXT("and ends at 10%"), Stripes.Stops[1].Position, 0.1f);
		TestEqual(TEXT("white starts where black ends"), Stripes.Stops[2].Position, 0.1f);
		TestEqual(TEXT("#fff is white"), Stripes.Stops[3].Color, FColor::White);
	}

	const FDreamGradient Circle = Read(TEXT("radial-gradient(circle closest-side at 30% 40%, red, blue)"));
	TestTrue(TEXT("a circle"), Circle.Type == EDreamPaintType::Radial && Circle.Shape == EDreamPaintRadialShape::Circle);
	TestTrue(TEXT("its size keyword"), Circle.Size == EDreamPaintRadialSize::ClosestSide);
	TestEqual(TEXT("its centre"), Circle.Center, FVector2f(0.3f, 0.4f));
	const FDreamGradient Ellipse = Read(TEXT("radial-gradient(ellipse 50% 25% at left top, red, blue)"));
	TestTrue(TEXT("percentages are an explicit size"), Ellipse.Size == EDreamPaintRadialSize::Explicit);
	TestEqual(TEXT("of the width, then the height"), Ellipse.Radius, FVector2f(0.5f, 0.25f));
	TestEqual(TEXT("'left top' is the corner"), Ellipse.Center, FVector2f(0.0f, 0.0f));
	TestEqual(TEXT("an edge and an offset from it, both ways"),
		Read(TEXT("radial-gradient(farthest-side at right 10% bottom 20%, red, blue)")).Center, FVector2f(0.9f, 0.8f));
	TestEqual(TEXT("'top center' is the keywords the other way round"),
		Read(TEXT("radial-gradient(at top center, red, blue)")).Center, FVector2f(0.5f, 0.0f));

	const FDreamGradient Conic = Read(TEXT("conic-gradient(from 0.25turn at 25% 75% in oklab, red, blue)"));
	TestTrue(TEXT("a conic gradient in Oklab"), Conic.Type == EDreamPaintType::Conic && Conic.Interpolation == EDreamPaintInterpolation::Oklab);
	TestEqual(TEXT("from a quarter turn"), Conic.Angle, 90.0f);
	TestEqual(TEXT("at its centre"), Conic.Center, FVector2f(0.25f, 0.75f));
	TestEqual(TEXT("a conic gradient with no 'from' starts at 0, as CSS's"), Read(TEXT("conic-gradient(red, blue)")).Angle, 0.0f);

	const FDreamGradient Colours = Read(TEXT("linear-gradient(in srgb-linear, rgb(255 0 0 / 50%), rgba(0, 0, 255, 0.25), GREEN, Transparent)"));
	TestTrue(TEXT("srgb-linear is linear light"), Colours.Interpolation == EDreamPaintInterpolation::Linear);
	if (TestEqual(TEXT("four colours"), Colours.Stops.Num(), 4))
	{
		TestEqual(TEXT("rgb() with a percentage alpha"), Colours.Stops[0].Color, FColor(255, 0, 0, 128));
		TestEqual(TEXT("rgba() with CSS's 0..1 alpha"), Colours.Stops[1].Color, FColor(0, 0, 255, 64));
		TestEqual(TEXT("green is CSS's #008000, in any case"), Colours.Stops[2].Color, FColor(0, 128, 0, 255));
		TestEqual(TEXT("transparent is transparent black"), Colours.Stops[3].Color, FColor(0, 0, 0, 0));
	}

	// Placing as CSS places: the first at 0, the last at 1, a run between two placed stops evenly between them.
	const FDreamGradient Placed = Read(TEXT("linear-gradient(red, lime, blue 80%, yellow, white)"));
	if (TestEqual(TEXT("five stops"), Placed.Stops.Num(), 5))
	{
		TestEqual(TEXT("lime halfway to blue"), Placed.Stops[1].Position, 0.4f, 1e-6f);
		TestEqual(TEXT("blue where written"), Placed.Stops[2].Position, 0.8f);
		TestEqual(TEXT("yellow halfway to the end"), Placed.Stops[3].Position, 0.9f, 1e-6f);
		TestEqual(TEXT("white at the end"), Placed.Stops[4].Position, 1.0f);
	}
	const FDreamGradient OutOfOrder = Read(TEXT("linear-gradient(red 50%, lime, blue 20%)"));
	if (TestEqual(TEXT("three stops"), OutOfOrder.Stops.Num(), 3))
	{
		TestEqual(TEXT("a written position is kept as written, out of order too"), OutOfOrder.Stops[2].Position, 0.2f);
		TestEqual(TEXT("and one placed between counts the later as at least the earlier, as CSS does"), OutOfOrder.Stops[1].Position, 0.5f);
	}

	const FDreamGradient Extended = Read(TEXT("reflecting-diamond-gradient(30deg circle 40% at 25% 50% scale 2 offset 0.5, red, blue)"));
	TestTrue(TEXT("diamond-gradient and reflecting- are read"), Extended.Type == EDreamPaintType::Diamond && Extended.Spread == EDreamPaintSpread::Reflect);
	TestEqual(TEXT("its turn"), Extended.Angle, 30.0f);
	TestTrue(TEXT("a circle of an explicit size"), Extended.Shape == EDreamPaintRadialShape::Circle && Extended.Size == EDreamPaintRadialSize::Explicit);
	TestEqual(TEXT("a circle's one radius, of the width"), Extended.Radius.X, 0.4f);
	TestEqual(TEXT("its scale"), Extended.Scale, 2.0f);
	TestEqual(TEXT("an offset written as a fraction"), Extended.Offset, 0.5f);
	TestTrue(TEXT("'none' is no paint"), Read(TEXT("none")).Type == EDreamPaintType::None);
	TestTrue(TEXT("spaces around everything are CSS's to allow"),
		Read(TEXT("  linear-gradient( 90deg ,red ,  blue )  ")) == Read(TEXT("linear-gradient(90deg, red, blue)")));
	TestTrue(TEXT("and none at all is what a rich text's tag can hold"),
		Read(TEXT("linear-gradient(90deg,red,blue)")) == Read(TEXT("linear-gradient(90deg, red, blue)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintCssRoundTripTest,
	"DreamGUI.Text.Paint.CssIsPrintedPlainAndReadsBackFieldForField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintCssRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;

	// What CSS can say is written as CSS, as plainly as it can be: positions CSS would place are left out.
	FDreamGradient Gold;
	Gold.Stops = { FDreamGradientStop(0.0f, FColor(0xFF, 0xF3, 0xB0)), FDreamGradientStop(0.55f, FColor(0xE8, 0xB6, 0x4A)),
		FDreamGradientStop(1.0f, FColor(0x9C, 0x6A, 0x12)) };
	TestEqual(TEXT("the design's gold"), Gold.ToCss(), FString(TEXT("linear-gradient(#FFF3B0, #E8B64A 55%, #9C6A12)")));
	TestEqual(TEXT("an angle"), MakeTwoStops(FColor::Red, FColor::Blue).ToCss(), FString(TEXT("linear-gradient(90deg, #FF0000, #0000FF)")));

	FDreamGradient Circle = MakeTwoStops(FColor::Red, FColor::Blue, EDreamPaintInterpolation::Oklab);
	Circle.Type = EDreamPaintType::Radial;
	Circle.Angle = 180.0f;
	Circle.Shape = EDreamPaintRadialShape::Circle;
	Circle.Size = EDreamPaintRadialSize::ClosestSide;
	Circle.Center = FVector2f(0.3f, 0.4f);
	TestEqual(TEXT("a radial gradient"), Circle.ToCss(), FString(TEXT("radial-gradient(circle closest-side at 30% 40% in oklab, #FF0000, #0000FF)")));

	FDreamGradient Stripes;
	Stripes.Angle = 45.0f;
	Stripes.Spread = EDreamPaintSpread::Repeat;
	Stripes.Stops = { FDreamGradientStop(0.0f, FColor::Black), FDreamGradientStop(0.1f, FColor::Black),
		FDreamGradientStop(0.1f, FColor::White), FDreamGradientStop(0.2f, FColor::White) };
	TestEqual(TEXT("hard stripes"), Stripes.ToCss(), FString(TEXT("repeating-linear-gradient(45deg, #000000, #000000 10%, #FFFFFF 10%, #FFFFFF 20%)")));

	FDreamGradient Conic = MakeTwoStops(FColor::Red, FColor::Blue);
	Conic.Type = EDreamPaintType::Conic;
	TestEqual(TEXT("a conic gradient's angle is its 'from'"), Conic.ToCss(), FString(TEXT("conic-gradient(from 90deg, #FF0000, #0000FF)")));

	FDreamGradient Diamond = MakeTwoStops(FColor::Red, FColor(0, 0, 255, 128));
	Diamond.Type = EDreamPaintType::Diamond;
	Diamond.Spread = EDreamPaintSpread::Reflect;
	Diamond.Angle = 30.0f;
	Diamond.Scale = 2.0f;
	Diamond.Offset = 0.25f;
	TestEqual(TEXT("what CSS cannot say, in DreamGUI's words"), Diamond.ToCss(),
		FString(TEXT("reflecting-diamond-gradient(30deg scale 2 offset 25%, #FF0000, #0000FF80)")));
	TestEqual(TEXT("a gradient with nothing in it"), FDreamGradient().ToCss(), FString(TEXT("linear-gradient()")));
	FDreamGradient Nothing;
	Nothing.Type = EDreamPaintType::None;
	TestEqual(TEXT("no paint"), Nothing.ToCss(), FString(TEXT("none")));
	FDreamGradient Corners;
	if (TestTrue(TEXT("corners-gradient reads"), FDreamGradient::ParseCss(TEXT("corners-gradient(red, lime, blue, white)"), Corners)))
	{
		TestEqual(TEXT("and prints back as it was written"), Corners.ToCss(), FString(TEXT("corners-gradient(#FF0000, #00FF00, #0000FF, #FFFFFF)")));
	}

	// The invariant over gradients of every shape: what is printed reads back as the very gradient, every field of it --
	// the ones the type does not use included, since the .dui keeps a gradient as this text and nothing else.
	FRandomStream Random(0x5EED);
	int32 Failures = 0;
	for (int32 Round = 0; Round < 3000 && Failures < 5; ++Round)
	{
		const FDreamGradient Original = MakeRandomGradient(Random);
		const FString Css = Original.ToCss();
		FDreamGradient ReadBack;
		FString Error;
		const bool bRead = FDreamGradient::ParseCss(Css, ReadBack, &Error);
		if (!bRead || !(ReadBack == Original))
		{
			++Failures;
			AddError(FString::Printf(TEXT("round %d does not read back: %s%s%s"), Round, *Css, bRead ? TEXT("") : TEXT(" -- "), *Error));
		}
	}
	TestEqual(TEXT("every random gradient read back as itself"), Failures, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintCssMalformedTest,
	"DreamGUI.Text.Paint.MalformedCssIsRefusedAndLeavesTheGradientAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintCssMalformedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	const TCHAR* Malformed[] =
	{
		TEXT(""),
		TEXT("   "),
		TEXT("linear-gradient"),
		TEXT("linear-gradient("),
		TEXT("linear-gradient(red, blue"),
		TEXT("linear-gradient(red, blue))"),
		TEXT("linear-gradient(red, blue) red"),
		TEXT("linear-gradient(red,, blue)"),
		TEXT("linear-gradient(red, blue,)"),
		TEXT("linear-gradient(foo, red)"),
		TEXT("linear-gradient(90deg red, blue)"),
		TEXT("linear-gradient(red 10px, blue)"),
		TEXT("linear-gradient(red, 30%, blue)"),
		TEXT("linear-gradient(red 10% 20% 30%, blue)"),
		TEXT("linear-gradient(90deg 45deg, red)"),
		TEXT("linear-gradient(90, red)"),
		TEXT("linear-gradient(1e999deg, red)"),
		TEXT("linear-gradient(to middle, red)"),
		TEXT("linear-gradient(to top bottom, red)"),
		TEXT("linear-gradient(in hsl, red, blue)"),
		TEXT("linear-gradient(scale, red)"),
		TEXT("linear-gradient(at, red)"),
		TEXT("linear-gradient (red, blue)"),
		TEXT("spiral-gradient(red, blue)"),
		TEXT("linear-gradient(#ggg, blue)"),
		TEXT("linear-gradient(#12345, blue)"),
		TEXT("linear-gradient(rgb(1,2), blue)"),
		TEXT("linear-gradient(rgb(1 2 3 / 4 5), blue)"),
		TEXT("linear-gradient(hsl(0 100% 50%), blue)"),
		TEXT("radial-gradient(circle circle, red)"),
		TEXT("radial-gradient(closest-side 30%, red)"),
		TEXT("radial-gradient(at left right, red)"),
		TEXT("radial-gradient(30% 40% radius 10%, red)"),
	};
	const FDreamGradient Sentinel = MakeSentinel();
	for (const TCHAR* Css : Malformed)
	{
		FDreamGradient Output = Sentinel;
		FString Error;
		TestFalse(FString::Printf(TEXT("'%s' is refused"), Css), FDreamGradient::ParseCss(Css, Output, &Error));
		TestTrue(FString::Printf(TEXT("'%s' leaves the gradient untouched"), Css), Output == Sentinel);
		TestFalse(FString::Printf(TEXT("'%s' says why"), Css), Error.IsEmpty());
	}
	// Nothing reading can be handed is a crash: a bracket a few thousand deep, a run of commas, a number of every length.
	FDreamGradient Output = Sentinel;
	TestFalse(TEXT("an unclosed pile of brackets is refused"), FDreamGradient::ParseCss(TEXT("linear-gradient(") + FString::ChrN(5000, TEXT('(')), Output));
	TestFalse(TEXT("so are commas alone"), FDreamGradient::ParseCss(TEXT("linear-gradient(") + FString::ChrN(5000, TEXT(',')) + TEXT(")"), Output));
	TestFalse(TEXT("and a number longer than any float"), FDreamGradient::ParseCss(TEXT("linear-gradient(") + FString::ChrN(400, TEXT('9')) + TEXT("deg, red)"), Output));
	TestTrue(TEXT("none of which touched the gradient"), Output == Sentinel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintDuiShortFormTest,
	"DreamGUI.Text.Paint.TheDuiShortFormOfAGradientIsItsCssInAString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintDuiShortFormTest::RunTest(const FString& Parameters)
{
	const FProperty* GradientProperty = FDreamTextPaint::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Gradient));
	const FProperty* PaintProperty = FDreamTextStyle::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDreamTextStyle, FacePaint));
	if (!TestNotNull(TEXT("a paint's gradient property"), GradientProperty) || !TestNotNull(TEXT("a style's face paint property"), PaintProperty))
	{
		return false;
	}
	TestTrue(TEXT("a gradient has a short form, so the write-back keeps its stops"), DreamUIValueFormat::HasShortForm(GradientProperty));
	TestTrue(TEXT("written as a string"), DreamUIValueFormat::GetShortFormLiteralKind(GradientProperty) == EDreamUIValueKind::String);
	TestEqual(TEXT("which is no tuple"), DreamUIValueFormat::GetExpectedTupleArity(GradientProperty), (int32)INDEX_NONE);
	TestFalse(TEXT("a paint has none: its switch and its preset are leaves of their own"), DreamUIValueFormat::HasShortForm(PaintProperty));

	FDreamTextPaint Paint;
	Paint.bEnabled = true;
	FDreamGradient::ParseCss(TEXT("linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)"), Paint.Gradient);
	FString Printed;
	if (!TestTrue(TEXT("it prints"), DreamUIValueFormat::Print(GradientProperty, GradientProperty->ContainerPtrToValuePtr<void>(&Paint), Printed)))
	{
		return false;
	}
	TestEqual(TEXT("as the design's spelling, quoted"), Printed, FString(TEXT("\"linear-gradient(#FFF3B0, #E8B64A 55%, #9C6A12)\"")));

	// Through the lexer, as the line a file holds, and back into a value.
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const FString Source = FString::Printf(TEXT("Widget R {\n    TextStyle.FacePaint.Gradient = %s\n}"), *Printed);
	if (!TestTrue(TEXT("the line lexes"), FDreamUISourceFile::Parse(Source, TEXT("Gradient.dui"), Ast, Diagnostics))
		|| !TestEqual(TEXT("to one property"), Ast.Root.Properties.Num(), 1))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}
	const FDreamUIValue& Value = Ast.Root.Properties[0].Value;
	TestTrue(TEXT("whose value is a string"), Value.Kind == EDreamUIValueKind::String);
	FDreamTextPaint ReadBack;
	TestTrue(TEXT("which reads into a gradient"), DreamUIValueFormat::Parse(GradientProperty, Value, GradientProperty->ContainerPtrToValuePtr<void>(&ReadBack)));
	TestTrue(TEXT("the very gradient printed"), ReadBack.Gradient == Paint.Gradient);

	// A refusal writes nothing; a value of another kind is refused.
	FDreamUIValue Broken;
	Broken.Kind = EDreamUIValueKind::String;
	Broken.Raw = TEXT("linear-gradient(red,");
	TestFalse(TEXT("broken CSS is refused"), DreamUIValueFormat::Parse(GradientProperty, Broken, GradientProperty->ContainerPtrToValuePtr<void>(&ReadBack)));
	TestTrue(TEXT("and leaves the gradient as it was"), ReadBack.Gradient == Paint.Gradient);
	Broken.Kind = EDreamUIValueKind::Tuple;
	Broken.Raw = TEXT("(1, 2)");
	TestFalse(TEXT("a tuple is not a gradient"), DreamUIValueFormat::Parse(GradientProperty, Broken, GradientProperty->ContainerPtrToValuePtr<void>(&ReadBack)));

	// A number with no spelling makes the value unrepresentable, which the write-back reports instead of writing.
	FDreamTextPaint NotFinite = Paint;
	NotFinite.Gradient.Angle = std::numeric_limits<float>::quiet_NaN();
	FString Untouched = TEXT("kept");
	bool bUnrepresentable = false;
	TestFalse(TEXT("a gradient with a NaN does not print"),
		DreamUIValueFormat::Print(GradientProperty, GradientProperty->ContainerPtrToValuePtr<void>(&NotFinite), Untouched, &bUnrepresentable));
	TestTrue(TEXT("and says it is the value that cannot be spelled"), bUnrepresentable);
	TestEqual(TEXT("the text is left alone"), Untouched, FString(TEXT("kept")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintLinearTest,
	"DreamGUI.Text.Paint.ALinearGradientFollowsCssAnglesAndMagicCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintLinearTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	FDreamGradient Ramp = MakeRamp(EDreamPaintType::Linear);
	// 180deg, the default: top to bottom, v from the box's top.
	TestEqual(TEXT("180deg runs down: t is v"), Ramp.Evaluate(FVector2f(0.3f, 0.25f)).R, 0.25f, 1e-5f);
	Ramp.Angle = 90.0f;
	TestEqual(TEXT("90deg runs right: t is u"), Ramp.Evaluate(FVector2f(0.7f, 0.2f)).R, 0.7f, 1e-5f);
	Ramp.Angle = 0.0f;
	TestEqual(TEXT("0deg runs up"), Ramp.Evaluate(FVector2f(0.3f, 0.25f)).R, 0.75f, 1e-5f);
	Ramp.Angle = 270.0f;
	TestEqual(TEXT("270deg runs left"), Ramp.Evaluate(FVector2f(0.7f, 0.2f)).R, 0.3f, 1e-5f);

	// 45deg on a box twice as wide as tall. CSS's gradient line is |W sin a| + |H cos a| long, so the corners it points
	// from and to get exactly the end colours (the "magic corners"), and the other two corners fall at a third and two.
	Ramp.Angle = 45.0f;
	const float Aspect = 2.0f;
	TestEqual(TEXT("the top right corner is the end"), Ramp.Evaluate(FVector2f(1.0f, 0.0f), Aspect).R, 1.0f, 1e-5f);
	TestEqual(TEXT("the bottom left corner is the start"), Ramp.Evaluate(FVector2f(0.0f, 1.0f), Aspect).R, 0.0f, 1e-5f);
	TestEqual(TEXT("the top left corner is a third of the way"), Ramp.Evaluate(FVector2f(0.0f, 0.0f), Aspect).R, 1.0f / 3.0f, 1e-5f);
	TestEqual(TEXT("the bottom right corner is two thirds"), Ramp.Evaluate(FVector2f(1.0f, 1.0f), Aspect).R, 2.0f / 3.0f, 1e-5f);
	TestEqual(TEXT("the middle is halfway"), Ramp.Evaluate(FVector2f(0.5f, 0.5f), Aspect).R, 0.5f, 1e-5f);
	// The same angle in a unit square -- what a line, glyph or run box is measured in.
	TestEqual(TEXT("in a square the far corner is the end too"), Ramp.Evaluate(FVector2f(1.0f, 0.0f), 1.0f).R, 1.0f, 1e-5f);
	TestEqual(TEXT("and the near corner halfway"), Ramp.Evaluate(FVector2f(0.0f, 0.0f), 1.0f).R, 0.5f, 1e-5f);
	// Past the ends the end colours carry on (Pad).
	Ramp.Angle = 90.0f;
	TestEqual(TEXT("before the start, the start's colour"), Ramp.Evaluate(FVector2f(-0.5f, 0.5f)).R, 0.0f, 1e-6f);
	TestEqual(TEXT("past the end, the end's"), Ramp.Evaluate(FVector2f(1.5f, 0.5f)).R, 1.0f, 1e-6f);

	FDreamGradient NotPainting;
	NotPainting.Type = EDreamPaintType::None;
	NotPainting.Stops = Ramp.Stops;
	TestEqual(TEXT("a gradient that does not paint leaves a colour as it is: white"), NotPainting.Evaluate(FVector2f(0.5f, 0.5f)), FLinearColor::White);
	TestEqual(TEXT("and so does one with no stops"), FDreamGradient().Evaluate(FVector2f(0.5f, 0.5f)), FLinearColor::White);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintRadialTest,
	"DreamGUI.Text.Paint.ARadialGradientMeetsTheSidesAndCornersCssNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintRadialTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	FDreamGradient Ramp = MakeRamp(EDreamPaintType::Radial);
	// The default: an ellipse through the farthest corner, at the centre.
	TestEqual(TEXT("farthest-corner passes through the corner"), Ramp.Evaluate(FVector2f(1.0f, 1.0f)).R, 1.0f, 1e-5f);
	TestEqual(TEXT("a quarter of the width out is a quarter over a corner's radius"), Ramp.Evaluate(FVector2f(0.75f, 0.5f)).R, 0.25f / (0.5f * UE_SQRT_2), 1e-5f);

	// A centre a quarter in from the left, on a box twice as wide as tall: the left side is half a height away, the right
	// one and a half, top and bottom half each.
	const float Aspect = 2.0f;
	Ramp.Center = FVector2f(0.25f, 0.5f);
	Ramp.Size = EDreamPaintRadialSize::ClosestSide;
	TestEqual(TEXT("closest-side meets the nearer side"), Ramp.Evaluate(FVector2f(0.5f, 0.5f), Aspect).R, 1.0f, 1e-5f);
	TestEqual(TEXT("in each direction (an ellipse)"), Ramp.Evaluate(FVector2f(0.25f, 0.75f), Aspect).R, 0.5f, 1e-5f);
	Ramp.Size = EDreamPaintRadialSize::FarthestSide;
	TestEqual(TEXT("farthest-side meets the farther side"), Ramp.Evaluate(FVector2f(1.0f, 0.5f), Aspect).R, 1.0f, 1e-5f);
	Ramp.Shape = EDreamPaintRadialShape::Circle;
	Ramp.Size = EDreamPaintRadialSize::FarthestCorner;
	TestEqual(TEXT("a circle through the farthest corner"), Ramp.Evaluate(FVector2f(1.0f, 1.0f), Aspect).R, 1.0f, 1e-5f);
	TestEqual(TEXT("is round: straight down is measured on the same radius"), Ramp.Evaluate(FVector2f(0.25f, 1.0f), Aspect).R,
		0.5f / FMath::Sqrt(1.5f * 1.5f + 0.5f * 0.5f), 1e-5f);
	Ramp.Size = EDreamPaintRadialSize::ClosestCorner;
	TestEqual(TEXT("a circle through the nearest corner"), Ramp.Evaluate(FVector2f(0.0f, 0.0f), Aspect).R, 1.0f, 1e-5f);
	Ramp.Size = EDreamPaintRadialSize::ClosestSide;
	TestEqual(TEXT("a circle to the nearest side"), Ramp.Evaluate(FVector2f(0.5f, 0.5f), Aspect).R, 1.0f, 1e-5f);

	// Explicit radii are fractions of the width and of the height; a circle's is of the width.
	Ramp.Center = FVector2f(0.5f, 0.5f);
	Ramp.Size = EDreamPaintRadialSize::Explicit;
	Ramp.Radius = FVector2f(0.25f, 0.5f);
	Ramp.Shape = EDreamPaintRadialShape::Ellipse;
	TestEqual(TEXT("an explicit ellipse, across"), Ramp.Evaluate(FVector2f(0.75f, 0.5f), Aspect).R, 1.0f, 1e-5f);
	TestEqual(TEXT("and down"), Ramp.Evaluate(FVector2f(0.5f, 1.0f), Aspect).R, 1.0f, 1e-5f);
	Ramp.Shape = EDreamPaintRadialShape::Circle;
	TestEqual(TEXT("an explicit circle's radius is of the width"), Ramp.Evaluate(FVector2f(0.5f, 0.75f), Aspect).R, 0.5f, 1e-5f);

	// A centre outside the box measures to its sides as distances, as CSS does.
	Ramp.Shape = EDreamPaintRadialShape::Ellipse;
	Ramp.Size = EDreamPaintRadialSize::ClosestSide;
	Ramp.Center = FVector2f(-0.25f, 0.5f);
	TestEqual(TEXT("a centre left of the box meets the left side"), Ramp.Evaluate(FVector2f(0.0f, 0.5f)).R, 1.0f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintConicTest,
	"DreamGUI.Text.Paint.AConicGradientTurnsClockwiseFromItsFromAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintConicTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	FDreamGradient Ramp = MakeRamp(EDreamPaintType::Conic);
	Ramp.Angle = 0.0f;
	// From 12 o'clock, clockwise: right is a quarter of the way round, down half, left three quarters.
	TestEqual(TEXT("right of the centre is a quarter turn"), Ramp.Evaluate(FVector2f(1.0f, 0.5f)).R, 0.25f, 1e-5f);
	TestEqual(TEXT("below it half"), Ramp.Evaluate(FVector2f(0.5f, 1.0f)).R, 0.5f, 1e-5f);
	TestEqual(TEXT("left of it three quarters"), Ramp.Evaluate(FVector2f(0.0f, 0.5f)).R, 0.75f, 1e-5f);
	// `from 90deg` starts at 3 o'clock.
	Ramp.Angle = 90.0f;
	TestEqual(TEXT("from 90deg, down and right is an eighth"), Ramp.Evaluate(FVector2f(0.75f, 0.75f)).R, 0.125f, 1e-5f);
	TestEqual(TEXT("down is a quarter"), Ramp.Evaluate(FVector2f(0.5f, 1.0f)).R, 0.25f, 1e-5f);
	TestEqual(TEXT("left half"), Ramp.Evaluate(FVector2f(0.0f, 0.5f)).R, 0.5f, 1e-5f);
	TestEqual(TEXT("up three quarters"), Ramp.Evaluate(FVector2f(0.5f, 0.0f)).R, 0.75f, 1e-5f);
	// Angles are measured in the box as it is: on a wide box the corner is less than 45 degrees off the horizontal.
	Ramp.Angle = 0.0f;
	TestEqual(TEXT("on a box twice as wide, the top right corner is atan(2) round"), Ramp.Evaluate(FVector2f(1.0f, 0.0f), 2.0f).R,
		FMath::RadiansToDegrees(FMath::Atan2(1.0f, 0.5f)) / 360.0f, 1e-4f);
	FDreamGradient Read;
	if (TestTrue(TEXT("conic CSS reads"), FDreamGradient::ParseCss(TEXT("conic-gradient(from 90deg in srgb-linear, black, white)"), Read)))
	{
		TestEqual(TEXT("and evaluates as the struct does"), Read.Evaluate(FVector2f(0.5f, 1.0f)).R, 0.25f, 1e-5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintDiamondCornersTest,
	"DreamGUI.Text.Paint.DiamondAndCornersGradientsEvaluateAsTheirDefinitionSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintDiamondCornersTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	FDreamGradient Diamond = MakeRamp(EDreamPaintType::Diamond);
	// |x| / Rx + |y| / Ry with the radii of an ellipse through the farthest corner.
	const float Radius = 0.5f * UE_SQRT_2;
	TestEqual(TEXT("the centre is the start"), Diamond.Evaluate(FVector2f(0.5f, 0.5f)).R, 0.0f, 1e-6f);
	TestEqual(TEXT("halfway out across"), Diamond.Evaluate(FVector2f(1.0f, 0.5f)).R, 0.5f / Radius, 1e-5f);
	TestEqual(TEXT("the same on the diagonal: a diamond's edge"), Diamond.Evaluate(FVector2f(0.75f, 0.75f)).R, 0.5f / Radius, 1e-5f);
	// Turned 45 degrees clockwise, radii half and a quarter of a height: a point on its turned x axis, and one on its y.
	Diamond.Angle = 45.0f;
	Diamond.Size = EDreamPaintRadialSize::Explicit;
	Diamond.Radius = FVector2f(0.5f, 0.25f);
	const float Diagonal = 0.25f * 0.5f * UE_SQRT_2;
	TestEqual(TEXT("its long axis is turned clockwise, towards the bottom right"),
		Diamond.Evaluate(FVector2f(0.5f + Diagonal, 0.5f + Diagonal)).R, 0.5f, 1e-5f);
	TestEqual(TEXT("and its short one towards the bottom left"),
		Diamond.Evaluate(FVector2f(0.5f - Diagonal * 0.5f, 0.5f + Diagonal * 0.5f)).R, 0.5f, 1e-5f);

	FDreamGradient Corners;
	Corners.Type = EDreamPaintType::Corners;
	Corners.Interpolation = EDreamPaintInterpolation::Linear;
	Corners.Stops = { FDreamGradientStop(0.0f, FColor::Red), FDreamGradientStop(0.0f, FColor::Green),
		FDreamGradientStop(0.0f, FColor::Blue), FDreamGradientStop(0.0f, FColor::White) };
	TestTrue(TEXT("top left is the first stop"), IsNearlyColor(Corners.Evaluate(FVector2f(0.0f, 0.0f)), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), 1e-5f));
	TestTrue(TEXT("top right the second"), IsNearlyColor(Corners.Evaluate(FVector2f(1.0f, 0.0f)), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f), 1e-5f));
	TestTrue(TEXT("bottom left the third"), IsNearlyColor(Corners.Evaluate(FVector2f(0.0f, 1.0f)), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f), 1e-5f));
	TestTrue(TEXT("bottom right the fourth"), IsNearlyColor(Corners.Evaluate(FVector2f(1.0f, 1.0f)), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 1e-5f));
	TestTrue(TEXT("the middle is all four mixed"), IsNearlyColor(Corners.Evaluate(FVector2f(0.5f, 0.5f)), FLinearColor(0.5f, 0.5f, 0.5f, 1.0f), 1e-5f));
	TestTrue(TEXT("past the box the edges carry on"), IsNearlyColor(Corners.Evaluate(FVector2f(-1.0f, 2.0f)), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f), 1e-5f));
	Corners.Stops.SetNum(2);
	TestTrue(TEXT("the last stop stands in for missing corners"), IsNearlyColor(Corners.Evaluate(FVector2f(1.0f, 1.0f)), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f), 1e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintHardStopsTest,
	"DreamGUI.Text.Paint.AHardStopBelongsToTheSegmentAfterIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintHardStopsTest::RunTest(const FString& Parameters)
{
	FDreamGradient Halves;
	if (!TestTrue(TEXT("reads"), FDreamGradient::ParseCss(TEXT("linear-gradient(90deg, red 0%, red 50%, blue 50%, blue 100%)"), Halves)))
	{
		return false;
	}
	using namespace DreamTextPaintTestLocal;
	const FLinearColor Red(1.0f, 0.0f, 0.0f, 1.0f);
	const FLinearColor Blue(0.0f, 0.0f, 1.0f, 1.0f);
	TestTrue(TEXT("just before the edge, red"), IsNearlyColor(Halves.Evaluate(FVector2f(0.4999f, 0.5f)), Red, 1e-5f));
	TestTrue(TEXT("on the edge itself, the later colour"), IsNearlyColor(Halves.Evaluate(FVector2f(0.5f, 0.5f)), Blue, 1e-5f));
	TestTrue(TEXT("just after, blue"), IsNearlyColor(Halves.Evaluate(FVector2f(0.5001f, 0.5f)), Blue, 1e-5f));
	// A position before the one ahead of it is taken as that one, which is a hard edge too.
	FDreamGradient Backwards;
	FDreamGradient::ParseCss(TEXT("linear-gradient(90deg, red 60%, blue 40%)"), Backwards);
	TestTrue(TEXT("out of order, red up to the first position"), IsNearlyColor(Backwards.Evaluate(FVector2f(0.59f, 0.5f)), Red, 1e-5f));
	TestTrue(TEXT("and blue from it"), IsNearlyColor(Backwards.Evaluate(FVector2f(0.61f, 0.5f)), Blue, 1e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintSpreadTest,
	"DreamGUI.Text.Paint.PadRepeatAndReflectSpreadOverTheFirstStopToTheLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintSpreadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	// Black at 20%, white at 80%, across: t is u, and the value read is (t - 0.2) / 0.6 between the stops.
	FDreamGradient Ramp = MakeRamp(EDreamPaintType::Linear);
	Ramp.Angle = 90.0f;
	Ramp.Stops[0].Position = 0.2f;
	Ramp.Stops[1].Position = 0.8f;
	auto At = [&Ramp](float InU) { return Ramp.Evaluate(FVector2f(InU, 0.5f)).R; };
	TestEqual(TEXT("between the stops all three agree"), At(0.5f), 0.5f, 1e-5f);
	TestEqual(TEXT("Pad: before the first stop, its colour"), At(0.1f), 0.0f, 1e-6f);
	TestEqual(TEXT("Pad: after the last, its colour"), At(0.9f), 1.0f, 1e-6f);
	Ramp.Spread = EDreamPaintSpread::Repeat;
	TestEqual(TEXT("Repeat: past the last stop the span starts again"), At(0.9f), (0.3f - 0.2f) / 0.6f, 1e-4f);
	TestEqual(TEXT("Repeat: before the first it is the end of the span before"), At(0.1f), (0.7f - 0.2f) / 0.6f, 1e-4f);
	TestEqual(TEXT("Repeat: just past the last stop is just past the first"), At(0.81f), (0.21f - 0.2f) / 0.6f, 1e-4f);
	Ramp.Spread = EDreamPaintSpread::Reflect;
	TestEqual(TEXT("Reflect: past the last stop the span runs back"), At(0.9f), (0.7f - 0.2f) / 0.6f, 1e-4f);
	TestEqual(TEXT("Reflect: before the first too"), At(0.1f), (0.3f - 0.2f) / 0.6f, 1e-4f);
	TestEqual(TEXT("Reflect: a third of a span past the end"), At(1.0f), (0.6f - 0.2f) / 0.6f, 1e-4f);
	// A span of nothing has nothing to repeat: it pads.
	Ramp.Spread = EDreamPaintSpread::Repeat;
	Ramp.Stops[1].Position = 0.2f;
	TestEqual(TEXT("a zero span pads before"), At(0.1f), 0.0f, 1e-6f);
	TestEqual(TEXT("and after"), At(0.9f), 1.0f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintSpacesTest,
	"DreamGUI.Text.Paint.ColoursMixPremultipliedInTheirInterpolationSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintSpacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	const FVector2f Middle(0.5f, 0.5f);
	// Red to blue halfway, three ways. sRGB mixes the encoded values: (0.5, 0, 0.5), which is 0.214 in linear light.
	const FLinearColor InSrgb = MakeTwoStops(FColor::Red, FColor::Blue).Evaluate(Middle);
	TestTrue(TEXT("in srgb the encoded values mix"), IsNearlyColor(InSrgb, FLinearColor(0.214041f, 0.0f, 0.214041f, 1.0f), 1e-5f));
	const FLinearColor InLinear = MakeTwoStops(FColor::Red, FColor::Blue, EDreamPaintInterpolation::Linear).Evaluate(Middle);
	TestTrue(TEXT("in srgb-linear the light mixes"), IsNearlyColor(InLinear, FLinearColor(0.5f, 0.0f, 0.5f, 1.0f), 1e-5f));
	// Oklab: red is (0.627955, 0.224863, 0.125846), blue (0.452014, -0.032457, -0.311528); their middle, back in linear sRGB.
	const FLinearColor InOklab = MakeTwoStops(FColor::Red, FColor::Blue, EDreamPaintInterpolation::Oklab).Evaluate(Middle);
	TestTrue(TEXT("in oklab the perceptual coordinates mix"), IsNearlyColor(InOklab, FLinearColor(0.263734f, 0.086572f, 0.362824f, 1.0f), 2e-3f));
	TestTrue(TEXT("and an end comes back as the colour it was"),
		IsNearlyColor(MakeTwoStops(FColor::Red, FColor::Blue, EDreamPaintInterpolation::Oklab).Evaluate(FVector2f(0.0f, 0.5f)), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), 1e-3f));

	// Premultiplied: fading red to transparent stays red all the way, only its alpha falls. Mixed straight, the middle
	// would be a darker red at half alpha.
	const FDreamGradient Fade = MakeTwoStops(FColor::Red, FColor(0, 0, 0, 0));
	const FLinearColor Half = Fade.Evaluate(Middle);
	TestEqual(TEXT("the middle of a fade is still full red"), Half.R, 1.0f, 1e-5f);
	TestEqual(TEXT("at half its alpha"), Half.A, 0.5f, 1e-5f);
	const FLinearColor Gone = Fade.Evaluate(FVector2f(1.0f, 0.5f));
	TestTrue(TEXT("where the alpha is 0 the colour is 0"), Gone.A == 0.0f && Gone.R == 0.0f && Gone.G == 0.0f && Gone.B == 0.0f);
	const FLinearColor OklabHalf = MakeTwoStops(FColor::Red, FColor(0, 0, 0, 0), EDreamPaintInterpolation::Oklab).Evaluate(Middle);
	TestTrue(TEXT("in oklab too"), IsNearlyColor(OklabHalf, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f), 1e-3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintMoveTest,
	"DreamGUI.Text.Paint.ScaleOffsetAndATextsAnimationMoveTheGradientAlongItsLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintMoveTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	FDreamGradient Ramp = MakeRamp(EDreamPaintType::Linear);
	Ramp.Angle = 90.0f;
	auto At = [&Ramp](float InU, const FDreamPaintAnimation& InAnimation = FDreamPaintAnimation())
	{
		return Ramp.Evaluate(FVector2f(InU, 0.5f), 1.0f, InAnimation).R;
	};
	Ramp.Scale = 2.0f;
	TestEqual(TEXT("a linear gradient scales about its middle"), At(1.0f), 0.75f, 1e-5f);
	TestEqual(TEXT("both ways"), At(0.0f), 0.25f, 1e-5f);
	Ramp.Scale = 1.0f;
	Ramp.Offset = 0.25f;
	TestEqual(TEXT("an offset moves it forward: the middle shows a quarter"), At(0.5f), 0.25f, 1e-5f);
	Ramp.Offset = 0.0f;
	FDreamPaintAnimation Animation;
	Animation.Phase = 0.25f;
	TestEqual(TEXT("a phase moves it the same way"), At(0.5f, Animation), 0.25f, 1e-5f);
	Animation = FDreamPaintAnimation();
	Animation.ScaleMultiplier = 0.5f;
	TestEqual(TEXT("a scale multiplier squeezes it"), At(0.6f, Animation), 0.7f, 1e-5f);
	Animation = FDreamPaintAnimation();
	Animation.CenterOffset = FVector2f(0.25f, 0.0f);
	TestEqual(TEXT("a centre offset moves a linear gradient's middle"), At(0.75f, Animation), 0.5f, 1e-5f);
	Ramp.Angle = 0.0f;
	Animation = FDreamPaintAnimation();
	Animation.AngleOffset = 90.0f;
	TestEqual(TEXT("an angle offset turns it"), At(0.7f, Animation), 0.7f, 1e-5f);

	FDreamGradient Radial = MakeRamp(EDreamPaintType::Radial);
	Radial.Scale = 2.0f;
	TestEqual(TEXT("a radial gradient scales about its start"), Radial.Evaluate(FVector2f(1.0f, 1.0f)).R, 0.5f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintPackedRowTest,
	"DreamGUI.Text.Paint.APackedRowReadAsTheShaderReadsItEvaluatesToWhatEvaluateAnswers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintPackedRowTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;

	// The layout first, on a gradient with something in every pixel.
	FDreamGradient Gradient;
	Gradient.Type = EDreamPaintType::Radial;
	Gradient.Spread = EDreamPaintSpread::Reflect;
	Gradient.Interpolation = EDreamPaintInterpolation::Linear;
	Gradient.Shape = EDreamPaintRadialShape::Circle;
	Gradient.Size = EDreamPaintRadialSize::ClosestCorner;
	Gradient.Angle = 33.0f;
	Gradient.Scale = 1.5f;
	Gradient.Offset = -0.25f;
	Gradient.Center = FVector2f(0.25f, 0.75f);
	Gradient.Radius = FVector2f(0.125f, 0.375f);
	for (int32 StopIndex = 0; StopIndex < 18; ++StopIndex)
	{
		Gradient.Stops.Add(FDreamGradientStop(StopIndex == 5 ? 0.0f : StopIndex / 20.0f, FColor(255, 255, 255, static_cast<uint8>(StopIndex * 10))));
	}
	TArray<FVector4f> Row;
	Gradient.PackRow(Row);
	if (!TestEqual(TEXT("a row is RowWidth pixels"), Row.Num(), DreamPaintRows::RowWidth))
	{
		return false;
	}
	TestEqual(TEXT("the header: type, spread, space, and only the first MaxStops"), Row[DreamPaintRows::GradientHeaderPixel], FVector4f(2.0f, 2.0f, 1.0f, 16.0f));
	TestEqual(TEXT("angle, scale, offset, shape"), Row[DreamPaintRows::GradientShapePixel], FVector4f(33.0f, 1.5f, -0.25f, 1.0f));
	TestEqual(TEXT("centre and radius"), Row[DreamPaintRows::GradientCenterPixel], FVector4f(0.25f, 0.75f, 0.125f, 0.375f));
	TestEqual(TEXT("the size"), Row[DreamPaintRows::GradientSizePixel], FVector4f(2.0f, 0.0f, 0.0f, 0.0f));
	TestEqual(TEXT("stop 4's position is pixel 5's x"), Row[DreamPaintRows::GradientPositionsPixel + 1].X, 4.0f / 20.0f);
	TestEqual(TEXT("stop 5's, written before the one ahead of it, is fixed up to it"), Row[DreamPaintRows::GradientPositionsPixel + 1].Y, 4.0f / 20.0f);
	TestEqual(TEXT("stop 6's is pixel 5's z"), Row[DreamPaintRows::GradientPositionsPixel + 1].Z, 6.0f / 20.0f);
	const FVector4f Stop3 = Row[DreamPaintRows::GradientColorsPixel + 3];
	TestEqual(TEXT("a colour is premultiplied and in its space: white at alpha 30/255, linear"), Stop3, FVector4f(30.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f, 30.0f / 255.0f));
	for (int32 Pixel = DreamPaintRows::GradientColorsPixel + FDreamGradient::MaxStops; Pixel < DreamPaintRows::RowWidth; ++Pixel)
	{
		if (!TestEqual(TEXT("the rest of the row is 0"), Row[Pixel], FVector4f(0.0f, 0.0f, 0.0f, 0.0f)))
		{
			break;
		}
	}

	// Then every gradient, at every kind of place, through the row and through Evaluate.
	FRandomStream Random(0xC0FFEE);
	int32 Mismatches = 0;
	for (int32 Round = 0; Round < 400 && Mismatches < 5; ++Round)
	{
		FDreamGradient RandomGradient = MakeRandomGradient(Random);
		// Unpainted ones answer white either way; the round is spent better on one that paints.
		if (!RandomGradient.IsPainting())
		{
			RandomGradient.Type = EDreamPaintType::Linear;
			RandomGradient.Stops.Add(FDreamGradientStop(0.5f, FColor::Orange));
		}
		RandomGradient.PackRow(Row);
		for (int32 Sample = 0; Sample < 16; ++Sample)
		{
			const FVector2f UV(Random.FRandRange(-0.25f, 1.25f), Random.FRandRange(-0.25f, 1.25f));
			const float Aspect = Random.FRandRange(0.25f, 4.0f);
			FDreamPaintAnimation Animation;
			Animation.Phase = Random.FRandRange(-1.0f, 1.0f);
			Animation.AngleOffset = Random.FRandRange(-90.0f, 90.0f);
			Animation.CenterOffset = FVector2f(Random.FRandRange(-0.2f, 0.2f), Random.FRandRange(-0.2f, 0.2f));
			Animation.ScaleMultiplier = Random.FRandRange(0.5f, 2.0f);
			const FLinearColor Expected = RandomGradient.Evaluate(UV, Aspect, Animation);
			const FLinearColor Decoded = EvaluatePackedRow(Row, UV, Aspect, Animation);
			if (!IsNearlyColor(Decoded, Expected, 1e-4f))
			{
				++Mismatches;
				AddError(FString::Printf(TEXT("%s at (%f, %f) aspect %f: the row gives %s, Evaluate %s"), *RandomGradient.ToCss(),
					UV.X, UV.Y, Aspect, *Decoded.ToString(), *Expected.ToString()));
				break;
			}
		}
	}
	TestEqual(TEXT("the row and Evaluate agree everywhere"), Mismatches, 0);

	// Sharing is by these pixels: what draws the same packs the same, whatever its stops past MaxStops say.
	FDreamGradient Longer = Gradient;
	Longer.Stops.Add(FDreamGradientStop(1.0f, FColor::Red));
	TestEqual(TEXT("a stop past MaxStops changes nothing drawn, and nothing of the hash"), Longer.GetRowHash(), Gradient.GetRowHash());
	FDreamGradient Signed = MakeRamp(EDreamPaintType::Linear);
	Signed.Offset = -0.0f;
	TestEqual(TEXT("-0 packs as 0"), Signed.GetRowHash(), MakeRamp(EDreamPaintType::Linear).GetRowHash());
	TestNotEqual(TEXT("a different gradient hashes differently"), MakeTwoStops(FColor::Red, FColor::Blue).GetRowHash(), Gradient.GetRowHash());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintTagResolutionTest,
	"DreamGUI.Text.Paint.ATagNameIsTheStyleEntryThenTheProjectPresetThenItsOwnCss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintTagResolutionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;

	FDreamUIRichTextCustomStyleItemData Warn;
	Warn.paintType = EDreamUIRichTextCustomStyleData_PaintType::Set;
	Warn.paint.Gradient = MakeRamp(EDreamPaintType::Radial);
	FDreamUIRichTextCustomStyleItemData Dormant;
	Dormant.paintType = EDreamUIRichTextCustomStyleData_PaintType::Set;
	Dormant.paint.bEnabled = false;
	Dormant.paint.Gradient = MakeRamp(EDreamPaintType::Conic);
	FDreamUIRichTextCustomStyleItemData Kept;
	Kept.paintType = EDreamUIRichTextCustomStyleData_PaintType::KeepOrigin;
	Kept.paint.Gradient = MakeRamp(EDreamPaintType::Diamond);
	TMap<FName, FDreamUIRichTextCustomStyleItemData> Entries;
	Entries.Add(TEXT("Warn"), Warn);
	Entries.Add(TEXT("Dormant"), Dormant);
	Entries.Add(TEXT("Kept"), Kept);
	UDreamUIRichTextCustomStyleData* Styles = NewObject<UDreamUIRichTextCustomStyleData>(GetTransientPackage());
	Styles->SetDataMap(Entries);

	// Project presets live on the settings' default object; this test's own go again at its end.
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	const TMap<FName, FString> SavedPresets = Settings->GradientPresets;
	ON_SCOPE_EXIT
	{
		Settings->GradientPresets = SavedPresets;
	};
	Settings->GradientPresets.Add(TEXT("TestGold"), TEXT("linear-gradient(#FFF3B0, #E8B64A 55%, #9C6A12)"));
	Settings->GradientPresets.Add(TEXT("Warn"), TEXT("linear-gradient(red, blue)"));
	Settings->GradientPresets.Add(TEXT("TestBroken"), TEXT("linear-gradient(red,"));

	FDreamGradient Found;
	TestTrue(TEXT("a style entry set to paint is found"), FDreamTextPaint::ResolveTagPaint(TEXT("Warn"), Styles, Found));
	TestTrue(TEXT("before a preset of the same name"), Found == Warn.paint.Gradient);
	TestTrue(TEXT("its bEnabled is not asked"), FDreamTextPaint::ResolveTagPaint(TEXT("Dormant"), Styles, Found) && Found == Dormant.paint.Gradient);
	TestFalse(TEXT("an entry that does not set a paint sets none"), FDreamTextPaint::ResolveTagPaint(TEXT("Kept"), Styles, Found));
	TestTrue(TEXT("a preset is found by its name"), FDreamTextPaint::ResolveTagPaint(TEXT("TestGold"), nullptr, Found));
	TestEqual(TEXT("and read as CSS"), Found.Stops.Num(), 3);
	TestTrue(TEXT("with no styles, the preset of a style's name"), FDreamTextPaint::ResolveTagPaint(TEXT("Warn"), nullptr, Found)
		&& Found.Type == EDreamPaintType::Linear && Found.Stops.Num() == 2);
	TestTrue(TEXT("a name that is CSS is read as CSS"), FDreamTextPaint::ResolveTagPaint(FName(TEXT("linear-gradient(90deg,red,blue)")), nullptr, Found));
	TestEqual(TEXT("as written"), Found.Angle, 90.0f);

	const FDreamGradient Before = Found;
	TestFalse(TEXT("a preset whose CSS does not read is no paint"), FDreamTextPaint::ResolveTagPaint(TEXT("TestBroken"), nullptr, Found));
	TestFalse(TEXT("nor is a name nothing answers to"), FDreamTextPaint::ResolveTagPaint(TEXT("NoSuchGradient"), Styles, Found));
	TestFalse(TEXT("nor no name"), FDreamTextPaint::ResolveTagPaint(NAME_None, Styles, Found));
	TestTrue(TEXT("and none of them touched the answer"), Found == Before);

	// An edited preset is read again at its next lookup: the cache keeps strings, not names.
	Settings->GradientPresets.Add(TEXT("TestGold"), TEXT("linear-gradient(90deg, white, black)"));
	TestTrue(TEXT("the edited preset is found"), FDreamTextPaint::ResolveTagPaint(TEXT("TestGold"), nullptr, Found));
	TestTrue(TEXT("as it is now"), Found.Angle == 90.0f && Found.Stops.Num() == 2 && Found.Stops[0].Color == FColor::White);

#if WITH_EDITOR
	// An edit in the editor -- the project settings, the gradient picker's "save as preset" -- is announced, so a text
	// that resolved a name to a preset can resolve it again; an edit of another setting is not about the presets.
	int32 Announced = 0;
	const FDelegateHandle Listening = DreamGradientPresets::OnPresetsChanged().AddLambda([&Announced]() { ++Announced; });
	ON_SCOPE_EXIT
	{
		DreamGradientPresets::OnPresetsChanged().Remove(Listening);
	};
	FProperty* PresetsProperty = FindFProperty<FProperty>(UDreamGUISettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UDreamGUISettings, GradientPresets));
	FProperty* OtherProperty = FindFProperty<FProperty>(UDreamGUISettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UDreamGUISettings, SmallTextContrast));
	if (TestNotNull(TEXT("the presets property"), PresetsProperty) && TestNotNull(TEXT("another one"), OtherProperty))
	{
		Settings->GradientPresets.Add(TEXT("TestSaved"), TEXT("radial-gradient(red, blue)"));
		FPropertyChangedEvent PresetsEdit(PresetsProperty);
		Settings->OnSettingChanged().Broadcast(Settings, PresetsEdit);
		TestEqual(TEXT("an edit of the presets is announced"), Announced, 1);
		TestTrue(TEXT("and the preset it added is found at once"), FDreamTextPaint::ResolveTagPaint(TEXT("TestSaved"), nullptr, Found)
			&& Found.Type == EDreamPaintType::Radial);
		FPropertyChangedEvent OtherEdit(OtherProperty);
		Settings->OnSettingChanged().Broadcast(Settings, OtherEdit);
		TestEqual(TEXT("an edit of another setting is not"), Announced, 1);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintStyleTest,
	"DreamGUI.Text.Paint.StylesComparePaintsAndKnowWhetherTheyPaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintTestLocal;
	FDreamTextStyle Style;
	TestFalse(TEXT("a style paints nothing by default"), Style.HasPaints());
	FDreamTextStyle Painted = Style;
	Painted.FacePaint.Gradient = MakeRamp(EDreamPaintType::Linear);
	TestFalse(TEXT("a gradient alone is not on"), Painted.HasPaints());
	TestTrue(TEXT("but it is a different style"), Painted != Style);
	Painted.FacePaint.bEnabled = true;
	TestTrue(TEXT("on, it paints"), Painted.HasPaints());
	FDreamTextStyle Overlaid = Style;
	Overlaid.OverlayPaint.bEnabled = true;
	TestFalse(TEXT("on with no stops, it does not"), Overlaid.HasPaints());
	Overlaid.OverlayPaint.Gradient.Stops.Add(FDreamGradientStop(0.5f, FColor::White));
	TestTrue(TEXT("one stop is enough"), Overlaid.HasPaints());
	FDreamTextStyle Boxed = Style;
	Boxed.PaintBoxVertical = EDreamTextPaintBox::Line;
	TestTrue(TEXT("the boxes are compared"), Boxed != Style);
	FDreamTextStyle Blended = Style;
	Blended.OverlayBlend = EDreamTextOverlayBlend::Add;
	TestTrue(TEXT("and the overlay's blend"), Blended != Style);

	// A preset stands in for the paint's own gradient, and a change of it is announced.
	UDreamGradientAsset* Preset = NewObject<UDreamGradientAsset>(GetTransientPackage());
	int32 Changes = 0;
	Preset->OnGradientChanged().AddLambda([&Changes]() { ++Changes; });
	Preset->SetGradient(MakeRamp(EDreamPaintType::Conic));
	TestEqual(TEXT("setting the gradient tells its users"), Changes, 1);
	FDreamTextPaint WithPreset;
	WithPreset.bEnabled = true;
	WithPreset.Preset = Preset;
	TestTrue(TEXT("a paint with a preset paints with the preset's gradient"), WithPreset.GetEffectiveGradient().Type == EDreamPaintType::Conic);
	TestTrue(TEXT("and paints"), WithPreset.IsPainting());
	FDreamTextPaint WithoutPreset = WithPreset;
	WithoutPreset.Preset = nullptr;
	TestTrue(TEXT("paints with different presets differ"), WithoutPreset != WithPreset);
	TestFalse(TEXT("and without it this one has nothing to paint"), WithoutPreset.IsPainting());
	return true;
}

#endif
