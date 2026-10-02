// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamFontFaceResolver.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUITextData.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

namespace DreamFontFaceResolverLocal
{
	/** How a face's cultures stand to a cluster's language: the groups the fallbacks are tried in. */
	enum class ECultureFit : uint8
	{
		/** The face names cultures, and none of them is one of the language's names. */
		Mismatched,
		/** The face names no culture: it is meant for any language. */
		AnyLanguage,
		/** One of the face's cultures is one of the language's names. */
		Matched,
	};

	ECultureFit GetCultureFit(const FDreamFontFaceInfo* Info, TConstArrayView<FString> Cultures)
	{
		if (Info == nullptr || Info->Cultures.Num() == 0)
		{
			return ECultureFit::AnyLanguage;
		}
		for (const FString& FaceCulture : Info->Cultures)
		{
			for (const FString& Culture : Cultures)
			{
				if (FaceCulture.Equals(Culture, ESearchCase::IgnoreCase))
				{
					return ECultureFit::Matched;
				}
			}
		}
		return ECultureFit::Mismatched;
	}

	/** Whether a face may draw a cluster with this base: one of its ranges holds it, or it has none. */
	bool FaceTakesBase(const FDreamFontFaceInfo* Info, uint32 Base)
	{
		if (Info == nullptr || Info->Ranges.Num() == 0)
		{
			return true;
		}
		const int32 Codepoint = (int32)FMath::Min<uint32>(Base, (uint32)MAX_int32);
		for (const FInt32Interval& Range : Info->Ranges)
		{
			if (Range.Contains(Codepoint))
			{
				return true;
			}
		}
		return false;
	}

	/** The language part of a culture name: "zh" of "zh-Hans-CN", and of "zh_CN". */
	FString GetLanguageSubtag(const FString& CultureName)
	{
		for (int32 Index = 0; Index < CultureName.Len(); Index++)
		{
			if (CultureName[Index] == TEXT('-') || CultureName[Index] == TEXT('_'))
			{
				return CultureName.Left(Index);
			}
		}
		return CultureName;
	}

	TArray<FString> ComputePrioritizedCultureNames(const FString& CultureName)
	{
		TArray<FString> Names = FInternationalization::Get().GetPrioritizedCultureNames(CultureName);
		// A culture the engine has no data for comes back as its last resort, English alone. In a game cooked with only some
		// cultures' data that would match Japanese text to an "en" fallback and to no "ja" one: the name itself and its
		// language say more.
		const FString Language = GetLanguageSubtag(CultureName);
		if (Names.Num() == 1 && Names[0].Equals(TEXT("en"), ESearchCase::IgnoreCase) && !Language.Equals(TEXT("en"), ESearchCase::IgnoreCase))
		{
			Names.Reset();
			Names.Add(CultureName);
			if (!Language.IsEmpty() && !Language.Equals(CultureName, ESearchCase::IgnoreCase))
			{
				Names.Add(Language);
			}
		}
		return Names;
	}

	FDreamFontFaceChoice MakeChoice(int32 FaceIndex, bool bCoversCluster, bool bCoversBase, bool bColor)
	{
		FDreamFontFaceChoice Choice;
		Choice.FaceIndex = FaceIndex;
		Choice.bCoversCluster = bCoversCluster;
		Choice.bCoversBase = bCoversBase;
		Choice.bColor = bColor;
		return Choice;
	}
}

FDreamTextLanguage FDreamTextLanguage::Make(const FString& InCultureName)
{
	FDreamTextLanguage Language;
	Language.Name = InCultureName;
	if (Language.Name.IsEmpty())
	{
		Language.Name = FInternationalization::Get().GetCurrentLanguage()->GetName();
	}
	if (Language.Name.IsEmpty())
	{
		return Language;
	}
	// A layout makes its languages every time it runs, and the engine works the names out from its culture data, so they
	// are kept by name on the game thread. What the engine answers for a name does not change while it runs.
	if (IsInGameThread())
	{
		static TMap<FString, TArray<FString>> NamesByCulture;
		if (const TArray<FString>* Known = NamesByCulture.Find(Language.Name))
		{
			Language.PrioritizedCultureNames = *Known;
			return Language;
		}
		Language.PrioritizedCultureNames = DreamFontFaceResolverLocal::ComputePrioritizedCultureNames(Language.Name);
		// A text can name any culture in a <lang> tag; the names kept stay few.
		if (NamesByCulture.Num() >= 64)
		{
			NamesByCulture.Reset();
		}
		NamesByCulture.Add(Language.Name, Language.PrioritizedCultureNames);
		return Language;
	}
	Language.PrioritizedCultureNames = DreamFontFaceResolverLocal::ComputePrioritizedCultureNames(Language.Name);
	return Language;
}

