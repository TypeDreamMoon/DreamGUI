// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIValueFormat.h"

#include "Layout/Margin.h"
#include "Math/Color.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Vector2D.h"
#include "Misc/CString.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

/**
 * WHAT "INVERSE" MEANS HERE, per type. Two of the three are exact and one cannot be, and the whole
 * value of writing this down is that the inexact one is inexact in a bounded, stated way rather than
 * in whatever way the implementation happened to come out.
 *
 *   FVector2D / FVector2f / FMargin   Exact. Parse(Print(v)) == v for every finite value, bit for
 *                                     bit, at both single and double precision.
 *   FColor                            Exact. The text IS the eight bits.
 *   FLinearColor                      Quantised once, then stable. Print encodes through sRGB into
 *                                     eight bits, so Parse(Print(v)) is the colour that hex code
 *                                     point names, not v. What is exact is the SECOND trip:
 *                                     Print(Parse(Print(v))) == Print(v). That is the property that
 *                                     actually stops a file creeping -- a value that survives one
 *                                     write-back survives every later one unchanged -- and it holds
 *                                     for all 256 code points on every channel. Measured cost of the
 *                                     one quantisation: at most 0.00464 per channel in LINEAR space,
 *                                     which is larger than 1/255 and always will be, because eight
 *                                     sRGB bits are coarsest where the curve is flattest (near white
 *                                     one code point spans 0.0089 of linear range). A test that
 *                                     asserts "within 1/255 linear" is asserting something false.
 *
 * The single thing FLinearColor cannot carry is a channel outside [0,1]: an HDR tint of 2.0 comes
 * back as 1.0, clamped, permanently. That is a table-level decision, not an implementation accident
 * -- hex is what the header chose for colours -- but it is the one case where the short form loses
 * information a designer could have meant, and it is the first thing to revisit if DreamGUI ever
 * ships glow tints. See the note on Print below for why the escape hatch is not "return false".
 */

namespace DreamUIValueFormatLocal
{
	enum class EShortForm : uint8
	{
		None,
		/** FVector2D, whose components are doubles under LWC. */
		Vector2Double,
		/** FVector2f, whose components are floats. Different struct, different precision, same text. */
		Vector2Float,
		/** FVector, double components under LWC. RelativeScale is why it is here. */
		Vector3Double,
		/** FRotator, printed (Pitch, Yaw, Roll) in degrees. RelativeRotationEuler is why it is here. */
		Rotator,
		LinearColor,
		Color,
		Margin,
	};

