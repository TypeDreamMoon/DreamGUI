// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DreamUIValueFormatTestTypes.h"
#include "Layout/Margin.h"
#include "Math/RandomStream.h"
#include "Text/DreamUIValueFormat.h"
#include "UObject/UnrealType.h"

/*
 * The short forms, in both directions, with the round trip as the actual subject.
 *
 * Everything else here is scaffolding for one assertion: Parse(Print(v)) == v. A printer that is
 * merely close is worse than one that is obviously wrong, because the failure it produces is a
 * designer's file whose numbers walk away from the editor's over weeks, one write-back at a time,
 * with no error anywhere. So the numeric cases are randomised over hundreds of values rather than
 * spot-checked -- the six-decimal spelling that FString::SanitizeFloat produces passes every
 * hand-picked example (400, 0.5, 8) and loses more than half of the values a real anchor drag makes.
 *
 * Print writes the text a .dui line contains; Parse consumes what the lexer made of that text. The
 * lexer does not exist yet, so Relex below does its one relevant job by hand -- splitting the
 * parentheses off a tuple and the '#' off a colour. Doing it here rather than inside the format is
 * deliberate: it keeps the printer answerable for producing text a file can actually hold.
 */

namespace DreamUIValueFormatTestLocal
{
	/**
	 * The seed, fixed and written down rather than taken from the clock.
	 *
	 * A round-trip failure is a one-in-thousands event. FMath::Rand() would find one, print a value
	 * nobody can get back, and pass on the next run -- which is the same as not having found it.
	 */
	static constexpr int32 Seed = 20260829;

	/** Per population, per type. Enough that a six-decimal printer fails hundreds of times over. */
	static constexpr int32 SampleCount = 512;

	static const FProperty* Prop(const TCHAR* InName)
	{
		return FindFProperty<FProperty>(FDreamUIValueFormatFixture::StaticStruct(), InName);
	}

	/** Full precision, for failure messages: a message that rounds hides the bug it is reporting. */
	static FString Describe(const double InValue)
	{
		return FString::Printf(TEXT("%.17g"), InValue);
	}

	// ...Value, not ...Tuple: MakeTuple is a global in Templates/Tuple.h, and a `using namespace` that
	// pulls a same-named local into the same overload set is the kind of thing that compiles for a
	// year and then resolves the other way when someone adds an argument.
	static FDreamUIValue MakeTupleValue(const TArray<FString>& InElements)
	{
		FDreamUIValue Value;
		Value.Kind = EDreamUIValueKind::Tuple;
		Value.Elements = InElements;
		return Value;
	}

	/** Raw carries the digits and NOT the '#', which is what FDreamUIValue promises for a HexColor. */
	static FDreamUIValue MakeHexValue(const FString& InText)
	{
		FDreamUIValue Value;
		Value.Kind = EDreamUIValueKind::HexColor;
		Value.Raw = InText.StartsWith(TEXT("#")) ? InText.RightChop(1) : InText;
		return Value;
	}

	static FDreamUIValue MakeSimpleValue(const EDreamUIValueKind InKind, const FString& InRaw)
	{
		FDreamUIValue Value;
		Value.Kind = InKind;
		Value.Raw = InRaw;
		return Value;
	}

