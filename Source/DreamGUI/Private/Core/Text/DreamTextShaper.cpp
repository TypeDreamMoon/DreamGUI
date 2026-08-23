// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextShaper.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Internationalization/Text.h"

#if WITH_HARFBUZZ
#include "hb.h"
#endif

bool FDreamTextShaper::CanShape(UDreamUIFontData_BaseObject* Font)
{
#if WITH_HARFBUZZ
	return Font != nullptr && Font->GetShapingFont(0, 16.0f) != nullptr;
#else
	return false;
#endif
}

#if WITH_HARFBUZZ
namespace DreamTextShaperLocal
{
	struct FItem
	{
		bool bRightToLeft = false;
		hb_script_t Script = HB_SCRIPT_COMMON;
		int32 FaceIndex = 0;
		float Size = 0.0f;
		bool bBold = false;
		bool bUnshaped = false;

		bool SameRun(const FItem& Other) const
		{
			return bRightToLeft == Other.bRightToLeft && Script == Other.Script && FaceIndex == Other.FaceIndex
				&& Size == Other.Size && bBold == Other.bBold && bUnshaped == Other.bUnshaped;
		}
	};

	bool ScriptIsNeutral(hb_script_t Script)
	{
		return Script == HB_SCRIPT_COMMON || Script == HB_SCRIPT_INHERITED || Script == HB_SCRIPT_UNKNOWN;
	}

	/** Direction per element, from the engine's bidi over the paragraph as UTF-16. */
	void ResolveDirections(const TArray<FDreamShapeElement>& Elements, TArray<bool>& OutRightToLeft, bool& OutBaseRightToLeft)
	{
		FString Plain;
		TArray<int32> PlainStart;
		PlainStart.SetNumUninitialized(Elements.Num());
		for (int32 i = 0; i < Elements.Num(); i++)
		{
			PlainStart[i] = Plain.Len();
			const uint32 C = Elements[i].Codepoint;
			if (C >= 0x10000)
			{
				const uint32 V = C - 0x10000;
				Plain.AppendChar((TCHAR)(0xD800 + (V >> 10)));
				Plain.AppendChar((TCHAR)(0xDC00 + (V & 0x3FF)));
			}
			else
			{
				Plain.AppendChar((TCHAR)C);
			}
		}

		OutRightToLeft.Init(false, Elements.Num());
		const TextBiDi::ETextDirection Base = TextBiDi::ComputeBaseDirection(Plain);
		OutBaseRightToLeft = Base == TextBiDi::ETextDirection::RightToLeft;
		TArray<TextBiDi::FTextDirectionInfo> Infos;
		TextBiDi::ComputeTextDirection(Plain, OutBaseRightToLeft ? TextBiDi::ETextDirection::RightToLeft : TextBiDi::ETextDirection::LeftToRight, Infos);
		// The engine reports the runs in VISUAL order (ubidi_getVisualRun), so each one is mapped
		// back onto the elements by index rather than walked in sequence.
		TArray<int32> ElementAtPlain;
		ElementAtPlain.SetNumUninitialized(Plain.Len());
		for (int32 i = 0; i < Elements.Num(); i++)
		{
			const int32 End = i + 1 < Elements.Num() ? PlainStart[i + 1] : Plain.Len();
			for (int32 p = PlainStart[i]; p < End; p++)
			{
				ElementAtPlain[p] = i;
			}
		}
		for (const TextBiDi::FTextDirectionInfo& Info : Infos)
		{
			const bool bRTL = Info.TextDirection == TextBiDi::ETextDirection::RightToLeft;
			const int32 End = FMath::Min(Info.StartIndex + Info.Length, Plain.Len());
			for (int32 p = FMath::Max(0, Info.StartIndex); p < End; p++)
			{
				OutRightToLeft[ElementAtPlain[p]] = bRTL;
			}
		}
	}
}
#endif