	/**
	 * Which of the three the property is, decided on struct identity.
	 *
	 * Never on the struct's NAME. Under LWC there are three spellings -- FVector2D, FVector2d,
	 * FVector2f -- of which the first two are the same C++ type, and a substring or prefix match
	 * would also swallow FVector, FVector4 and FVector2DHalf. Two pointer comparisons cover all
	 * three spellings for free: FNames compare case-insensitively, so the "Vector2d" variant struct
	 * resolves to the same UScriptStruct object as "Vector2D" (NoExportTypes.h has FVector2d
	 * commented out for exactly that collision), leaving only Vector2D and Vector2f to test.
	 */
	static EShortForm Classify(const FProperty* InProperty)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty);
		if (StructProperty == nullptr || StructProperty->Struct == nullptr)
		{
			return EShortForm::None;
		}

		const UScriptStruct* Struct = StructProperty->Struct;
		if (Struct == TBaseStructure<FVector2D>::Get())
		{
			return EShortForm::Vector2Double;
		}
		if (Struct == TVariantStructure<FVector2f>::Get())
		{
			return EShortForm::Vector2Float;
		}
		if (Struct == TBaseStructure<FVector>::Get())
		{
			return EShortForm::Vector3Double;
		}
		if (Struct == TBaseStructure<FRotator>::Get())
		{
			return EShortForm::Rotator;
		}
		if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			return EShortForm::LinearColor;
		}
		if (Struct == TBaseStructure<FColor>::Get())
		{
			return EShortForm::Color;
		}
		// FMargin is an ordinary UHT struct rather than a core variant, so it has no TBaseStructure
		// specialisation of its own and StaticStruct() is the direct answer.
		if (Struct == FMargin::StaticStruct())
		{
			return EShortForm::Margin;
		}
		return EShortForm::None;
	}

	/**
	 * The most fractional digits we will spend before giving up on plain decimal notation.
	 *
	 * 24 is where a double stops being expressible in fixed form for values a UI could plausibly hold:
	 * a double carries at most 17 significant digits, so anything down to 1e-7 still lands inside the
	 * budget. Measured over 200k random floats spanning 1e-10..1e10 this produces zero exponent forms;
	 * over random doubles it produces them for about 9%, all of them values far smaller than a pixel.
	 */
	static constexpr int32 MaxFractionalDigits = 24;

	/**
	 * Above this magnitude we go straight to exponent form.
	 *
	 * Not for correctness -- "%.0f" of FLT_MAX round-trips perfectly -- but because it is 39 digits
	 * long, and a double's would be 309. Past 1e17 the integer part alone is past what any decimal
	 * type here can represent exactly anyway, so the long spelling buys nothing but line width.
	 */
	static constexpr double FixedNotationCeiling = 1.0e17;

	/**
	 * One component, in the shortest text that reads back as the same value.
	 *
	 * NOT FString::SanitizeFloat. That is printf "%f", six decimals, and six decimals is not enough:
	 * over 20k random floats it loses 57% of them (3.14159274f comes back as 3.14159298f). Written
	 * into a .dui that is precisely the drift the header warns about -- the designer's value and the
	 * file's value part company on the first write-back, silently, and a little further on each one
	 * after.
	 *
	 * NOT a fixed "%.9g" / "%.17g" either, even though max_digits10 is guaranteed to round-trip and is
	 * what the engine's own ini writer uses. It writes 400 as "4e+02" and 0.1f as "0.100000001", and
	 * the entire point of the short forms is that (400, 240) is what the author sees.
	 *
	 * So: print, read back, compare, and take the first spelling that survives. Fixed notation first
	 * so ordinary numbers stay ordinary, exponent form only for magnitudes fixed notation cannot say.
	 * The verify step is what makes the exactness a fact rather than a claim about printf -- it holds
	 * whatever the CRT does with rounding, and it is why this loop can afford to start optimistically
	 * at zero decimals.
	 *
	 * Both halves go through the same CRT, so they agree about the decimal separator whatever
	 * LC_NUMERIC says, and nothing in the engine sets it globally (ConsoleManager forces "C" around
	 * its own parsing and puts it back, which is the only place that touches it). A file written on a
	 * machine whose locale differs would still be a hazard -- and it would be a comma inside a tuple,
	 * which is worse than a wrong number -- so if .dui files ever travel between locales, this is the
	 * function that needs an explicit C-locale conversion rather than the CRT's.
	 */
	static FString PrintScalar(const double InValue, const bool bSinglePrecision)
	{
		// -0.0 is written "0". The two compare equal so nothing drifts either way, and a value nudged
		// back to zero should not leave a minus sign in the file for the next reader to puzzle over.
		if (InValue == 0.0)
		{
			return TEXT("0");
		}

		auto RoundTrips = [InValue, bSinglePrecision](const FString& InCandidate) -> bool
		{
			const double Parsed = FCString::Atod(*InCandidate);
			return bSinglePrecision
				? static_cast<float>(Parsed) == static_cast<float>(InValue)
				: Parsed == InValue;
		};

		if (FMath::Abs(InValue) < FixedNotationCeiling)
		{
			for (int32 FractionalDigits = 0; FractionalDigits <= MaxFractionalDigits; ++FractionalDigits)
			{
				FString Candidate = FString::Printf(TEXT("%.*f"), FractionalDigits, InValue);
				if (RoundTrips(Candidate))
				{
					return Candidate;
				}
			}
		}

		// max_digits10: nine significant digits reproduce any float, seventeen any double. The search
		// still runs upward from one so that 1e20 stays "1e+20" instead of "1.00000000e+20".
		const int32 MaxSignificantDigits = bSinglePrecision ? 9 : 17;
		for (int32 SignificantDigits = 1; SignificantDigits <= MaxSignificantDigits; ++SignificantDigits)
		{
			FString Candidate = FString::Printf(TEXT("%.*g"), SignificantDigits, InValue);
			if (RoundTrips(Candidate))
			{
				return Candidate;
			}
		}

		// Unreachable for finite values; infinities and NaNs land here and get printf's spelling,
		// which the parser below will refuse. Refusing is the right end for them: there is no number
		// to write, and inventing one would put a plausible wrong value into the author's file.
		return FString::Printf(TEXT("%.*g"), MaxSignificantDigits, InValue);
	}

	static bool IsAsciiDigit(const TCHAR InChar)
	{
		// Deliberately not FChar::IsDigit, which on wide characters answers for the Unicode digit
		// category: an Arabic-Indic digit would pass the check and then read as zero.
		return InChar >= TEXT('0') && InChar <= TEXT('9');
	}

	/**
	 * A tuple element, validated before it is converted.
	 *
	 * FCString::Atod answers 0 for anything it cannot read, so trusting it alone would turn
	 * `SizeDelta = (400, wide)` into (400, 0) with no complaint -- a silently wrong layout is the one
	 * failure this whole pipeline exists to make impossible. FCString::IsNumeric is not the guard
	 * either: it rejects the exponent forms this file's own printer emits for very small values.
	 */
	static bool TryParseScalar(const FString& InText, double& OutValue)
	{
		const FString Trimmed = InText.TrimStartAndEnd();
		const int32 Length = Trimmed.Len();
		int32 Index = 0;

		if (Index < Length && (Trimmed[Index] == TEXT('+') || Trimmed[Index] == TEXT('-')))
		{
			++Index;
		}

		int32 MantissaDigits = 0;
		while (Index < Length && IsAsciiDigit(Trimmed[Index]))
		{
			++Index;
			++MantissaDigits;
		}
		if (Index < Length && Trimmed[Index] == TEXT('.'))
		{
			++Index;
			while (Index < Length && IsAsciiDigit(Trimmed[Index]))
			{
				++Index;
				++MantissaDigits;
			}
		}
		if (MantissaDigits == 0)
		{
			return false;
		}

		if (Index < Length && (Trimmed[Index] == TEXT('e') || Trimmed[Index] == TEXT('E')))
		{
			++Index;
			if (Index < Length && (Trimmed[Index] == TEXT('+') || Trimmed[Index] == TEXT('-')))
			{
				++Index;
			}
			int32 ExponentDigits = 0;
			while (Index < Length && IsAsciiDigit(Trimmed[Index]))
			{
				++Index;
				++ExponentDigits;
			}
			if (ExponentDigits == 0)
			{
				return false;
			}
		}

		if (Index != Length)
		{
			return false;
		}

		OutValue = FCString::Atod(*Trimmed);
		return true;
	}

	/**
	 * Eight bits per channel as text.
	 *
	 * Six digits when the alpha BYTE is 255, eight otherwise -- the decision is made on the quantised
	 * alpha, never on the float that produced it. A linear alpha of 0.999 encodes to 255, and writing
	 * "FF" for it would be no more faithful than leaving it out; both read back as 1.0. Deciding on
	 * the float instead would put an "FF" suffix on colours that are opaque for every purpose the
	 * file has, which is noise in the author's diff and nothing else.
	 */
	static FString MakeHexText(const FColor& InColor)
	{
		return InColor.A == 255
			? FString::Printf(TEXT("#%02X%02X%02X"), InColor.R, InColor.G, InColor.B)
			: FString::Printf(TEXT("#%02X%02X%02X%02X"), InColor.R, InColor.G, InColor.B, InColor.A);
	}

	static int32 HexDigitValue(const TCHAR InChar)
	{
		if (InChar >= TEXT('0') && InChar <= TEXT('9')) { return InChar - TEXT('0'); }
		if (InChar >= TEXT('a') && InChar <= TEXT('f')) { return 10 + (InChar - TEXT('a')); }
		if (InChar >= TEXT('A') && InChar <= TEXT('F')) { return 10 + (InChar - TEXT('A')); }
		return INDEX_NONE;
	}

	/**
	 * 3, 4, 6 or 8 digits, with or without the '#'.
	 *
	 * Not FColor::FromHex, for two reasons that both matter here: it has no 4-digit (#RGBA) form, and
	 * it answers opaque black for garbage instead of saying no -- and "no" is the entire contract of
	 * the function this feeds, which exists to let the caller raise ValueTypeMismatch at a line
	 * number. The 3- and 4-digit expansion does follow it (and CSS): each digit doubles, so #ABC is
	 * #AABBCC, which is the only expansion where #FFF is white.
	 *
	 * The printer never writes the short forms, so an author's #ABC becomes #AABBCC the first time the
	 * designer writes that line back. That is a real, if small, cost: one line the author did not edit
	 * shows up in their diff. It is accepted rather than fixed, because the alternative -- remembering
	 * per-value which spelling was on disk -- would put a second source of truth next to the value.
	 */
	static bool TryParseHexText(const FString& InText, FColor& OutColor)
	{
		FString Digits = InText.TrimStartAndEnd();
		if (Digits.StartsWith(TEXT("#"), ESearchCase::CaseSensitive))
		{
			Digits.RightChopInline(1);
		}

		const int32 Length = Digits.Len();
		if (Length != 3 && Length != 4 && Length != 6 && Length != 8)
		{
			return false;
		}

		int32 Nibbles[8] = {};
		for (int32 Index = 0; Index < Length; ++Index)
		{
			const int32 Value = HexDigitValue(Digits[Index]);
			if (Value == INDEX_NONE)
			{
				return false;
			}
			Nibbles[Index] = Value;
		}

		if (Length <= 4)
		{
			OutColor.R = static_cast<uint8>(Nibbles[0] * 17);
			OutColor.G = static_cast<uint8>(Nibbles[1] * 17);
			OutColor.B = static_cast<uint8>(Nibbles[2] * 17);
			OutColor.A = Length == 4 ? static_cast<uint8>(Nibbles[3] * 17) : 255;
		}
		else
		{
			OutColor.R = static_cast<uint8>((Nibbles[0] << 4) | Nibbles[1]);
			OutColor.G = static_cast<uint8>((Nibbles[2] << 4) | Nibbles[3]);
			OutColor.B = static_cast<uint8>((Nibbles[4] << 4) | Nibbles[5]);
			OutColor.A = Length == 8 ? static_cast<uint8>((Nibbles[6] << 4) | Nibbles[7]) : 255;
		}
		return true;
	}
}

