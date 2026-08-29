// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Text/DreamUIAst.h"

class FProperty;

/**
 * The short text forms, and the only place that knows them.
 *
 * Everything else in a .dui is written with the reflected name and the reflected value, so there
 * is no table to keep in sync. Three struct types are the exception, because their ExportTextItem
 * form is what an author would actually see every day:
 *
 *   FVector2D / FVector2f      (400, 240)
 *   FLinearColor / FColor      #1E1E1E
 *   FMargin                    (8, 8, 8, 8)
 *
 * FDreamUIAnchorData is deliberately NOT here: nested structs are written with dotted paths
 * (AnchorData.SizeDelta = (400, 240)), so it never appears as a whole value -- only its leaves do.
 * That decision is what keeps this table at three entries instead of growing with every struct.
 *
 * PRINTER AND PARSER MUST BE INVERSE. A short form that parses to a slightly different value than
 * it was printed from makes the designer's write-back drift: drag an anchor, and the number in the
 * file creeps a little further from the number in the editor every time, with nothing to notice it.
 * The round-trip test over randomised values is not optional, and it is the reason both directions
 * live in one file.
 *
 * What "inverse" means per type, written down because one of the three cannot be exact and that has
 * to be a decision rather than a discovery:
 *
 *   FVector2D / FVector2f / FMargin   Exact. Parse(Print(v)) == v for every finite value.
 *   FColor                            Exact. The text IS the eight bits.
 *   FLinearColor                      Quantised once through sRGB, then stable. Parse(Print(v)) is
 *                                     the colour that hex code point names, not v; what is exact is
 *                                     Print(Parse(Print(v))) == Print(v), so a value survives the
 *                                     first write-back and every one after it unchanged. The one
 *                                     thing it cannot carry is a channel outside [0,1] -- an HDR
 *                                     tint of 2.0 comes back clamped to 1.0. DreamUIValueFormat.cpp
 *                                     explains why that is not solved by refusing the value.
 */
namespace DreamUIValueFormat
{
	/** Whether this property has a short form at all. False sends the caller to ExportTextItem. */
	DREAMGUI_API bool HasShortForm(const FProperty* InProperty);

	/**
	 * The short form of a live value, e.g. "(400, 240)".
	 *
	 * Returns false and leaves OutText untouched when the property has no short form; the caller
	 * then falls back to ExportTextItem, which is always correct and merely ugly.
	 */
	DREAMGUI_API bool Print(const FProperty* InProperty, const void* InValuePtr, FString& OutText);

	/**
	 * Read a parsed literal into a live value.
	 *
	 * Returns false when the literal's shape cannot produce this property's type -- wrong arity,
	 * a hex colour on a vector, a bare identifier where a tuple belongs. The caller raises
	 * ValueTypeMismatch or TupleArityMismatch with the value's own source location.
	 */
	DREAMGUI_API bool Parse(const FProperty* InProperty, const FDreamUIValue& InValue, void* OutValuePtr);

	/**
	 * Number of tuple elements this property's short form expects, or INDEX_NONE when it has none.
	 * Exposed so a caller can report arity before attempting the write.
	 */
	DREAMGUI_API int32 GetExpectedTupleArity(const FProperty* InProperty);

	/**
	 * "#RRGGBB" or "#RRGGBBAA" (whichever is lossless) from a linear or 8-bit colour.
	 * Exposed separately because the designer's colour picker writes colours far more often than
	 * anything else, and its round trip is the one most likely to drift.
	 *
	 * The two sides are asymmetric about the '#' because their callers are: PrintColorHex writes the
	 * text as it appears in the file, while FDreamUIValue::Raw arrives with the '#' already stripped
	 * by the lexer. ParseColorHex therefore accepts the digits either way, so that the obvious
	 * ParseColorHex(PrintColorHex(c)) is not a refusal. It reads 3, 4, 6 and 8 digits; the printer
	 * only ever writes 6 or 8.
	 */
	/**
	 * One number, spelled so that reading it back gives exactly the number that went in.
	 *
	 * Exported because the write-back layer needs the same spelling and had copied it: a designer
	 * compares "what the tree holds, printed" against "what the file says", so two printers that
	 * round differently would report a change on a value nobody touched -- every flush, forever, on
	 * whichever properties the two disagreed about. Neither module's tests can see that; they each
	 * round-trip through their own copy and pass.
	 *
	 * bSinglePrecision decides what "exactly" means: a float destination only has to survive a float
	 * round trip, and demanding double precision of it would print nine digits where two will do.
	 */
	DREAMGUI_API FString PrintScalar(double InValue, bool bSinglePrecision);

	DREAMGUI_API FString PrintColorHex(const FLinearColor& InColor);
	DREAMGUI_API bool ParseColorHex(const FString& InHexDigits, FLinearColor& OutColor);
}
