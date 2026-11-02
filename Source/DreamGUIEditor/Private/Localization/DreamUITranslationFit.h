// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidgetTree;
struct FDreamUIDiagnosticBag;
struct FDreamUILocalization;

/**
 * A translation laid out in the text it is for: whether it fits the text's box, and whether its font has every
 * character. What the designer says as it compiles (DUI6024 / DUI6025) and what `dloc check` asks DreamGUI for
 * (DLOC4001 / DLOC4002) when the project has DreamLoc.
 */
namespace DreamUITranslationFit
{
	struct FFit
	{
		bool bMeasured = false;
		bool bOverflows = false;
		FVector2f Needed = FVector2f::ZeroVector;
		FVector2f Box = FVector2f::ZeroVector;
		TArray<uint32> MissingGlyphs;
		/** `'Apply' (DefaultFont_DistanceField, 20)`. */
		FString Where;
	};

	/**
	 * InText laid out in the DreamText of node InNodePath (`Footer/Apply`; the last id names it) of InTree, when that text's
	 * own FText is InNamespace / InKey -- so a key on another property (a tooltip, an accessible text) is not measured
	 * against a box it is not shown in. False with nothing measured when there is no such text.
	 */
	bool Measure(const UDreamWidgetTree* InTree, const FString& InNodePath, const FString& InNamespace, const FString& InKey,
		const FString& InText, const FString& InCulture, FFit& OutFit);

	/**
	 * Every language's translation of the compile's texts, laid out in the tree the compile made: a warning for each that
	 * does not fit (DUI6024) or has characters its font lacks (DUI6025). Does nothing without DreamLoc.
	 */
	void ReportTranslations(const UDreamWidgetTree* InTree, const FDreamUILocalization& InLocalization, FDreamUIDiagnosticBag& OutDiagnostics);

	/** DreamGUI as DreamLoc's IDreamLocContextProvider for .dui sources, when the project has DreamLoc. */
	void RegisterWithDreamLoc();
	void UnregisterFromDreamLoc();
}