bool DreamUIValueFormat::HasShortForm(const FProperty* InProperty)
{
	return DreamUIValueFormatLocal::Classify(InProperty) != DreamUIValueFormatLocal::EShortForm::None;
}

int32 DreamUIValueFormat::GetExpectedTupleArity(const FProperty* InProperty)
{
	using namespace DreamUIValueFormatLocal;

	switch (Classify(InProperty))
	{
	case EShortForm::Vector2Double:
	case EShortForm::Vector2Float:
		return 2;
	case EShortForm::Vector3Double:
	case EShortForm::Rotator:
		return 3;
	case EShortForm::Margin:
		return 4;
	default:
		// Colours included: they have a short form, but it is not a tuple, so there is no arity to
		// report and a caller that finds INDEX_NONE here must not raise TupleArityMismatch.
		return INDEX_NONE;
	}
}

/**
 * Note on the one thing this function will NOT do: refuse a value it cannot represent.
 *
 * It is tempting to have an HDR FLinearColor return false and let the caller fall back to
 * ExportTextItem, which would keep it exactly. It does not work, because the fallback has to be
 * symmetric and Parse's false means "raise a diagnostic", not "try ImportText" -- a colour written
 * long-hand would print fine and then fail to read back, which is worse than the clamp. If HDR tints
 * ever need to survive, the fix is a longer short form, not a per-value escape.
 */