FDreamFontFaceChoice FDreamFontFaceResolver::Resolve(const FDreamFontFaceTable& Table, int32 FaceCount, const FDreamFontFaceQuery& Query,
	TFunctionRef<bool(int32 FaceIndex, uint32 Codepoint)> HasCodepoint, TFunctionRef<bool(int32 FaceIndex)> IsColorFace)
{
	if (Query.Cluster.Num() == 0)
	{
		return FDreamFontFaceChoice();
	}
	FaceCount = FMath::Max(FaceCount, 1);
	const uint32 Base = Query.Cluster[0];
	// Only the regular faces are described by the table; a style face is past it and takes the defaults.
	auto InfoOf = [&Table, FaceCount](int32 FaceIndex) -> const FDreamFontFaceInfo*
	{
		return FaceIndex < FaceCount && Table.Faces.IsValidIndex(FaceIndex) ? &Table.Faces[FaceIndex] : nullptr;
	};

	// The candidates in the order the rules give them, never a face whose ranges leave the base out.
	TArray<int32, TInlineAllocator<16>> Candidates;
	if (Query.StyledFace > 0 && DreamFontFaceResolverLocal::FaceTakesBase(InfoOf(Query.StyledFace), Base))
	{
		Candidates.Add(Query.StyledFace);
	}
	// Fallbacks that win over the primary face for their ranges in their languages: those naming the language first, then
	// those meant for any language (Slate's sub-fonts, in Slate's order).
	for (const DreamFontFaceResolverLocal::ECultureFit Fit : { DreamFontFaceResolverLocal::ECultureFit::Matched, DreamFontFaceResolverLocal::ECultureFit::AnyLanguage })
	{
		for (int32 FaceIndex = 1; FaceIndex < FaceCount; FaceIndex++)
		{
			const FDreamFontFaceInfo* Info = InfoOf(FaceIndex);
			if (Info != nullptr && Info->bPreferOverPrimary && DreamFontFaceResolverLocal::GetCultureFit(Info, Query.Cultures) == Fit && DreamFontFaceResolverLocal::FaceTakesBase(Info, Base))
			{
				Candidates.AddUnique(FaceIndex);
			}
		}
	}
	Candidates.AddUnique(0);
	// The other fallbacks: those naming the language, those for any language, then -- a last resort before a missing-glyph
	// box, where Slate would drop them -- those for other languages.
	for (const DreamFontFaceResolverLocal::ECultureFit Fit : { DreamFontFaceResolverLocal::ECultureFit::Matched, DreamFontFaceResolverLocal::ECultureFit::AnyLanguage, DreamFontFaceResolverLocal::ECultureFit::Mismatched })
	{
		for (int32 FaceIndex = 1; FaceIndex < FaceCount; FaceIndex++)
		{
			const FDreamFontFaceInfo* Info = InfoOf(FaceIndex);
			if (DreamFontFaceResolverLocal::GetCultureFit(Info, Query.Cultures) == Fit && DreamFontFaceResolverLocal::FaceTakesBase(Info, Base))
			{
				Candidates.AddUnique(FaceIndex);
			}
		}
	}

	// Presentation decides where the colour faces stand, the order among the colour faces and among the others kept: an
	// emoji tries them first (unless the font says otherwise), text tries them last.
	enum class EColorOrder : uint8 { AsListed, ColorFirst, ColorLast };
	const EColorOrder Order = Query.Presentation == EDreamTextPresentation::Text ? EColorOrder::ColorLast
		: (Table.bPreferColorEmoji ? EColorOrder::ColorFirst : EColorOrder::AsListed);
	const int32 PreferredKind = Order == EColorOrder::ColorFirst ? 1 : 0;
	const int32 OtherKind = 1 - PreferredKind;
	const bool bBaseIgnorable = IsDefaultIgnorable(Base);
	auto HasRest = [&Query, &HasCodepoint](int32 FaceIndex)
	{
		for (int32 Index = 1; Index < Query.Cluster.Num(); Index++)
		{
			const uint32 Codepoint = Query.Cluster[Index];
			if (!IsDefaultIgnorable(Codepoint) && !HasCodepoint(FaceIndex, Codepoint))
			{
				return false;
			}
		}
		return true;
	};

	// One walk in candidate order. Whether a face is a colour face is asked only of a face that has the base -- one whose
	// coverage was asked anyway -- so no fallback is loaded sooner than it would be for its coverage. Per kind (0 the
	// monochrome faces, 1 the colour ones): the first face with the whole cluster, and the first with only its base.
	int32 FirstWhole[2] = { INDEX_NONE, INDEX_NONE };
	int32 FirstBase[2] = { INDEX_NONE, INDEX_NONE };
	for (const int32 FaceIndex : Candidates)
	{
		if (!bBaseIgnorable && !HasCodepoint(FaceIndex, Base))
		{
			continue;
		}
		const bool bWhole = HasRest(FaceIndex);
		if (Order == EColorOrder::AsListed)
		{
			if (bWhole)
			{
				return DreamFontFaceResolverLocal::MakeChoice(FaceIndex, true, true, IsColorFace(FaceIndex));
			}
			if (FirstBase[0] == INDEX_NONE)
			{
				FirstBase[0] = FaceIndex;
			}
			continue;
		}
		const int32 Kind = IsColorFace(FaceIndex) ? 1 : 0;
		if (bWhole)
		{
			// Nothing before it in the reordered list had the whole cluster.
			if (Kind == PreferredKind)
			{
				return DreamFontFaceResolverLocal::MakeChoice(FaceIndex, true, true, Kind == 1);
			}
			if (FirstWhole[Kind] == INDEX_NONE)
			{
				FirstWhole[Kind] = FaceIndex;
			}
		}
		else if (FirstBase[Kind] == INDEX_NONE)
		{
			FirstBase[Kind] = FaceIndex;
		}
	}

	if (Order == EColorOrder::AsListed)
	{
		if (FirstBase[0] != INDEX_NONE)
		{
			return DreamFontFaceResolverLocal::MakeChoice(FirstBase[0], false, true, IsColorFace(FirstBase[0]));
		}
	}
	else
	{
		if (FirstWhole[OtherKind] != INDEX_NONE)
		{
			return DreamFontFaceResolverLocal::MakeChoice(FirstWhole[OtherKind], true, true, OtherKind == 1);
		}
		// No face has the whole cluster: the first with its base, so the character itself is never a box.
		if (FirstBase[PreferredKind] != INDEX_NONE)
		{
			return DreamFontFaceResolverLocal::MakeChoice(FirstBase[PreferredKind], false, true, PreferredKind == 1);
		}
		if (FirstBase[OtherKind] != INDEX_NONE)
		{
			return DreamFontFaceResolverLocal::MakeChoice(FirstBase[OtherKind], false, true, OtherKind == 1);
		}
	}
	// Nobody has it: the primary face's .notdef.
	return DreamFontFaceResolverLocal::MakeChoice(0, false, false, IsColorFace(0));
}

