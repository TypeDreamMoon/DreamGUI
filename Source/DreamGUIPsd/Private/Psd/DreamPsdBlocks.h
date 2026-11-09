// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Psd/DreamPsdDocument.h"

/** The interpretation of tagged blocks: the reader frames them, this reads what is inside. */
namespace DreamPsdBlocks
{
	/** Whether a block's length is 8 bytes in a PSB. */
	bool HasWideLength(const FString& InKey);

	/** One block of a layer record. Unknown keys are kept in OtherBlocks. */
	void ParseLayerBlock(const FString& InKey, FDreamPsdStream& InStream, FDreamPsdLayer& InOutLayer,
		const FDreamPsdDocument& InDocument, FDreamPsdMessages& OutMessages);

	/** 'Patt', 'Pat2', 'Pat3': the patterns a document carries. */
	void ParsePatterns(FDreamPsdStream& InStream, FDreamPsdDocument& InOutDocument, FDreamPsdMessages& OutMessages);

	/** A 'Grdn' descriptor. */
	void ReadGradient(const FDreamPsdDescriptor& InDescriptor, FDreamPsdGradient& OutGradient);
	/** The layout keys of a gradient fill or overlay (Type, Angl, Scl, Rvrs, Dthr, Algn, Ofst) and its 'Grad'. */
	void ReadGradientFill(const FDreamPsdDescriptor& InDescriptor, FDreamPsdGradientFill& OutFill);
	void ReadPatternFill(const FDreamPsdDescriptor& InDescriptor, FDreamPsdPatternFill& OutFill);

	/** The effects of an 'lfx2' or 'lmfx' descriptor, the global light applied. */
	void ReadEffects(const FDreamPsdDescriptor& InDescriptor, const FDreamPsdDocument& InDocument, FDreamPsdLayerEffects& OutEffects);

	/** Fill the derived text fields (runs, box, text) from a type layer's descriptor and its engine data. */
	void ReadTextEngine(FDreamPsdText& InOutText, FDreamPsdMessages& OutMessages, const FString& InLayer);
}