bool DreamUIValueFormat::Print(const FProperty* InProperty, const void* InValuePtr, FString& OutText)
{
	using namespace DreamUIValueFormatLocal;

	if (InValuePtr == nullptr)
	{
		return false;
	}

	switch (Classify(InProperty))
	{
	case EShortForm::Vector2Double:
	{
		const FVector2D& Value = *static_cast<const FVector2D*>(InValuePtr);
		OutText = FString::Printf(TEXT("(%s, %s)"),
			*PrintScalar(Value.X, false), *PrintScalar(Value.Y, false));
		return true;
	}
	case EShortForm::Vector2Float:
	{
		const FVector2f& Value = *static_cast<const FVector2f*>(InValuePtr);
		OutText = FString::Printf(TEXT("(%s, %s)"),
			*PrintScalar(Value.X, true), *PrintScalar(Value.Y, true));
		return true;
	}
	case EShortForm::Vector3Double:
	{
		const FVector& Value = *static_cast<const FVector*>(InValuePtr);
		OutText = FString::Printf(TEXT("(%s, %s, %s)"),
			*PrintScalar(Value.X, false), *PrintScalar(Value.Y, false), *PrintScalar(Value.Z, false));
		return true;
	}
	case EShortForm::Rotator:
	{
		// (Pitch, Yaw, Roll) -- FRotator's own declaration order, matched in Parse. The text carries
		// exactly what the euler field holds: no trip through a quaternion, so 370 stays 370 and the
		// number in the file is the number in the panel.
		const FRotator& Value = *static_cast<const FRotator*>(InValuePtr);
		OutText = FString::Printf(TEXT("(%s, %s, %s)"),
			*PrintScalar(Value.Pitch, false), *PrintScalar(Value.Yaw, false), *PrintScalar(Value.Roll, false));
		return true;
	}
	case EShortForm::LinearColor:
	{
		OutText = PrintColorHex(*static_cast<const FLinearColor*>(InValuePtr));
		return true;
	}
	case EShortForm::Color:
	{
		// Straight from the bytes rather than via PrintColorHex(FLinearColor(C)). The two agree --
		// the sRGB pair is an exact inverse on all 256 code points, which the tests pin -- but an
		// FColor's text should not depend on that being true.
		OutText = MakeHexText(*static_cast<const FColor*>(InValuePtr));
		return true;
	}
	case EShortForm::Margin:
	{
		const FMargin& Value = *static_cast<const FMargin*>(InValuePtr);
		OutText = FString::Printf(TEXT("(%s, %s, %s, %s)"),
			*PrintScalar(Value.Left, true), *PrintScalar(Value.Top, true),
			*PrintScalar(Value.Right, true), *PrintScalar(Value.Bottom, true));
		return true;
	}
	default:
		return false;
	}
}