FDreamFontFaceChoice FDreamFontFaceResolver::Resolve(UDreamUIFontData_BaseObject* Font, const FDreamFontFaceQuery& Query)
{
	if (Font == nullptr)
	{
		return FDreamFontFaceChoice();
	}
	return Resolve(Font->GetFaceTable(), Font->GetFaceCount(), Query,
		[Font](int32 FaceIndex, uint32 Codepoint) { return Font->FaceHasCodepoint(FaceIndex, Codepoint); },
		[Font](int32 FaceIndex) { return Font->IsColorFace(FaceIndex); });
}

bool FDreamFontFaceResolver::IsDefaultIgnorable(uint32 Codepoint)
{
	return Codepoint == 0x200D
		|| (Codepoint >= 0xFE00 && Codepoint <= 0xFE0F)
		|| (Codepoint >= 0xE0020 && Codepoint <= 0xE007F)
		|| (Codepoint >= 0xE0100 && Codepoint <= 0xE01EF);
}

EDreamTextPresentation FDreamFontFaceResolver::GetPresentation(TConstArrayView<uint32> Cluster)
{
	if (Cluster.Num() == 0)
	{
		return EDreamTextPresentation::Text;
	}
	// U+FE0E after the base asks for text, whatever else the cluster holds -- a keycap too.
	if (Cluster.Num() > 1 && Cluster[1] == FDreamUIText_CodePoint::UNICODE_VS_BLACK)
	{
		return EDreamTextPresentation::Text;
	}
	int32 RegionalIndicators = 0;
	for (const uint32 Codepoint : Cluster)
	{
		if (Codepoint == FDreamUIText_CodePoint::UNICODE_VS_COLOR
			|| Codepoint == FDreamUIText_CodePoint::UNICODE_COMBINING_ENCLOSING_KEYCAP
			|| Codepoint == FDreamUIText_CodePoint::UNICODE_ZWJ
			|| FDreamUIText_CodePoint::IsSkinToneModifier(Codepoint)
			|| FDreamUIText_CodePoint::IsTagCharacter(Codepoint))
		{
			return EDreamTextPresentation::Emoji;
		}
		if (FDreamUIText_CodePoint::IsRegionalIndicator(Codepoint))
		{
			RegionalIndicators++;
		}
	}
	if (RegionalIndicators >= 2 || FDreamUIText_CodePoint::HasEmojiPresentation(Cluster[0]))
	{
		return EDreamTextPresentation::Emoji;
	}
	return EDreamTextPresentation::Text;
}
