// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Math/Interval.h"

class UDreamUIFontData_BaseObject;

/**
 * Which face of a font draws a grapheme cluster: a pure function of the font's face table, the cluster's code points,
 * the style it asks for, its language and its presentation. The shaper itemizes with it, the layout asks it before it
 * decides how an emoji is drawn, the FreeType font resolves code points with it on the path that does not shape, and
 * the font editor's "resolve sample" row shows what it answers. Nothing here touches FreeType: what a face has comes
 * through the two callbacks, so the rules can be tested with a table and a lambda.
 */

/** Which form a cluster asks to be drawn in: the emoji (colour) form or the text (monochrome) form. */
enum class EDreamTextPresentation : uint8
{
	Text,
	Emoji,
};

/**
 * A language as face resolution and shaping see it. Made once per language a layout uses (the text's own, and each
 * rich-text <lang=xx> it meets), and indexed by FDreamShapeElement::LanguageIndex.
 */
struct DREAMGUI_API FDreamTextLanguage
{
	/** The culture name as given ("ja", "zh-Hans", "en-US"). */
	FString Name;
	/**
	 * Name and the names it falls back to, most specific first, as FInternationalization::GetPrioritizedCultureNames gives
	 * them ("zh-Hans-CN", "zh-CN", "zh-Hans", "zh"): what a fallback entry's cultures are matched against.
	 */
	TArray<FString> PrioritizedCultureNames;

	/** The language InCultureName names; the game's current language (FInternationalization::GetCurrentLanguage) when it is empty. */
	static FDreamTextLanguage Make(const FString& InCultureName);
};

/** What the resolver knows about one regular face of a font: its fallback entry's settings (FDreamUIFontFallback). */
struct FDreamFontFaceInfo
{
	/** Code points the face may be used for, inclusive; empty means every code point. Matched against a cluster's base. */
	TArray<FInt32Interval> Ranges;
	/** Cultures the face is meant for ("ja", "zh-Hans"), already split; empty means any language. */
	TArray<FString> Cultures;
	/**
	 * Glyph and metric scale, CSS size-adjust: a run drawn from this face is shaped and rasterized at its style size times
	 * this, and its line box is measured at that size too, so a scaled face grows the line (Chrome's behaviour, not Slate's).
	 */
	float Scale = 1.0f;
	/**
	 * Tried before face 0 for code points in Ranges when its cultures match the cluster's language, or when it has none and so
	 * is meant for any language (Slate's sub-fonts, culture-matched ones first).
	 */
	bool bPreferOverPrimary = false;
};

/**
 * A font's faces as the resolver sees them (UDreamUIFontData_BaseObject::GetFaceTable). Faces[i] describes face index i;
 * a face index past the end -- every face of a font that keeps no table -- has the defaults: every code point, any
 * language, scale 1, not preferred. Face 0 is the font itself; its entry's Ranges, Cultures and bPreferOverPrimary are
 * never read. Style faces (bold, italic, bold-italic, past GetFaceCount) are not described here: they are only ever
 * tried as FDreamFontFaceQuery::StyledFace, and scale 1.
 */
struct FDreamFontFaceTable
{
	TArray<FDreamFontFaceInfo> Faces;
	/** Clusters in emoji presentation try the colour faces first (FreeType's FT_HAS_COLOR); off, they take the faces in order. */
	bool bPreferColorEmoji = true;

	/** The face's scale; 1 for a face the table does not describe. */
	float GetScale(int32 FaceIndex) const { return Faces.IsValidIndex(FaceIndex) ? Faces[FaceIndex].Scale : 1.0f; }
};

/** One cluster to resolve. */
struct FDreamFontFaceQuery
{
	/** The cluster's code points in logical order, [0] its base; an emoji sequence whole. Default-ignorables are skipped for coverage. */
	TConstArrayView<uint32> Cluster;
	/** The font's face for the cluster's bold/italic style (UDreamUIFontData_BaseObject::GetStyledFace), tried first; 0 for none. */
	int32 StyledFace = 0;
	/** The cluster's language, FDreamTextLanguage::PrioritizedCultureNames. Empty: no face's cultures match it. */
	TConstArrayView<FString> Cultures;
	/** GetPresentation(Cluster), unless the caller knows better. */
	EDreamTextPresentation Presentation = EDreamTextPresentation::Text;
};

/** The resolver's answer. */
struct FDreamFontFaceChoice
{
	/** The face to draw the cluster from; 0 (the primary face's .notdef) when no face has even the base. */
	int32 FaceIndex = 0;
	/** The face has every code point of the cluster that is not default-ignorable. */
	bool bCoversCluster = false;
	/** The face has the cluster's base. False only when nothing had it. */
	bool bCoversBase = false;
	/** The face is a colour face (UDreamUIFontData_BaseObject::IsColorFace). */
	bool bColor = false;
};

class DREAMGUI_API FDreamFontFaceResolver
{
public:
	/**
	 * The face a cluster is drawn from. Candidates, in order, never a face whose Ranges exclude the base:
	 *   1. Query.StyledFace, when it is not 0;
	 *   2. faces with bPreferOverPrimary whose cultures match the language, then those with no cultures (any language);
	 *   3. face 0;
	 *   4. the other faces: those whose cultures match the language, then those with no cultures, then the rest.
	 * Presentation then reorders them, keeping the order within each group: Emoji (with Table.bPreferColorEmoji) puts the
	 * colour faces first; Text puts them last. The first candidate with the whole cluster wins; failing that, the first
	 * with its base; failing that, face 0. Cultures match when any of a face's cultures equals any of Query.Cultures,
	 * ignoring case. IsColorFace is asked only of a face the walk reaches, so a fallback is loaded no sooner than its
	 * coverage is asked for.
	 * @param FaceCount  The font's regular faces (UDreamUIFontData_BaseObject::GetFaceCount), style faces excluded.
	 */
	static FDreamFontFaceChoice Resolve(const FDreamFontFaceTable& Table, int32 FaceCount, const FDreamFontFaceQuery& Query,
		TFunctionRef<bool(int32 FaceIndex, uint32 Codepoint)> HasCodepoint, TFunctionRef<bool(int32 FaceIndex)> IsColorFace);
	/** The same over a font: its GetFaceTable, GetFaceCount, FaceHasCodepoint and IsColorFace. Game thread. */
	static FDreamFontFaceChoice Resolve(UDreamUIFontData_BaseObject* Font, const FDreamFontFaceQuery& Query);

	/**
	 * Code points coverage does not ask for, because they draw nothing of their own: U+200D ZWJ, the variation selectors
	 * U+FE00-FE0F and U+E0100-E01EF, and the tag characters U+E0020-E007F. A face that lacks U+FE0F still covers U+2764 U+FE0F.
	 */
	static bool IsDefaultIgnorable(uint32 Codepoint);
	/**
	 * Which form a cluster asks for: Text when U+FE0E follows its base (a keycap included); otherwise Emoji when it holds
	 * U+FE0F, a keycap mark U+20E3, two regional indicators, a skin tone modifier, a ZWJ or a tag character, or when its
	 * base has Unicode's Emoji_Presentation property (FDreamUIText_CodePoint::HasEmojiPresentation); otherwise Text.
	 */
	static EDreamTextPresentation GetPresentation(TConstArrayView<uint32> Cluster);
};