bool DreamUIValueFormat::Parse(const FProperty* InProperty, const FDreamUIValue& InValue, void* OutValuePtr)
{
	using namespace DreamUIValueFormatLocal;

	if (OutValuePtr == nullptr)
	{
		return false;
	}

	const EShortForm Form = Classify(InProperty);
	switch (Form)
	{
	case EShortForm::Vector2Double:
	case EShortForm::Vector2Float:
	case EShortForm::Vector3Double:
	case EShortForm::Rotator:
	case EShortForm::Margin:
	{
		// Exactly the printed arity, with none of ImportText's leniency: FMargin's own text format
		// also accepts one and two elements (uniform, then horizontal/vertical), and taking those
		// would give the file two spellings for the same margin while the printer only ever writes
		// the four. Every designer touch would then rewrite `Padding = 8` into `(8, 8, 8, 8)` --
		// a line the author never edited, in every diff, for the rest of the file's life.
		const int32 Arity = Form == EShortForm::Margin ? 4
			: (Form == EShortForm::Vector3Double || Form == EShortForm::Rotator) ? 3 : 2;
		if (InValue.Kind != EDreamUIValueKind::Tuple || InValue.Elements.Num() != Arity)
		{
			return false;
		}

		// Read every component before writing any: a half-written destination after a refusal is how
		// a value that the caller reported as an error still ends up in the tree.
		double Components[4] = { 0.0, 0.0, 0.0, 0.0 };
		for (int32 Index = 0; Index < Arity; ++Index)
		{
			if (!TryParseScalar(InValue.Elements[Index], Components[Index]))
			{
				return false;
			}
		}

		if (Form == EShortForm::Vector2Double)
		{
			FVector2D& Out = *static_cast<FVector2D*>(OutValuePtr);
			Out.X = Components[0];
			Out.Y = Components[1];
		}
		else if (Form == EShortForm::Vector2Float)
		{
			FVector2f& Out = *static_cast<FVector2f*>(OutValuePtr);
			Out.X = static_cast<float>(Components[0]);
			Out.Y = static_cast<float>(Components[1]);
		}
		else if (Form == EShortForm::Vector3Double)
		{
			FVector& Out = *static_cast<FVector*>(OutValuePtr);
			Out.X = Components[0];
			Out.Y = Components[1];
			Out.Z = Components[2];
		}
		else if (Form == EShortForm::Rotator)
		{
			FRotator& Out = *static_cast<FRotator*>(OutValuePtr);
			Out.Pitch = Components[0];
			Out.Yaw = Components[1];
			Out.Roll = Components[2];
		}
		else
		{
			// Left, Top, Right, Bottom -- the order FMargin's own constructor takes and the order the
			// printer writes. Nothing in the text says which is which, so the two orders agreeing is
			// the whole of the contract.
			FMargin& Out = *static_cast<FMargin*>(OutValuePtr);
			Out.Left = static_cast<float>(Components[0]);
			Out.Top = static_cast<float>(Components[1]);
			Out.Right = static_cast<float>(Components[2]);
			Out.Bottom = static_cast<float>(Components[3]);
		}
		return true;
	}
	case EShortForm::LinearColor:
	{
		if (InValue.Kind != EDreamUIValueKind::HexColor)
		{
			return false;
		}
		return ParseColorHex(InValue.Raw, *static_cast<FLinearColor*>(OutValuePtr));
	}
	case EShortForm::Color:
	{
		if (InValue.Kind != EDreamUIValueKind::HexColor)
		{
			return false;
		}
		FColor Parsed(ForceInitToZero);
		if (!TryParseHexText(InValue.Raw, Parsed))
		{
			return false;
		}
		*static_cast<FColor*>(OutValuePtr) = Parsed;
		return true;
	}
	default:
		return false;
	}
}

