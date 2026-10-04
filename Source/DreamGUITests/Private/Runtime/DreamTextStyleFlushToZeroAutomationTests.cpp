// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUITextData.h"
#include "Math/Float16.h"

/*
 * The text style record, as a GPU that flushes denormals to zero loads it.
 *
 * Every pixel of the record is a bit pattern the shader loads as a float before it takes the bits apart, and most mobile
 * GPUs flush a denormal float to zero on the load: whatever the pattern held is gone before asuint() can see it. Two
 * halves in one pixel make a denormal whenever the first is zero (or nearly) and the second is not -- a face with no
 * softness and some dilation, an underlay offset straight down, no fill dim with a fade width, and the default style's
 * own glow (no width, power 1). A colour makes one with alpha 128 and red under 128. The desktop the code is written on
 * does not flush, so this is asserted on the bits, pixel by pixel, rather than looked for in a picture.
 */
namespace DreamTextStyleFlushToZeroTestLocal
{
	/** A word whose float exponent is all zeros and whose mantissa is not: what a flush-to-zero load turns into 0. */
	bool IsDenormalBits(uint32 InBits)
	{
		const uint32 Exponent = (InBits >> 23) & 0xff;
		const uint32 Mantissa = InBits & 0x007fffff;
		return Exponent == 0 && Mantissa != 0;
	}

	void PackPixels(const FDreamTextStyle& InStyle, uint32 (&OutPixels)[FDreamTextStyle::PackedPixelCount])
	{
		TArray<uint8> Bytes;
		InStyle.Pack(Bytes);
		check(Bytes.Num() == static_cast<int32>(sizeof(OutPixels)));
		FMemory::Memcpy(OutPixels, Bytes.GetData(), sizeof(OutPixels));
	}

	/** DreamUIText_UnpackHalf2: x from the high 16 bits, y from the low. */
	FVector2f ReadHalves(uint32 InPixel)
	{
		FFloat16 X;
		FFloat16 Y;
		X.Encoded = static_cast<uint16>((InPixel >> 16) & 0xffff);
		Y.Encoded = static_cast<uint16>(InPixel & 0xffff);
		return FVector2f(X.GetFloat(), Y.GetFloat());
	}