	/**
	 * The lexer's share of the round trip.
	 *
	 * Splitting a tuple on ',' is only safe because the printer never writes a comma inside a number.
	 * It cannot, while the process is in the C numeric locale, which is the default and which nothing
	 * in the engine changes globally -- see the note in DreamUIValueFormat.cpp.
	 */
	static bool Relex(const FString& InText, FDreamUIValue& OutValue)
	{
		const FString Trimmed = InText.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("#")))
		{
			OutValue = MakeHexValue(Trimmed);
			return true;
		}
		if (Trimmed.StartsWith(TEXT("(")) && Trimmed.EndsWith(TEXT(")")))
		{
			TArray<FString> Elements;
			Trimmed.Mid(1, Trimmed.Len() - 2).ParseIntoArray(Elements, TEXT(","), false);
			for (FString& Element : Elements)
			{
				Element = Element.TrimStartAndEnd();
			}
			OutValue = MakeTupleValue(Elements);
			return true;
		}
		return false;
	}

	/** Print, re-lex, parse. Fails on any of the three, so a caller only has to look at the value. */
	template <typename StructType>
	static bool RoundTrip(const FProperty* InProperty, const StructType& InValue, StructType& OutValue, FString& OutText)
	{
		if (!DreamUIValueFormat::Print(InProperty, &InValue, OutText))
		{
			return false;
		}
		FDreamUIValue Relexed;
		if (!Relex(OutText, Relexed))
		{
			return false;
		}
		return DreamUIValueFormat::Parse(InProperty, Relexed, &OutValue);
	}

	/**
	 * A random BIT PATTERN, not a random number in a range.
	 *
	 * FRand() has 24 bits of mantissa, so every value it produces has a short exact decimal and the
	 * printer never has to work. The values that need all 17 significant digits -- the ones a
	 * SanitizeFloat-style printer silently rounds -- only come from the whole space.
	 */
	static double RandomDoubleBits(FRandomStream& InStream)
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const uint64 Bits = (static_cast<uint64>(InStream.GetUnsignedInt()) << 32)
				| static_cast<uint64>(InStream.GetUnsignedInt());
			double Value = 0.0;
			FMemory::Memcpy(&Value, &Bits, sizeof(Value));
			if (FMath::IsFinite(Value))
			{
				return Value;
			}
		}
		return 0.0;
	}

	static float RandomFloatBits(FRandomStream& InStream)
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const uint32 Bits = InStream.GetUnsignedInt();
			float Value = 0.0f;
			FMemory::Memcpy(&Value, &Bits, sizeof(Value));
			if (FMath::IsFinite(Value))
			{
				return Value;
			}
		}
		return 0.0f;
	}

	/** The other population: numbers a .dui would really hold. Both have to survive. */
	static double RandomLayoutValue(FRandomStream& InStream)
	{
		return InStream.FRandRange(-4096.0, 4096.0);
	}

	/** The smallest positive float there is. Denormal, so the exponent path has to reach it. */
	static float SmallestDenormalFloat()
	{
		const uint32 Bits = 1u;
		float Value = 0.0f;
		FMemory::Memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIValueFormatHasShortFormTest,
	"DreamGUI.Text.HasShortFormAnswersOnlyForTheThreeShortForms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIValueFormatHasShortFormTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIValueFormatTestLocal;

	// A missing property here means the fixture drifted, not that the format is wrong, and the two
	// failures look nothing alike -- so say which it is before asserting anything about the answer.
	const TCHAR* Names[] = { TEXT("Vector2D"), TEXT("Vector2f"), TEXT("LinearColor"), TEXT("Color"),
		TEXT("Margin"), TEXT("Vector"), TEXT("IntPoint"), TEXT("Scalar"), TEXT("Label") };
	for (const TCHAR* Name : Names)
	{
		if (!TestNotNull(*FString::Printf(TEXT("the fixture still declares '%s'"), Name), Prop(Name)))
		{
			return false;
		}
	}

	TestTrue(TEXT("FVector2D has a short form"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Vector2D"))));
	TestTrue(TEXT("so does its single-precision variant"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Vector2f"))));
	TestTrue(TEXT("FLinearColor has one"), DreamUIValueFormat::HasShortForm(Prop(TEXT("LinearColor"))));
	TestTrue(TEXT("FColor has one"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Color"))));
	TestTrue(TEXT("FMargin has one"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Margin"))));

	// The table is three entries and stays three entries. Each of these is a specific wrong match:
	// FVector shares a name prefix, FIntPoint shares a shape, and the last two are not structs at all.
	TestFalse(TEXT("FVector does not, despite the name"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Vector"))));
	TestFalse(TEXT("nor FIntPoint, despite the shape"), DreamUIValueFormat::HasShortForm(Prop(TEXT("IntPoint"))));
	TestFalse(TEXT("nor a float"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Scalar"))));
	TestFalse(TEXT("nor a string"), DreamUIValueFormat::HasShortForm(Prop(TEXT("Label"))));
	TestFalse(TEXT("nor no property at all"), DreamUIValueFormat::HasShortForm(nullptr));

	TestEqual(TEXT("a vector's short form is two elements"), DreamUIValueFormat::GetExpectedTupleArity(Prop(TEXT("Vector2D"))), 2);
	TestEqual(TEXT("and so is the float variant's"), DreamUIValueFormat::GetExpectedTupleArity(Prop(TEXT("Vector2f"))), 2);
	TestEqual(TEXT("a margin's is four"), DreamUIValueFormat::GetExpectedTupleArity(Prop(TEXT("Margin"))), 4);

	// A colour HAS a short form but no arity, and the distinction is load-bearing: a caller that read
	// INDEX_NONE as "no short form" would send colours to ExportTextItem, and one that reported an
	// arity mismatch on a colour would name a number the syntax never has.
	TestEqual(TEXT("a colour reports no arity"), DreamUIValueFormat::GetExpectedTupleArity(Prop(TEXT("LinearColor"))), int32(INDEX_NONE));
	TestEqual(TEXT("nor does an 8-bit colour"), DreamUIValueFormat::GetExpectedTupleArity(Prop(TEXT("Color"))), int32(INDEX_NONE));
	TestEqual(TEXT("nor does anything outside the table"), DreamUIValueFormat::GetExpectedTupleArity(Prop(TEXT("Vector"))), int32(INDEX_NONE));
	TestEqual(TEXT("nor does nothing"), DreamUIValueFormat::GetExpectedTupleArity(nullptr), int32(INDEX_NONE));

	// Print refuses the same set, and must leave the caller's string alone when it does -- the caller
	// is about to write ExportTextItem into it.
	FString Untouched = TEXT("sentinel");
	FDreamUIValueFormatFixture Fixture;
	TestFalse(TEXT("printing something outside the table is refused"),
		DreamUIValueFormat::Print(Prop(TEXT("Vector")), &Fixture.Vector, Untouched));
	TestEqual(TEXT("and leaves the output string alone"), Untouched, FString(TEXT("sentinel")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIValueFormatRoundTripTest,
	"DreamGUI.Text.EveryShortFormRoundTripsExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIValueFormatRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIValueFormatTestLocal;

	const FProperty* Vector2DProperty = Prop(TEXT("Vector2D"));
	const FProperty* Vector2fProperty = Prop(TEXT("Vector2f"));
	const FProperty* MarginProperty = Prop(TEXT("Margin"));
	if (!TestNotNull(TEXT("the fixture declares the numeric short forms"), Vector2DProperty)
		|| !TestNotNull(TEXT("including the float variant"), Vector2fProperty)
		|| !TestNotNull(TEXT("and the margin"), MarginProperty))
	{
		return false;
	}

	FRandomStream Stream(Seed);

	// One error per type, not one per sample: a printer that is off by a digit fails every sample, and
	// a thousand identical lines buries the one value you needed to see.
	int32 Failures = 0;
	FString FirstFailure;
	auto Note = [&Failures, &FirstFailure](FString InMessage)
	{
		if (Failures++ == 0)
		{
			FirstFailure = MoveTemp(InMessage);
		}
	};
	auto Report = [this, &Failures, &FirstFailure](const TCHAR* InWhat)
	{
		TestEqual(InWhat, Failures, 0);
		if (Failures > 0)
		{
			AddError(FString::Printf(TEXT("first of %d: %s"), Failures, *FirstFailure));
		}
		Failures = 0;
		FirstFailure.Reset();
	};

	for (int32 Index = 0; Index < SampleCount * 2; ++Index)
	{
		// Two populations, interleaved so one seed drives both: whole-space bit patterns, which need
		// every significant digit, and layout-sized numbers, which are what the file will really hold.
		//
		// Drawn into named locals, never as two arguments to a constructor: C++ does not order
		// argument evaluation, so the same seed would produce a different sample per compiler and the
		// reproducibility this seed exists for would be gone.
		const bool bWholeSpace = Index < SampleCount;
		const double FirstDraw = bWholeSpace ? RandomDoubleBits(Stream) : RandomLayoutValue(Stream);
		const double SecondDraw = bWholeSpace ? RandomDoubleBits(Stream) : RandomLayoutValue(Stream);
		const FVector2D Original(FirstDraw, SecondDraw);

		FVector2D Parsed(FVector2D::ZeroVector);
		FString Text;
		if (!RoundTrip(Vector2DProperty, Original, Parsed, Text))
		{
			Note(FString::Printf(TEXT("(%s, %s) did not survive printing at all; text was '%s'"),
				*Describe(Original.X), *Describe(Original.Y), *Text));
		}
		else if (Parsed.X != Original.X || Parsed.Y != Original.Y)
		{
			Note(FString::Printf(TEXT("(%s, %s) printed as '%s' and read back (%s, %s)"),
				*Describe(Original.X), *Describe(Original.Y), *Text,
				*Describe(Parsed.X), *Describe(Parsed.Y)));
		}
	}
	Report(TEXT("every FVector2D comes back bit for bit"));

	for (int32 Index = 0; Index < SampleCount * 2; ++Index)
	{
		const bool bWholeSpace = Index < SampleCount;
		const float FirstDraw = bWholeSpace ? RandomFloatBits(Stream) : static_cast<float>(RandomLayoutValue(Stream));
		const float SecondDraw = bWholeSpace ? RandomFloatBits(Stream) : static_cast<float>(RandomLayoutValue(Stream));
		const FVector2f Original(FirstDraw, SecondDraw);

		FVector2f Parsed(FVector2f::ZeroVector);
		FString Text;
		if (!RoundTrip(Vector2fProperty, Original, Parsed, Text))
		{
			Note(FString::Printf(TEXT("(%s, %s) did not survive printing at all; text was '%s'"),
				*Describe(Original.X), *Describe(Original.Y), *Text));
		}
		else if (Parsed.X != Original.X || Parsed.Y != Original.Y)
		{
			Note(FString::Printf(TEXT("(%s, %s) printed as '%s' and read back (%s, %s)"),
				*Describe(Original.X), *Describe(Original.Y), *Text,
				*Describe(Parsed.X), *Describe(Parsed.Y)));
		}
	}
	Report(TEXT("every FVector2f comes back bit for bit"));

	for (int32 Index = 0; Index < SampleCount * 2; ++Index)
	{
		const bool bWholeSpace = Index < SampleCount;
		auto NextComponent = [&Stream, bWholeSpace]()
		{
			return bWholeSpace ? RandomFloatBits(Stream) : static_cast<float>(RandomLayoutValue(Stream));
		};
		FMargin Original;
		Original.Left = NextComponent();
		Original.Top = NextComponent();
		Original.Right = NextComponent();
		Original.Bottom = NextComponent();

		FMargin Parsed;
		FString Text;
		if (!RoundTrip(MarginProperty, Original, Parsed, Text))
		{
			Note(FString::Printf(TEXT("a margin did not survive printing at all; text was '%s'"), *Text));
		}
		// Compared field by field, in the order the text writes them. An == that happened to be
		// order-insensitive would let a printer that swaps Top and Right pass every symmetric sample.
		else if (Parsed.Left != Original.Left || Parsed.Top != Original.Top
			|| Parsed.Right != Original.Right || Parsed.Bottom != Original.Bottom)
		{
			Note(FString::Printf(TEXT("(%s, %s, %s, %s) printed as '%s' and read back (%s, %s, %s, %s)"),
				*Describe(Original.Left), *Describe(Original.Top), *Describe(Original.Right), *Describe(Original.Bottom),
				*Text,
				*Describe(Parsed.Left), *Describe(Parsed.Top), *Describe(Parsed.Right), *Describe(Parsed.Bottom)));
		}
	}
	Report(TEXT("every FMargin comes back bit for bit, in order"));

	// Margin order, pinned once explicitly, because the loop above can only catch a swap by luck.
	{
		FMargin Original;
		Original.Left = 1.0f;
		Original.Top = 2.0f;
		Original.Right = 3.0f;
		Original.Bottom = 4.0f;
		FString Text;
		TestTrue(TEXT("a margin prints"), DreamUIValueFormat::Print(MarginProperty, &Original, Text));
		TestEqual(TEXT("in Left, Top, Right, Bottom order"), Text, FString(TEXT("(1, 2, 3, 4)")));
	}

	// The readable spelling is the whole reason the short forms exist; an exact printer that wrote
	// "(4e+02, 2.4e+02)" would pass every assertion above and fail the actual requirement.
	{
		const FVector2D Original(400.0, 240.0);
		FString Text;
		TestTrue(TEXT("a size prints"), DreamUIValueFormat::Print(Vector2DProperty, &Original, Text));
		TestEqual(TEXT("as the plain decimal an author would write"), Text, FString(TEXT("(400, 240)")));
	}
	{
		FMargin Original(8.0f);
		FString Text;
		TestTrue(TEXT("a uniform margin prints"), DreamUIValueFormat::Print(MarginProperty, &Original, Text));
		TestEqual(TEXT("with no spurious decimals"), Text, FString(TEXT("(8, 8, 8, 8)")));
	}
	{
		const FVector2D Original(0.5, -0.25);
		FString Text;
		TestTrue(TEXT("a fractional size prints"), DreamUIValueFormat::Print(Vector2DProperty, &Original, Text));
		TestEqual(TEXT("with exactly the digits it needs"), Text, FString(TEXT("(0.5, -0.25)")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIValueFormatBoundaryTest,
	"DreamGUI.Text.BoundaryNumbersSurviveTheRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIValueFormatBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIValueFormatTestLocal;

	const FProperty* Vector2DProperty = Prop(TEXT("Vector2D"));
	const FProperty* Vector2fProperty = Prop(TEXT("Vector2f"));
	if (!TestNotNull(TEXT("the fixture declares the numeric short forms"), Vector2DProperty)
		|| !TestNotNull(TEXT("including the float variant"), Vector2fProperty))
	{
		return false;
	}

	// The values a randomised run reaches only by accident, or not at all. Zero and negative zero are
	// here because the printer normalises one into the other; the denormal and the maxima are here
	// because they are where fixed notation has to hand over to the exponent form.
	const double DoubleCases[] = {
		0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 400.0, 240.0, 8.0, 0.1, 1.0 / 3.0, UE_DOUBLE_PI,
		TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Min(),
		1.0e17, 1.0e-17, 1.0e-4, 1.0e-5, 9007199254740993.0, 0.9999999999999999, -0.0000001,
	};
	for (const double Case : DoubleCases)
	{
		const FVector2D Original(Case, -Case);
		FVector2D Parsed(FVector2D::ZeroVector);
		FString Text;
		const bool bRoundTripped = RoundTrip(Vector2DProperty, Original, Parsed, Text)
			&& Parsed.X == Original.X && Parsed.Y == Original.Y;
		TestTrue(*FString::Printf(TEXT("the double %s survives (printed '%s')"), *Describe(Case), *Text), bRoundTripped);
	}

	const float FloatCases[] = {
		0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 400.0f, 8.0f, 0.1f, 1.0f / 3.0f, UE_PI,
		TNumericLimits<float>::Max(), TNumericLimits<float>::Lowest(), TNumericLimits<float>::Min(),
		SmallestDenormalFloat(), 16777216.0f, 16777215.0f, 0.9999999f, 1.0000001f, 1.0e-4f, 1.0e-5f,
	};
	for (const float Case : FloatCases)
	{
		const FVector2f Original(Case, -Case);
		FVector2f Parsed(FVector2f::ZeroVector);
		FString Text;
		const bool bRoundTripped = RoundTrip(Vector2fProperty, Original, Parsed, Text)
			&& Parsed.X == Original.X && Parsed.Y == Original.Y;
		TestTrue(*FString::Printf(TEXT("the float %s survives (printed '%s')"), *Describe(Case), *Text), bRoundTripped);
	}

	// The two spellings that make the case for the whole approach, pinned explicitly rather than left
	// to the random population. FString::SanitizeFloat writes pi as "3.141593", which is a DIFFERENT
	// float; a fixed max_digits10 printer writes a tenth as "0.100000001", which is the same float
	// spelled unreadably. Seven decimals is what pi actually needs -- one fewer than max_digits10
	// would spend, because the search stops at the first spelling that reads back, not at a
	// worst-case digit count.
	{
		const FVector2f Original(UE_PI, 0.1f);
		FVector2f Parsed(FVector2f::ZeroVector);
		FString Text;
		TestTrue(TEXT("pi and a tenth survive at single precision"),
			RoundTrip(Vector2fProperty, Original, Parsed, Text) && Parsed.X == Original.X && Parsed.Y == Original.Y);
		TestEqual(TEXT("spelled with the digits each needs and no more"), Text, FString(TEXT("(3.1415927, 0.1)")));
	}

	// Negative zero is printed as zero on purpose: they compare equal, so nothing drifts, and a stray
	// minus sign in a file is a question the next reader has to answer.
	{
		const FVector2D Original(-0.0, 0.0);
		FString Text;
		TestTrue(TEXT("negative zero prints"), DreamUIValueFormat::Print(Vector2DProperty, &Original, Text));
		TestEqual(TEXT("without its sign"), Text, FString(TEXT("(0, 0)")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIValueFormatColorTest,
	"DreamGUI.Text.AColourRoundTripIsQuantisedButStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIValueFormatColorTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIValueFormatTestLocal;

	const FProperty* ColorProperty = Prop(TEXT("Color"));
	const FProperty* LinearColorProperty = Prop(TEXT("LinearColor"));
	if (!TestNotNull(TEXT("the fixture declares an 8-bit colour"), ColorProperty)
		|| !TestNotNull(TEXT("and a linear one"), LinearColorProperty))
	{
		return false;
	}

	FRandomStream Stream(Seed);

	// FColor is exact, with no room for interpretation: the text IS the bits.
	int32 ColorFailures = 0;
	FString FirstColorFailure;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const uint32 Bits = Stream.GetUnsignedInt();
		const FColor Original(
			static_cast<uint8>(Bits & 0xFF), static_cast<uint8>((Bits >> 8) & 0xFF),
			static_cast<uint8>((Bits >> 16) & 0xFF), static_cast<uint8>((Bits >> 24) & 0xFF));

		FColor Parsed = FColor::Black;
		FString Text;
		if (!RoundTrip(ColorProperty, Original, Parsed, Text) || Parsed != Original)
		{
			if (ColorFailures++ == 0)
			{
				FirstColorFailure = FString::Printf(TEXT("%s printed as '%s' and read back %s"),
					*Original.ToString(), *Text, *Parsed.ToString());
			}
		}
	}
	TestEqual(TEXT("every FColor comes back byte for byte"), ColorFailures, 0);
	if (ColorFailures > 0)
	{
		AddError(FString::Printf(TEXT("first of %d: %s"), ColorFailures, *FirstColorFailure));
	}

	// The channel order, pinned separately. FColor's memory is BGRA and its members are not, so a
	// printer that read the struct as bytes rather than through R/G/B/A would pass a grey-only test
	// and swap red with blue on every real colour.
	{
		const FColor Original(0x12, 0x34, 0x56, 0x78);
		FString Text;
		TestTrue(TEXT("an 8-bit colour prints"), DreamUIValueFormat::Print(ColorProperty, &Original, Text));
		TestEqual(TEXT("red first, then green, blue, alpha"), Text, FString(TEXT("#12345678")));
	}
	{
		const FColor Original(0x1E, 0x1E, 0x1E, 0xFF);
		FString Text;
		TestTrue(TEXT("an opaque colour prints"), DreamUIValueFormat::Print(ColorProperty, &Original, Text));
		TestEqual(TEXT("in six digits, with the alpha left off"), Text, FString(TEXT("#1E1E1E")));
	}

	// The sRGB pair is an exact inverse on every code point, and that -- not any tolerance -- is what
	// makes a linear colour's second write-back byte-identical to its first. If this fails, the pair
	// has been mixed up (ToFColor(false) against FromSRGBColor, or the reverse) and every colour in
	// every .dui will shift a little on each save.
	int32 CodePointFailures = 0;
	for (int32 Byte = 0; Byte < 256; ++Byte)
	{
		const FColor Quantized(static_cast<uint8>(Byte), static_cast<uint8>(255 - Byte),
			static_cast<uint8>((Byte * 7) & 0xFF), static_cast<uint8>(Byte));
		FString Direct;
		if (!DreamUIValueFormat::Print(ColorProperty, &Quantized, Direct)
			|| DreamUIValueFormat::PrintColorHex(FLinearColor(Quantized)) != Direct)
		{
			++CodePointFailures;
		}
	}
	TestEqual(TEXT("decoding a code point to linear and encoding it back lands on the same code point"),
		CodePointFailures, 0);

	// FLinearColor: one quantisation, then nothing moves again.
	int32 StabilityFailures = 0;
	int32 AccuracyFailures = 0;
	double WorstChannelError = 0.0;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float DrawR = Stream.FRand();
		const float DrawG = Stream.FRand();
		const float DrawB = Stream.FRand();
		const float DrawA = Stream.FRand();
		const FLinearColor Original(DrawR, DrawG, DrawB, DrawA);

		FLinearColor Parsed = FLinearColor::Black;
		FString Text;
		if (!RoundTrip(LinearColorProperty, Original, Parsed, Text))
		{
			++StabilityFailures;
			continue;
		}

		FString SecondText;
		if (!DreamUIValueFormat::Print(LinearColorProperty, &Parsed, SecondText) || SecondText != Text)
		{
			++StabilityFailures;
		}

		// Deliberately NOT 1/255. Eight sRGB bits are coarsest where the curve is flattest: near white
		// one code point spans 0.0089 of linear range, so the worst one-way error is 0.00464, which is
		// larger than 1/255 and always will be. Tightening this bound does not make colours better, it
		// makes the test wrong.
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			const double Error = FMath::Abs(
				static_cast<double>(Parsed.Component(Channel)) - static_cast<double>(Original.Component(Channel)));
			WorstChannelError = FMath::Max(WorstChannelError, Error);
			if (Error >= 1.0 / 200.0)
			{
				++AccuracyFailures;
			}
		}
	}
	TestEqual(TEXT("printing a colour that came from text writes the same text again"), StabilityFailures, 0);
	TestEqual(TEXT("and no channel moved by as much as 1/200 in linear space"), AccuracyFailures, 0);
	AddInfo(FString::Printf(TEXT("worst linear-space channel error over %d colours: %s"),
		SampleCount, *Describe(WorstChannelError)));

	// Six digits or eight, decided on the quantised alpha rather than the float that produced it.
	{
		const FLinearColor Opaque(0.5f, 0.25f, 0.125f, 1.0f);
		FString Text;
		TestTrue(TEXT("an opaque linear colour prints"), DreamUIValueFormat::Print(LinearColorProperty, &Opaque, Text));
		TestEqual(TEXT("in six digits"), Text.Len(), 7);

		const FLinearColor Translucent(0.5f, 0.25f, 0.125f, 0.5f);
		TestTrue(TEXT("a translucent one prints"), DreamUIValueFormat::Print(LinearColorProperty, &Translucent, Text));
		TestEqual(TEXT("in eight"), Text.Len(), 9);

		// Nearly-opaque: the alpha byte is 255, so the eighth pair would say nothing the sixth does not.
		const FLinearColor NearlyOpaque(0.5f, 0.25f, 0.125f, 0.999f);
		TestTrue(TEXT("a nearly-opaque one prints"), DreamUIValueFormat::Print(LinearColorProperty, &NearlyOpaque, Text));
		TestEqual(TEXT("in six, because its alpha byte is 255"), Text.Len(), 7);
	}

	// The one thing hex cannot carry, pinned so it stays a decision. An HDR tint clamps and does not
	// come back; if DreamGUI ever ships glow tints this test is where the cost becomes visible.
	{
		const FLinearColor Hdr(2.0f, 2.0f, 2.0f, 1.0f);
		FString Text;
		TestTrue(TEXT("an out-of-gamut colour still prints"), DreamUIValueFormat::Print(LinearColorProperty, &Hdr, Text));
		TestEqual(TEXT("clamped to white"), Text, FString(TEXT("#FFFFFF")));

		FLinearColor Parsed = FLinearColor::Black;
		TestTrue(TEXT("and reads back"), DreamUIValueFormat::ParseColorHex(Text, Parsed));
		TestTrue(TEXT("as 1.0, not 2.0 -- the documented loss"), Parsed.R == 1.0f && Parsed.A == 1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIValueFormatHexDigitsTest,
	"DreamGUI.Text.HexDigitCountsThreeFourSixAndEightAreAllReadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIValueFormatHexDigitsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIValueFormatTestLocal;

	const FProperty* ColorProperty = Prop(TEXT("Color"));
	if (!TestNotNull(TEXT("the fixture declares an 8-bit colour"), ColorProperty))
	{
		return false;
	}

	auto ParseColor = [ColorProperty](const TCHAR* InDigits, FColor& OutColor)
	{
		return DreamUIValueFormat::Parse(ColorProperty, MakeHexValue(InDigits), &OutColor);
	};

	FColor Parsed = FColor::Black;

	// Each digit doubles, which is the only expansion where #FFF is white rather than #0F0F0F.
	TestTrue(TEXT("three digits read"), ParseColor(TEXT("1AB"), Parsed));
	TestEqual(TEXT("as each digit doubled, opaque"), Parsed, FColor(0x11, 0xAA, 0xBB, 0xFF));
	TestTrue(TEXT("four digits read"), ParseColor(TEXT("1AB8"), Parsed));
	TestEqual(TEXT("with the fourth as alpha"), Parsed, FColor(0x11, 0xAA, 0xBB, 0x88));
	TestTrue(TEXT("six digits read"), ParseColor(TEXT("1E1E1E"), Parsed));
	TestEqual(TEXT("as the bytes they are, opaque"), Parsed, FColor(0x1E, 0x1E, 0x1E, 0xFF));
	TestTrue(TEXT("eight digits read"), ParseColor(TEXT("1E1E1E80"), Parsed));
	TestEqual(TEXT("with the last byte as alpha"), Parsed, FColor(0x1E, 0x1E, 0x1E, 0x80));
	TestTrue(TEXT("#FFF is white"), ParseColor(TEXT("FFF"), Parsed) && Parsed == FColor::White);

	TestTrue(TEXT("lower case reads"), ParseColor(TEXT("1e1e1e"), Parsed));
	TestEqual(TEXT("as the same colour as upper case"), Parsed, FColor(0x1E, 0x1E, 0x1E, 0xFF));

	// Surrounding whitespace is trimmed rather than refused: the lexer hands over trimmed text, but
	// the write-back path calls in with whatever was on the line.
	TestTrue(TEXT("padding around the digits reads"), ParseColor(TEXT("  1E1E1E  "), Parsed));
	TestEqual(TEXT("as the colour inside it"), Parsed, FColor(0x1E, 0x1E, 0x1E, 0xFF));

	// Every other length is a refusal, not a best guess. FColor::FromHex answers black for all of
	// these, which is exactly the silent-wrong-value this layer exists to convert into a line number.
	const TCHAR* BadDigits[] = { TEXT(""), TEXT("1"), TEXT("12"), TEXT("12345"), TEXT("1234567"),
		TEXT("123456789"), TEXT("GGGGGG"), TEXT("12345G"), TEXT("12 456"), TEXT("0x1E1E"), TEXT("-12345") };
	for (const TCHAR* Bad : BadDigits)
	{
		const FColor Sentinel(1, 2, 3, 4);
		FColor Destination = Sentinel;
		TestFalse(*FString::Printf(TEXT("'%s' is refused"), Bad), ParseColor(Bad, Destination));
		TestTrue(*FString::Printf(TEXT("and '%s' left the destination alone"), Bad), Destination == Sentinel);
	}

	// The '#' asymmetry: PrintColorHex writes it because that is what a .dui line holds, while
	// FDreamUIValue::Raw arrives with it already stripped. ParseColorHex has to take both, or the
	// obvious composition of this file's own two halves is a refusal.
	FLinearColor Linear = FLinearColor::Black;
	TestTrue(TEXT("ParseColorHex takes the digits with a '#'"), DreamUIValueFormat::ParseColorHex(TEXT("#1E1E1E"), Linear));
	FLinearColor Bare = FLinearColor::Black;
	TestTrue(TEXT("and without one"), DreamUIValueFormat::ParseColorHex(TEXT("1E1E1E"), Bare));
	TestTrue(TEXT("landing on the same colour either way"), Linear == Bare);
	TestTrue(TEXT("so its own printer's output reads back"),
		DreamUIValueFormat::ParseColorHex(DreamUIValueFormat::PrintColorHex(FLinearColor::Red), Linear));
	TestFalse(TEXT("but a bare '#' is still nothing"), DreamUIValueFormat::ParseColorHex(TEXT("#"), Linear));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIValueFormatShapeMismatchTest,
	"DreamGUI.Text.AWrongShapedLiteralIsRefusedRatherThanCoerced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIValueFormatShapeMismatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIValueFormatTestLocal;

	const FProperty* Vector2DProperty = Prop(TEXT("Vector2D"));
	const FProperty* MarginProperty = Prop(TEXT("Margin"));
	const FProperty* LinearColorProperty = Prop(TEXT("LinearColor"));
	const FProperty* VectorProperty = Prop(TEXT("Vector"));
	if (!TestNotNull(TEXT("the fixture declares a vector short form"), Vector2DProperty)
		|| !TestNotNull(TEXT("a margin"), MarginProperty)
		|| !TestNotNull(TEXT("a linear colour"), LinearColorProperty)
		|| !TestNotNull(TEXT("and something outside the table"), VectorProperty))
	{
		return false;
	}

	// The sentinel is the assertion as much as the return value is. A refusal that has already written
	// half a value into the destination gives the caller an error to report and a wrong value in the
	// tree, which is the worst of both.
	const FVector2D Sentinel(-11.0, -22.0);
	FVector2D Destination = Sentinel;
	auto RefusedAndUntouched = [this, Vector2DProperty, &Destination, Sentinel](const TCHAR* InWhat, const FDreamUIValue& InValue)
	{
		Destination = Sentinel;
		TestFalse(InWhat, DreamUIValueFormat::Parse(Vector2DProperty, InValue, &Destination));
		TestTrue(*FString::Printf(TEXT("%s, without writing anything"), InWhat),
			Destination.X == Sentinel.X && Destination.Y == Sentinel.Y);
	};

	RefusedAndUntouched(TEXT("a one-element tuple is not a vector"), MakeTupleValue({ TEXT("400") }));
	RefusedAndUntouched(TEXT("nor a three-element one"), MakeTupleValue({ TEXT("400"), TEXT("240"), TEXT("0") }));
	RefusedAndUntouched(TEXT("nor a five-element one"),
		MakeTupleValue({ TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5") }));
	RefusedAndUntouched(TEXT("nor an empty one"), MakeTupleValue(TArray<FString>()));
	RefusedAndUntouched(TEXT("a hex colour is not a vector"), MakeHexValue(TEXT("1E1E1E")));
	RefusedAndUntouched(TEXT("nor is a bare number"), MakeSimpleValue(EDreamUIValueKind::Number, TEXT("400")));
	RefusedAndUntouched(TEXT("nor an identifier"), MakeSimpleValue(EDreamUIValueKind::Identifier, TEXT("Left")));
	RefusedAndUntouched(TEXT("nor a string"), MakeSimpleValue(EDreamUIValueKind::String, TEXT("400, 240")));
	RefusedAndUntouched(TEXT("nor an asset path"), MakeSimpleValue(EDreamUIValueKind::AssetPath, TEXT("/Game/UI/F_Body")));

	// A word where a number belongs. FCString::Atod answers 0 for these, so without the check they
	// would land as (400, 0) and lay the widget out in the wrong place with no diagnostic at all.
	RefusedAndUntouched(TEXT("a word where a number belongs"), MakeTupleValue({ TEXT("400"), TEXT("wide") }));
	RefusedAndUntouched(TEXT("an empty element"), MakeTupleValue({ TEXT("400"), TEXT("") }));
	RefusedAndUntouched(TEXT("two decimal points"), MakeTupleValue({ TEXT("400"), TEXT("1.2.3") }));
	RefusedAndUntouched(TEXT("a lone minus"), MakeTupleValue({ TEXT("400"), TEXT("-") }));
	RefusedAndUntouched(TEXT("a lone dot"), MakeTupleValue({ TEXT("400"), TEXT(".") }));
	RefusedAndUntouched(TEXT("an exponent with no digits"), MakeTupleValue({ TEXT("400"), TEXT("1e") }));
	RefusedAndUntouched(TEXT("a trailing unit"), MakeTupleValue({ TEXT("400"), TEXT("240px") }));
	RefusedAndUntouched(TEXT("hex where decimal belongs"), MakeTupleValue({ TEXT("400"), TEXT("0x10") }));

	// The forms the printer emits, which therefore must read: exponent notation for very small
	// components, and a leading '+' that no printer writes but an author might.
	{
		FVector2D Parsed(FVector2D::ZeroVector);
		TestTrue(TEXT("exponent notation reads"),
			DreamUIValueFormat::Parse(Vector2DProperty, MakeTupleValue({ TEXT("1e-300"), TEXT("-2.5E+8") }), &Parsed));
		TestTrue(TEXT("as the numbers it spells"), Parsed.X == 1.0e-300 && Parsed.Y == -2.5e8);
		TestTrue(TEXT("and so does an explicit plus"),
			DreamUIValueFormat::Parse(Vector2DProperty, MakeTupleValue({ TEXT("+400"), TEXT("+0.5") }), &Parsed));
		TestTrue(TEXT("as the same numbers"), Parsed.X == 400.0 && Parsed.Y == 0.5);
	}

	// A margin is four, and only four. FMargin's own ImportText also takes one and two elements, and
	// accepting those here would give the file a second spelling that the printer never writes -- so
	// every designer touch would rewrite `Padding = 8` into `(8, 8, 8, 8)` in the author's diff.
	{
		FMargin MarginDestination(-1.0f);
		TestFalse(TEXT("a margin does not take a single element"),
			DreamUIValueFormat::Parse(MarginProperty, MakeTupleValue({ TEXT("8") }), &MarginDestination));
		TestFalse(TEXT("nor two"),
			DreamUIValueFormat::Parse(MarginProperty, MakeTupleValue({ TEXT("8"), TEXT("4") }), &MarginDestination));
		TestFalse(TEXT("nor three"),
			DreamUIValueFormat::Parse(MarginProperty, MakeTupleValue({ TEXT("8"), TEXT("4"), TEXT("8") }), &MarginDestination));
		TestFalse(TEXT("nor a hex colour"),
			DreamUIValueFormat::Parse(MarginProperty, MakeHexValue(TEXT("1E1E1E")), &MarginDestination));
		TestTrue(TEXT("and none of that touched the destination"), MarginDestination.Left == -1.0f);
	}

	// A colour takes a hex literal and nothing else.
	{
		FLinearColor ColorDestination = FLinearColor::Green;
		TestFalse(TEXT("a tuple is not a colour"),
			DreamUIValueFormat::Parse(LinearColorProperty, MakeTupleValue({ TEXT("1"), TEXT("1"), TEXT("1"), TEXT("1") }), &ColorDestination));
		TestFalse(TEXT("nor is a number"),
			DreamUIValueFormat::Parse(LinearColorProperty, MakeSimpleValue(EDreamUIValueKind::Number, TEXT("1")), &ColorDestination));
		TestFalse(TEXT("nor an identifier"),
			DreamUIValueFormat::Parse(LinearColorProperty, MakeSimpleValue(EDreamUIValueKind::Identifier, TEXT("White")), &ColorDestination));
		TestFalse(TEXT("nor a quoted string of digits"),
			DreamUIValueFormat::Parse(LinearColorProperty, MakeSimpleValue(EDreamUIValueKind::String, TEXT("1E1E1E")), &ColorDestination));
		TestTrue(TEXT("and none of that touched the destination"), ColorDestination == FLinearColor::Green);
	}

	// Nothing outside the table parses here at all, whatever shape it is handed: those go to
	// ImportText, and answering for them would mean this file quietly owning a fourth format.
	{
		FVector VectorDestination(1.0, 2.0, 3.0);
		TestFalse(TEXT("a property outside the table takes no short form"),
			DreamUIValueFormat::Parse(VectorProperty, MakeTupleValue({ TEXT("1"), TEXT("2"), TEXT("3") }), &VectorDestination));
		TestFalse(TEXT("and neither does no property at all"),
			DreamUIValueFormat::Parse(nullptr, MakeTupleValue({ TEXT("1"), TEXT("2") }), &VectorDestination));
		TestTrue(TEXT("with the destination untouched"), VectorDestination.X == 1.0);
	}

	return true;
}

#endif