/**
 * ToFColor(true) and FLinearColor(FColor) are a matched pair, and which pair you pick is the whole
 * question.
 *
 * The engine offers two: sRGB (ToFColorSRGB / the sRGBToLinearTable constructor, which is what
 * FromSRGBColor is) and plain quantisation (QuantizeRound / ReinterpretAsLinear). Both are internally
 * consistent; mixing one half of each is how a colour drifts, and the mix is easy to write by
 * accident because ToFColor takes the choice as a bool. sRGB is the right one here: hex in a UI file
 * means the sRGB code point a colour picker shows, so #808080 has to be mid-grey to the eye rather
 * than 0.5 linear.
 *
 * They are exact inverses in the direction that matters -- encode(decode(byte)) == byte for all 256,
 * on every channel including alpha -- which is what makes the second write-back byte-identical to the
 * first. Verified against the tables, and pinned by the round-trip test rather than trusted.
 */
FString DreamUIValueFormat::PrintScalar(const double InValue, const bool bSinglePrecision)
{
	// A forwarder rather than a move, so the short-form printers above keep calling the unqualified
	// name they always did and there is still exactly one body.
	return DreamUIValueFormatLocal::PrintScalar(InValue, bSinglePrecision);
}

FString DreamUIValueFormat::PrintColorHex(const FLinearColor& InColor)
{
	return DreamUIValueFormatLocal::MakeHexText(InColor.ToFColor(/*bSRGB*/ true));
}

bool DreamUIValueFormat::ParseColorHex(const FString& InHexDigits, FLinearColor& OutColor)
{
	// Takes the digits with or without the leading '#'. The AST strips it (FDreamUIValue::Raw for a
	// HexColor excludes the '#') while PrintColorHex writes it, so a caller doing the obvious
	// ParseColorHex(PrintColorHex(c)) would otherwise get a refusal for a string this file produced.
	FColor Quantized(ForceInitToZero);
	if (!DreamUIValueFormatLocal::TryParseHexText(InHexDigits, Quantized))
	{
		return false;
	}
	OutColor = FLinearColor(Quantized);
	return true;
}