bool FDreamTextShaper::ShapeParagraph(const TArray<FDreamShapeElement>& Elements, UDreamUIFontData_BaseObject* Font, bool bUseKerning, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft)
{
	OutRuns.Reset();
	OutBaseRightToLeft = false;
#if !WITH_HARFBUZZ
	return false;
#else
	using namespace DreamTextShaperLocal;
	if (!CanShape(Font) || Elements.Num() == 0)
	{
		return false;
	}

	// Itemize: direction, script, face, style, per element.
	TArray<bool> RightToLeft;
	bool bBaseRightToLeft = false;
	ResolveDirections(Elements, RightToLeft, bBaseRightToLeft);
	OutBaseRightToLeft = bBaseRightToLeft;

	hb_unicode_funcs_t* Unicode = hb_unicode_funcs_get_default();
	const int32 FaceCount = Font->GetFaceCount();
	TArray<FItem> Items;
	Items.SetNum(Elements.Num());
	hb_script_t LastScript = HB_SCRIPT_COMMON;
	int32 LastFace = 0;
	for (int32 i = 0; i < Elements.Num(); i++)
	{
		const FDreamShapeElement& E = Elements[i];
		FItem& Item = Items[i];
		Item.bRightToLeft = RightToLeft[i];
		Item.Size = E.Size;
		Item.bBold = E.bBold;
		Item.bUnshaped = E.bUnshaped;
		if (E.bUnshaped)
		{
			continue;
		}
		const uint32 C = E.Codepoint == '\t' ? (uint32)' ' : E.Codepoint;
		// Neutral characters (punctuation, spaces, marks) take the script of what came before them,
		// so a run is not cut on every comma.
		hb_script_t Script = hb_unicode_script(Unicode, C);
		if (ScriptIsNeutral(Script))
		{
			Script = LastScript;
		}
		else
		{
			LastScript = Script;
		}
		Item.Script = Script;
		// Face: the first face that can draw it, primary first -- except that neutral characters
		// (spaces, punctuation, marks) stay with the face of what came before them, so a run is not
		// cut around every space, the way a browser's font fallback segments text.
		const bool bNeutral = ScriptIsNeutral(hb_unicode_script(Unicode, C));
		int32 FaceIndex = -1;
		if (bNeutral && Font->FaceHasCodepoint(LastFace, C))
		{
			FaceIndex = LastFace;
		}
		else
		{
			for (int32 F = 0; F < FaceCount; F++)
			{
				if (Font->FaceHasCodepoint(F, C))
				{
					FaceIndex = F;
					break;
				}
			}
		}
		if (FaceIndex < 0)
		{
			FaceIndex = 0;//nobody has it: the primary face's .notdef
		}
		Item.FaceIndex = FaceIndex;
		LastFace = FaceIndex;
	}
	// Leading neutrals before the first scripted character take that script.
	for (int32 i = 0; i < Items.Num(); i++)
	{
		if (Items[i].bUnshaped)continue;
		if (!ScriptIsNeutral(Items[i].Script))
		{
			for (int32 j = 0; j < i; j++)
			{
				if (!Items[j].bUnshaped && ScriptIsNeutral(Items[j].Script))Items[j].Script = Items[i].Script;
			}
			break;
		}
	}

	// Cut runs and shape each.
	const float BoldRatio = Font->GetBoldRatio();
	hb_buffer_t* Buffer = hb_buffer_create();
	TArray<hb_codepoint_t> Codepoints;
	int32 RunStart = 0;
	while (RunStart < Elements.Num())
	{
		int32 RunEnd = RunStart + 1;
		while (RunEnd < Elements.Num() && Items[RunEnd].SameRun(Items[RunStart]))
		{
			RunEnd++;
		}
		const FItem& Item = Items[RunStart];
		if (!Item.bUnshaped)
		{
			FDreamShapedRun Run;
			Run.ElementStart = RunStart;
			Run.ElementEnd = RunEnd;
			Run.bRightToLeft = Item.bRightToLeft;
			Run.FaceIndex = Item.FaceIndex;
			Run.Size = Item.Size;
			Run.bBold = Item.bBold;

			hb_font_t* HBFont = static_cast<hb_font_t*>(Font->GetShapingFont(Item.FaceIndex, Item.Size));
			if (HBFont == nullptr)
			{
				HBFont = static_cast<hb_font_t*>(Font->GetShapingFont(0, Item.Size));
			}
			if (HBFont != nullptr)
			{
				Codepoints.Reset(RunEnd - RunStart);
				for (int32 i = RunStart; i < RunEnd; i++)
				{
					const uint32 C = Elements[i].Codepoint;
					Codepoints.Add(C == '\t' ? (hb_codepoint_t)' ' : (hb_codepoint_t)C);
				}
				hb_buffer_clear_contents(Buffer);
				hb_buffer_set_direction(Buffer, Item.bRightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
				hb_buffer_set_script(Buffer, Item.Script);
				hb_buffer_set_language(Buffer, hb_language_get_default());
				hb_buffer_set_cluster_level(Buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
				// Clusters are indices into this array, so a glyph's cluster + RunStart is its element.
				hb_buffer_add_codepoints(Buffer, Codepoints.GetData(), Codepoints.Num(), 0, Codepoints.Num());

				// Ligatures stay off for now so a code point keeps its own glyph, which is what the
				// caret and per-character animation contracts assume until they learn about clusters.
				hb_feature_t Features[3];
				Features[0] = { HB_TAG('k','e','r','n'), bUseKerning ? 1u : 0u, 0, (unsigned int)-1 };
				Features[1] = { HB_TAG('l','i','g','a'), 0u, 0, (unsigned int)-1 };
				Features[2] = { HB_TAG('c','l','i','g'), 0u, 0, (unsigned int)-1 };
				hb_shape(HBFont, Buffer, Features, 3);

				unsigned int GlyphCount = 0;
				hb_glyph_info_t* Infos = hb_buffer_get_glyph_infos(Buffer, &GlyphCount);
				hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, &GlyphCount);
				const float BoldAdvance = Item.bBold ? Item.Size * BoldRatio : 0.0f;
				Run.Glyphs.Reserve(GlyphCount);
				for (unsigned int g = 0; g < GlyphCount; g++)
				{
					FDreamShapedGlyph Glyph;
					Glyph.FaceIndex = Item.FaceIndex;
					Glyph.GlyphIndex = Infos[g].codepoint;
					Glyph.ElementIndex = RunStart + (int32)Infos[g].cluster;
					Glyph.XAdvance = Positions[g].x_advance / 64.0f + BoldAdvance;
					Glyph.YAdvance = Positions[g].y_advance / 64.0f;
					Glyph.XOffset = Positions[g].x_offset / 64.0f;
					Glyph.YOffset = Positions[g].y_offset / 64.0f;
					Run.Glyphs.Add(Glyph);
				}
			}
			OutRuns.Add(MoveTemp(Run));
		}
		RunStart = RunEnd;
	}
	hb_buffer_destroy(Buffer);
	return true;
#endif
}