	/** DreamUIText_UnpackColor. */
	FColor ReadColor(uint32 InPixel)
	{
		return FColor((InPixel >> 16) & 0xff, (InPixel >> 8) & 0xff, InPixel & 0xff, (InPixel >> 24) & 0xff);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextStyleFlushToZeroTest,
	"DreamGUI.Text.Style.NoPackedStylePixelIsADenormalSoFlushToZeroCannotEatIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextStyleFlushToZeroTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextStyleFlushToZeroTestLocal;
	auto ExpectNoDenormal = [this](const TCHAR* InWhat, const FDreamTextStyle& InStyle) -> bool
	{
		uint32 Pixels[FDreamTextStyle::PackedPixelCount];
		PackPixels(InStyle, Pixels);
		for (int32 Pixel = 0; Pixel < FDreamTextStyle::PackedPixelCount; ++Pixel)
		{
			if (IsDenormalBits(Pixels[Pixel]))
			{
				AddError(FString::Printf(TEXT("%s: style pixel %d (0x%08x) is a denormal"), InWhat, Pixel, Pixels[Pixel]));
				return false;
			}
		}
		return true;
	};

	// The cases of the design, each a style an author makes every day.
	FDreamTextStyle Default;
	ExpectNoDenormal(TEXT("the default style (its glow: width 0, power 1)"), Default);
	FDreamTextStyle Dilated;
	Dilated.FaceDilate = 0.1f;
	ExpectNoDenormal(TEXT("no softness with a dilation"), Dilated);
	FDreamTextStyle DropShadow;
	DropShadow.UnderlayColor = FColor(0, 0, 0, 200);
	DropShadow.UnderlayOffset = FVector2f(0.0f, 0.05f);
	ExpectNoDenormal(TEXT("an underlay straight down"), DropShadow);
	FDreamTextStyle HardUnderlay;
	HardUnderlay.UnderlaySoftness = 0.0f;
	HardUnderlay.UnderlayDilate = 0.1f;
	ExpectNoDenormal(TEXT("no underlay softness with a dilation"), HardUnderlay);
	FDreamTextStyle NoDim;
	NoDim.FillDimAlpha = 0.0f;
	NoDim.FillFadeWidth = 0.15f;
	ExpectNoDenormal(TEXT("no fill dim with a fade width"), NoDim);
	FDreamTextStyle HalfAlpha;
	HalfAlpha.OutlineColor = FColor(10, 20, 30, 128);
	HalfAlpha.UnderlayColor = FColor(127, 255, 1, 128);
	HalfAlpha.GlowColor = FColor(0, 0, 1, 128);
	ExpectNoDenormal(TEXT("colours at alpha 128 with red under 128"), HalfAlpha);
	FDreamTextStyle Signed;
	Signed.FaceSoftness = -0.0f;
	Signed.FaceDilate = 0.1f;
	Signed.UnderlayOffset = FVector2f(1.0e-6f, -0.05f);
	ExpectNoDenormal(TEXT("a negative zero, and a first half too small to have an exponent"), Signed);

	// What the shader reads back: the second value of a pair exactly, the first within the nudge, colours as they were.
	uint32 Pixels[FDreamTextStyle::PackedPixelCount];
	PackPixels(Dilated, Pixels);
	const FVector2f Face = ReadHalves(Pixels[0]);
	TestTrue(TEXT("the softness reads as next to nothing"), Face.X >= 0.0f && Face.X <= 6.2e-5f);
	TestEqual(TEXT("and the dilation as written"), Face.Y, FFloat16(0.1f).GetFloat());
	PackPixels(DropShadow, Pixels);
	TestEqual(TEXT("an underlay straight down keeps its y"), ReadHalves(Pixels[4]).Y, FFloat16(0.05f).GetFloat());
	TestTrue(TEXT("and its x is next to nothing"), FMath::Abs(ReadHalves(Pixels[4]).X) <= 6.2e-5f);
	PackPixels(Signed, Pixels);
	TestTrue(TEXT("a nudged negative zero keeps its sign"), ReadHalves(Pixels[0]).X <= 0.0f);
	PackPixels(HalfAlpha, Pixels);
	TestEqual(TEXT("alpha 128 is packed as 129, the outline's RGB kept"), ReadColor(Pixels[1]), FColor(10, 20, 30, 129));
	TestEqual(TEXT("the underlay's"), ReadColor(Pixels[3]), FColor(127, 255, 1, 129));
	TestEqual(TEXT("the glow's"), ReadColor(Pixels[6]), FColor(0, 0, 1, 129));
	FDreamTextStyle Untouched;
	Untouched.FaceSoftness = 0.125f;
	Untouched.FaceDilate = 0.25f;
	Untouched.OutlineColor = FColor(200, 100, 50, 127);
	PackPixels(Untouched, Pixels);
	TestEqual(TEXT("a pair that needs no nudge reads back exactly"), ReadHalves(Pixels[0]), FVector2f(0.125f, 0.25f));
	TestEqual(TEXT("and so does any other alpha"), ReadColor(Pixels[1]), FColor(200, 100, 50, 127));

	// Every pairing of awkward firsts and seconds, and every colour that has an alpha.
	const float Firsts[] = { 0.0f, -0.0f, 1.0e-8f, -1.0e-8f, 3.0e-6f, 6.0e-5f, 0.001f, 0.5f, -0.25f, 2.0f };
	const float Seconds[] = { 0.0f, -0.0f, 1.0e-8f, 6.0e-5f, 0.01f, 0.15f, 1.0f, -3.0f, 100.0f };
	for (const float First : Firsts)
	{
		for (const float Second : Seconds)
		{
			FDreamTextStyle Pair;
			Pair.FaceSoftness = First;
			Pair.FaceDilate = Second;
			Pair.UnderlayOffset = FVector2f(First, Second);
			Pair.FillDimAlpha = First;
			Pair.FillFadeWidth = Second;
			if (!ExpectNoDenormal(*FString::Printf(TEXT("the pair (%g, %g)"), First, Second), Pair))
			{
				return false;
			}
			PackPixels(Pair, Pixels);
			if (!TestEqual(TEXT("the second half is never touched"), ReadHalves(Pixels[0]).Y, FFloat16(Second).GetFloat()))
			{
				return false;
			}
		}
	}
	const uint8 Reds[] = { 0, 1, 64, 127, 128, 255 };
	for (int32 Alpha = 1; Alpha < 256; ++Alpha)
	{
		for (const uint8 Red : Reds)
		{
			FDreamTextStyle Coloured;
			Coloured.OutlineColor = FColor(Red, 7, 9, static_cast<uint8>(Alpha));
			if (!ExpectNoDenormal(TEXT("a colour with an alpha"), Coloured))
			{
				return false;
			}
			PackPixels(Coloured, Pixels);
			const FColor Read = ReadColor(Pixels[1]);
			if (!TestTrue(TEXT("reads back as itself, alpha 128 as 129"),
				Read.R == Red && Read.G == 7 && Read.B == 9 && Read.A == (Alpha == 128 ? 129 : Alpha)))
			{
				return false;
			}
		}
	}
	// The one pattern left: a colour with no alpha at all can be one, and reads back as the transparent colour it is.
	FDreamTextStyle Transparent;
	Transparent.UnderlayColor = FColor(10, 20, 30, 0);
	PackPixels(Transparent, Pixels);
	TestEqual(TEXT("a transparent colour stays transparent whatever a load does with it"),
		static_cast<int32>(ReadColor(IsDenormalBits(Pixels[3]) ? 0u : Pixels[3]).A), 0);
	return true;
}

#endif
