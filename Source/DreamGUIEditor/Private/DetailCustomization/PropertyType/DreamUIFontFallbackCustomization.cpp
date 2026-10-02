// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/PropertyType/DreamUIFontFallbackCustomization.h"

#include "Algo/Sort.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Fonts/UnicodeBlockRange.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Char.h"
#include "Misc/Parse.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "SCulturePicker.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamUIFontFallbackCustomization"

namespace DreamUIFontFallbackCustomizationLocal
{
	/** An inclusive code point range, for the preset and script tables below. */
	struct FCodepointRange
	{
		int32 Min;
		int32 Max;
	};

	// The presets use the bounds FUnicodeBlockRange gives each block wherever a range is a whole block, so every range a
	// preset adds reads back as its block in the rows below the entry.

	/** Han, kana, Hangul, CJK symbols and punctuation, and the halfwidth and fullwidth forms. */
	constexpr FCodepointRange CJKPresetRanges[] =
	{
		{0x4E00, 0x9FFF},	// CJK Unified Ideographs
		{0x3400, 0x4DBF},	// CJK Unified Ideographs Extension A
		{0xF900, 0xFAFF},	// CJK Compatibility Ideographs
		{0x20000, 0x2A6D6},	// CJK Unified Ideographs Extension B
		{0x3040, 0x309F},	// Hiragana
		{0x30A0, 0x30FF},	// Katakana
		{0x31F0, 0x31FF},	// Katakana Phonetic Extensions
		{0x1100, 0x11FF},	// Hangul Jamo
		{0x3130, 0x318F},	// Hangul Compatibility Jamo
		{0xAC00, 0xD7AF},	// Hangul Syllables
		{0x3000, 0x303F},	// CJK Symbols and Punctuation
		{0xFF00, 0xFFEF},	// Halfwidth and Fullwidth Forms
	};

	/**
	 * Everything an emoji cluster can start with. A face is only ever tried for a cluster whose base its ranges hold, so
	 * the keycap bases (#, * and the digits), (c), (r) and the BMP symbols that take U+FE0F are here as well as the
	 * pictograph blocks; a text-presentation cluster still tries the monochrome faces first, so a plain digit stays where
	 * it was.
	 */
	constexpr FCodepointRange EmojiPresetRanges[] =
	{
		{0x0023, 0x0023}, {0x002A, 0x002A}, {0x0030, 0x0039},
		{0x00A9, 0x00A9}, {0x00AE, 0x00AE},
		{0x203C, 0x203C}, {0x2049, 0x2049}, {0x2122, 0x2122}, {0x2139, 0x2139},
		{0x2190, 0x21FF},	// Arrows
		{0x2300, 0x23FF},	// Miscellaneous Technical
		{0x24C2, 0x24C2},
		{0x25A0, 0x25FF},	// Geometric Shapes
		{0x2600, 0x26FF},	// Miscellaneous Symbols
		{0x2700, 0x27BF},	// Dingbats
		{0x2934, 0x2935},
		{0x2B00, 0x2BFF},	// Miscellaneous Symbols and Arrows
		{0x3030, 0x3030}, {0x303D, 0x303D}, {0x3297, 0x3297}, {0x3299, 0x3299},
		{0x1F000, 0x1F02F},	// Mahjong Tiles
		{0x1F0A0, 0x1F0FF},	// Playing Cards
		{0x1F100, 0x1F1FF},	// Enclosed Alphanumeric Supplement: the regional indicators flags are made of
		{0x1F200, 0x1F2FF},	// Enclosed Ideographic Supplement
		{0x1F300, 0x1F5FF},	// Miscellaneous Symbols and Pictographs
		{0x1F600, 0x1F64F},	// Emoticons
		{0x1F680, 0x1F6FF},	// Transport and Map Symbols
		{0x1F780, 0x1F7FF},	// Geometric Shapes Extended
		{0x1F900, 0x1F9FF},	// Supplemental Symbols and Pictographs
		{0x1FA70, 0x1FAFF},	// Symbols and Pictographs Extended-A
	};

	// What the summary names by script rather than by block: the blocks of each script a CJK font is set up for.
	constexpr FCodepointRange HanRanges[] =
	{
		{0x4E00, 0x9FFF}, {0x3400, 0x4DBF}, {0xF900, 0xFAFF}, {0x20000, 0x2A6D6}, {0x2A700, 0x2B734}, {0x2B740, 0x2B81D},
		{0x2B820, 0x2CEA1}, {0x2F800, 0x2FA1F}, {0x2E80, 0x2EFF}, {0x2F00, 0x2FDF}, {0x31C0, 0x31EF},
	};
	constexpr FCodepointRange KanaRanges[] = { {0x3040, 0x309F}, {0x30A0, 0x30FF}, {0x31F0, 0x31FF}, {0x1B000, 0x1B0FF} };
	constexpr FCodepointRange HangulRanges[] = { {0x1100, 0x11FF}, {0x3130, 0x318F}, {0xAC00, 0xD7AF}, {0xA960, 0xA97F}, {0xD7B0, 0xD7FF} };
	constexpr FCodepointRange CJKPunctuationRanges[] = { {0x3000, 0x303F} };
	constexpr FCodepointRange FullwidthRanges[] = { {0xFF00, 0xFFEF} };

	constexpr int32 MaxUnicodeCodepoint = 0x10FFFF;
	/** Summary labels past this many become "N more". */
	constexpr int32 MaxSummaryRangeLabels = 3;
	/** A ranges list longer than this starts collapsed: the entry's header already says what it covers. */
	constexpr int32 MaxRangesShownExpanded = 6;

	bool TableHasRange(TConstArrayView<FCodepointRange> InTable, int32 InMin, int32 InMax)
	{
		for (const FCodepointRange& Range : InTable)
		{
			if (Range.Min == InMin && Range.Max == InMax)
			{
				return true;
			}
		}
		return false;
	}

	bool ListHasRange(const TArray<FInt32Interval>& InRanges, const FCodepointRange& InRange)
	{
		return InRanges.ContainsByPredicate([&InRange](const FInt32Interval& Range) { return Range.Min == InRange.Min && Range.Max == InRange.Max; });
	}

	/** "U+4E00", or "U+4E00–9FFF" for a range of more than one code point. */
	FString FormatBounds(int32 InMin, int32 InMax)
	{
		return InMin == InMax
			? FString::Printf(TEXT("U+%04X"), InMin)
			: FString::Printf(TEXT("U+%04X–%04X"), InMin, InMax);
	}

	FString FormatCodepoint(int32 InCodepoint)
	{
		return FString::Printf(TEXT("U+%04X"), InCodepoint);
	}

	/** A code point as typed: hex digits, with or without "U+" or "0x" in front. */
	bool ParseCodepoint(const FString& InText, int32& OutCodepoint)
	{
		FString Digits = InText.TrimStartAndEnd();
		if (Digits.StartsWith(TEXT("U+"), ESearchCase::IgnoreCase) || Digits.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
		{
			Digits.RightChopInline(2);
		}
		if (Digits.IsEmpty() || Digits.Len() > 6)
		{
			return false;
		}
		for (int32 Index = 0; Index < Digits.Len(); Index++)
		{
			if (!FChar::IsHexDigit(Digits[Index]))
			{
				return false;
			}
		}
		const uint32 Codepoint = FParse::HexNumber(Digits);
		if (Codepoint > (uint32)MaxUnicodeCodepoint)
		{
			return false;
		}
		OutCodepoint = (int32)Codepoint;
		return true;
	}

	void NotifyEditRefused(const FText& InMessage)
	{
		FNotificationInfo Info(InMessage);
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	/** The block whose bounds are exactly these; the table is the engine's, kept for the life of the process. */
	const FUnicodeBlockRange* FindExactBlock(int32 InMin, int32 InMax)
	{
		auto MakeKey = [](int32 InLower, int32 InUpper) { return ((uint64)(uint32)InLower << 32) | (uint64)(uint32)InUpper; };
		static const TMap<uint64, const FUnicodeBlockRange*> BlocksByBounds = [&MakeKey]()
		{
			TMap<uint64, const FUnicodeBlockRange*> Result;
			for (const FUnicodeBlockRange& Block : FUnicodeBlockRange::GetUnicodeBlockRanges())
			{
				Result.Add(MakeKey(Block.RangeLower, Block.RangeUpper), &Block);
			}
			return Result;
		}();
		const FUnicodeBlockRange* const* Found = BlocksByBounds.Find(MakeKey(InMin, InMax));
		return Found != nullptr ? *Found : nullptr;
	}

	/** The one block holding the whole range, or null; and how many blocks the range touches. */
	const FUnicodeBlockRange* FindContainingBlock(int32 InMin, int32 InMax, int32& OutTouchedBlocks)
	{
		const FUnicodeBlockRange* Containing = nullptr;
		OutTouchedBlocks = 0;
		for (const FUnicodeBlockRange& Block : FUnicodeBlockRange::GetUnicodeBlockRanges())
		{
			if (Block.RangeLower <= InMax && InMin <= Block.RangeUpper)
			{
				OutTouchedBlocks++;
				if (Block.RangeLower <= InMin && InMax <= Block.RangeUpper)
				{
					Containing = &Block;
				}
			}
		}
		return Containing;
	}

	bool IsValidRange(int32 InMin, int32 InMax)
	{
		return InMin >= 0 && InMin <= InMax && InMax <= MaxUnicodeCodepoint;
	}

	/** The script a range is one block of, as the summary names it; empty for any other range. */
	FText GetScriptLabel(int32 InMin, int32 InMax)
	{
		if (TableHasRange(HanRanges, InMin, InMax))return LOCTEXT("ScriptHan", "Han");
		if (TableHasRange(KanaRanges, InMin, InMax))return LOCTEXT("ScriptKana", "Kana");
		if (TableHasRange(HangulRanges, InMin, InMax))return LOCTEXT("ScriptHangul", "Hangul");
		if (TableHasRange(CJKPunctuationRanges, InMin, InMax))return LOCTEXT("ScriptCJKPunctuation", "CJK punctuation");
		if (TableHasRange(FullwidthRanges, InMin, InMax))return LOCTEXT("ScriptFullwidth", "Fullwidth");
		if (TableHasRange(EmojiPresetRanges, InMin, InMax))return LOCTEXT("ScriptEmoji", "Emoji");
		return FText::GetEmpty();
	}

	/** What a list of ranges covers in a few words: "every character", "Han+Kana", "Thai", "U+1F300–1FAFF", "CJK+2 more". */
	FString DescribeRanges(const TArray<FInt32Interval>& InRanges)
	{
		if (InRanges.Num() == 0)
		{
			return LOCTEXT("SummaryEveryCharacter", "every character").ToString();
		}
		TArray<FString> Labels;
		for (const FInt32Interval& Range : InRanges)
		{
			const FText Script = GetScriptLabel(Range.Min, Range.Max);
			if (!Script.IsEmpty())
			{
				Labels.AddUnique(Script.ToString());
			}
			else if (const FUnicodeBlockRange* Block = FindExactBlock(Range.Min, Range.Max))
			{
				Labels.AddUnique(Block->GetDisplayName().ToString());
			}
			else
			{
				Labels.AddUnique(FormatBounds(Range.Min, Range.Max));
			}
		}
		// The whole CJK preset is one word, where its first script was.
		const FString CJKScripts[] =
		{
			LOCTEXT("ScriptHan", "Han").ToString(),
			LOCTEXT("ScriptKana", "Kana").ToString(),
			LOCTEXT("ScriptHangul", "Hangul").ToString(),
			LOCTEXT("ScriptCJKPunctuation", "CJK punctuation").ToString(),
			LOCTEXT("ScriptFullwidth", "Fullwidth").ToString(),
		};
		int32 FirstCJKScript = INDEX_NONE;
		bool bHasEveryCJKScript = true;
		for (const FString& Script : CJKScripts)
		{
			const int32 Found = Labels.IndexOfByKey(Script);
			if (Found == INDEX_NONE)
			{
				bHasEveryCJKScript = false;
				break;
			}
			FirstCJKScript = FirstCJKScript == INDEX_NONE ? Found : FMath::Min(FirstCJKScript, Found);
		}
		if (bHasEveryCJKScript)
		{
			for (const FString& Script : CJKScripts)
			{
				Labels.Remove(Script);
			}
			Labels.Insert(LOCTEXT("ScriptCJK", "CJK").ToString(), FMath::Min(FirstCJKScript, Labels.Num()));
		}
		if (Labels.Num() > MaxSummaryRangeLabels)
		{
			const int32 More = Labels.Num() - MaxSummaryRangeLabels;
			Labels.SetNum(MaxSummaryRangeLabels);
			Labels.Add(FText::Format(LOCTEXT("SummaryMoreRanges", "{0} more"), FText::AsNumber(More)).ToString());
		}
		return FString::Join(Labels, TEXT("+"));
	}

	/** "ja;zh-Hans" as the names in it, trimmed, empty ones dropped -- the way the font splits them. */
	TArray<FString> SplitCultures(const FString& InCultures)
	{
		TArray<FString> Names;
		InCultures.ParseIntoArray(Names, TEXT(";"), true);
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
		}
		Names.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });
		return Names;
	}

	bool IsSameEntry(const FDreamUIFontFallback& InA, const FDreamUIFontFallback& InB)
	{
		return InA.Font == InB.Font
			&& InA.Ranges == InB.Ranges
			&& InA.Cultures.Equals(InB.Cultures, ESearchCase::CaseSensitive)
			&& InA.Scale == InB.Scale
			&& InA.bPreferOverPrimary == InB.bPreferOverPrimary;
	}

	/** "GenEi · ja · Han+Kana · ×1.0": the entry's font, languages, ranges and scale, and whether it goes before the font's own face. */
	FText GetSummaryText(const TSharedRef<IPropertyHandle>& InEntryHandle)
	{
		if (!InEntryHandle->IsValidHandle())
		{
			return FText::GetEmpty();
		}
		TArray<const void*> RawData;
		InEntryHandle->AccessRawData(RawData);
		const FDreamUIFontFallback* Entry = nullptr;
		for (const void* Raw : RawData)
		{
			const FDreamUIFontFallback* Candidate = static_cast<const FDreamUIFontFallback*>(Raw);
			if (Candidate == nullptr)
			{
				return FText::GetEmpty();
			}
			if (Entry == nullptr)
			{
				Entry = Candidate;
			}
			else if (!IsSameEntry(*Entry, *Candidate))
			{
				return LOCTEXT("SummaryMultipleValues", "Multiple Values");
			}
		}
		if (Entry == nullptr)
		{
			return FText::GetEmpty();
		}

		static const FNumberFormattingOptions ScaleFormat = FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(2);
		TArray<FString> Parts;
		Parts.Add(Entry->Font != nullptr ? Entry->Font->GetName() : LOCTEXT("SummaryNoFont", "No font").ToString());
		const TArray<FString> Cultures = SplitCultures(Entry->Cultures);
		Parts.Add(Cultures.Num() > 0 ? FString::Join(Cultures, TEXT(", ")) : LOCTEXT("SummaryAnyLanguage", "any language").ToString());
		Parts.Add(DescribeRanges(Entry->Ranges));
		Parts.Add(FString(TEXT("×")) + FText::AsNumber(Entry->Scale, &ScaleFormat).ToString());
		if (Entry->bPreferOverPrimary)
		{
			Parts.Add(LOCTEXT("SummaryPreferred", "before the font's own face").ToString());
		}
		return FText::FromString(FString::Join(Parts, TEXT(" · ")));
	}

	/** Entry i of a font's fallbacks is its face i + 1, which is what the "Resolve Sample" row and the face-index APIs call it. */
	TSharedRef<SWidget> MakeEntryNameWidget(const TSharedRef<IPropertyHandle>& InEntryHandle)
	{
		const int32 IndexInArray = InEntryHandle->GetIndexInArray();
		const TSharedPtr<IPropertyHandle> ListHandle = InEntryHandle->GetParentHandle();
		const FProperty* ListProperty = ListHandle.IsValid() ? ListHandle->GetProperty() : nullptr;
		const UClass* ListOwner = ListProperty != nullptr ? ListProperty->GetOwnerClass() : nullptr;
		if (IndexInArray != INDEX_NONE && ListProperty != nullptr && ListProperty->IsA<FArrayProperty>()
			&& ListOwner != nullptr && ListOwner->IsChildOf(UDreamUIFontData_FreeTypeRender::StaticClass()))
		{
			return InEntryHandle->CreatePropertyNameWidget(
				FText::Format(LOCTEXT("EntryFaceName", "Face {0}"), FText::AsNumber(IndexInArray + 1)),
				FText::Format(LOCTEXT("EntryFaceName_Tooltip", "Fallbacks[{0}]: the font's face {1}. Face 0 is the font's own."), FText::AsNumber(IndexInArray), FText::AsNumber(IndexInArray + 1)));
		}
		return InEntryHandle->CreatePropertyNameWidget();
	}

	/**
	 * Adds these ranges to the list of every object being edited, those it already has left out, as one change: one
	 * transaction, and one PostEditChange on the font rather than one per range.
	 */
	void AppendRanges(const TSharedRef<IPropertyHandle>& InRangesHandle, TConstArrayView<FCodepointRange> InRanges)
	{
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InRangesHandle->GetProperty());
		if (ArrayProperty == nullptr)
		{
			return;
		}
		TArray<const void*> RawData;
		InRangesHandle->AccessRawData(RawData);
		TArray<FString> PerObjectValues;
		bool bAnyAdded = false;
		for (const void* Raw : RawData)
		{
			if (Raw == nullptr)
			{
				return;
			}
			TArray<FInt32Interval> Ranges = *static_cast<const TArray<FInt32Interval>*>(Raw);
			for (const FCodepointRange& Range : InRanges)
			{
				if (!ListHasRange(Ranges, Range))
				{
					Ranges.Add(FInt32Interval(Range.Min, Range.Max));
					bAnyAdded = true;
				}
			}
			ArrayProperty->ExportText_Direct(PerObjectValues.AddDefaulted_GetRef(), &Ranges, &Ranges, nullptr, PPF_None);
		}
		if (bAnyAdded && InRangesHandle->SetPerObjectValues(PerObjectValues) == FPropertyAccess::Success)
		{
			// The element rows are the array builder's; it makes them again for the new count.
			InRangesHandle->RequestRebuildChildren();
		}
	}

	/** Whether every object being edited already has all of these ranges: the menu entry would add nothing. */
	bool HasAllRanges(const TSharedRef<IPropertyHandle>& InRangesHandle, TConstArrayView<FCodepointRange> InRanges)
	{
		TArray<const void*> RawData;
		InRangesHandle->AccessRawData(RawData);
		if (RawData.Num() == 0)
		{
			return false;
		}
		for (const void* Raw : RawData)
		{
			if (Raw == nullptr)
			{
				return false;
			}
			const TArray<FInt32Interval>& Ranges = *static_cast<const TArray<FInt32Interval>*>(Raw);
			for (const FCodepointRange& Range : InRanges)
			{
				if (!ListHasRange(Ranges, Range))
				{
					return false;
				}
			}
		}
		return true;
	}

	void AddRangesMenuEntry(FMenuBuilder& InMenuBuilder, const TSharedRef<IPropertyHandle>& InRangesHandle, const FText& InLabel, const FText& InToolTip,
		const FText& InBounds, TConstArrayView<FCodepointRange> InRanges)
	{
		// The entry outlives this call; it keeps its own copy of what it adds.
		const TArray<FCodepointRange> Ranges(InRanges.GetData(), InRanges.Num());
		InMenuBuilder.AddMenuEntry(InLabel, InToolTip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([InRangesHandle, Ranges]() { AppendRanges(InRangesHandle, Ranges); }),
				FCanExecuteAction::CreateLambda([InRangesHandle, Ranges]() { return InRangesHandle->IsEditable() && !HasAllRanges(InRangesHandle, Ranges); })),
			NAME_None, EUserInterfaceActionType::Button, NAME_None, InBounds);
	}

	TSharedRef<SWidget> MakeAddBlockMenu(TSharedRef<IPropertyHandle> InRangesHandle)
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("PresetsSection", "Presets"));
		AddRangesMenuEntry(MenuBuilder, InRangesHandle,
			LOCTEXT("CJKPreset", "CJK"),
			LOCTEXT("CJKPreset_Tooltip", "Han (the unified ideographs, extensions A and B, the compatibility ideographs), hiragana and katakana, Hangul, CJK symbols and punctuation, and the halfwidth and fullwidth forms."),
			FText::GetEmpty(), CJKPresetRanges);
		AddRangesMenuEntry(MenuBuilder, InRangesHandle,
			LOCTEXT("EmojiPreset", "Emoji"),
			LOCTEXT("EmojiPreset_Tooltip", "Every code point an emoji can start with: the pictograph blocks, the regional indicators of flags, the symbols and dingbats that take U+FE0F, and the keycap bases # * 0-9."),
			FText::GetEmpty(), EmojiPresetRanges);
		MenuBuilder.EndSection();

		// By name, as the engine's own composite font editor lists them; the menu's search finds one by typing.
		TArray<const FUnicodeBlockRange*> Blocks;
		for (const FUnicodeBlockRange& Block : FUnicodeBlockRange::GetUnicodeBlockRanges())
		{
			Blocks.Add(&Block);
		}
		Algo::Sort(Blocks, [](const FUnicodeBlockRange* InA, const FUnicodeBlockRange* InB)
		{
			return InA->GetDisplayName().CompareTo(InB->GetDisplayName()) < 0;
		});
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("BlocksSection", "Unicode blocks"));
		for (const FUnicodeBlockRange* Block : Blocks)
		{
			const FCodepointRange Range{Block->RangeLower, Block->RangeUpper};
			const FText Bounds = FText::FromString(FormatBounds(Range.Min, Range.Max));
			AddRangesMenuEntry(MenuBuilder, InRangesHandle, Block->GetDisplayName(), Bounds, Bounds, MakeArrayView(&Range, 1));
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> MakeAddBlockButton(const TSharedRef<IPropertyHandle>& InRangesHandle)
	{
		return SNew(SComboButton)
			.HasDownArrow(true)
			.IsEnabled(TAttribute<bool>(InRangesHandle, &IPropertyHandle::IsEditable))
			.ToolTipText(LOCTEXT("AddBlock_Tooltip", "Add a Unicode block, or the CJK or emoji preset, to the code points this face may draw."))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("AddBlock", "Add Unicode block"))
			]
			.OnGetMenuContent_Lambda([InRangesHandle]() { return MakeAddBlockMenu(InRangesHandle); });
	}

	/** One end of a range, in hex. */
	TSharedRef<SWidget> MakeCodepointBox(const TSharedRef<IPropertyHandle>& InHandle)
	{
		auto GetDisplayText = [InHandle]() -> FText
		{
			int32 Value = 0;
			switch (InHandle->GetValue(Value))
			{
			case FPropertyAccess::Success: return FText::FromString(FormatCodepoint(Value));
			case FPropertyAccess::MultipleValues: return LOCTEXT("CodepointMultipleValues", "Multiple Values");
			default: return FText::GetEmpty();
			}
		};
		return SNew(SBox)
			.WidthOverride(84.0f)
			[
				SNew(SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsEnabled(TAttribute<bool>(InHandle, &IPropertyHandle::IsEditable))
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.ToolTipText(LOCTEXT("Codepoint_Tooltip", "A code point in hex: 4E00, U+4E00 or 0x4E00."))
				.Text_Lambda(GetDisplayText)
				.OnTextCommitted_Lambda([InHandle, GetDisplayText](const FText& InText, ETextCommit::Type InCommitType)
				{
					// Leaving the box unchanged is not an edit.
					if (InCommitType == ETextCommit::OnCleared || InText.ToString().Equals(GetDisplayText().ToString(), ESearchCase::CaseSensitive))
					{
						return;
					}
					int32 Codepoint = 0;
					if (!ParseCodepoint(InText.ToString(), Codepoint))
					{
						NotifyEditRefused(FText::Format(LOCTEXT("NotACodepoint", "\"{0}\" is not a code point: type it in hex, from 0 to 10FFFF, as 4E00, U+4E00 or 0x4E00."), InText));
						return;
					}
					InHandle->SetValue(Codepoint);
				})
			];
	}

	/** What the range on a row is: its block, the block it lies in, how many blocks it spans; or that it is no range at all. */
	FText DescribeRangeRow(const TSharedRef<IPropertyHandle>& InElementHandle, bool& bOutInvalid)
	{
		bOutInvalid = false;
		if (!InElementHandle->IsValidHandle())
		{
			return FText::GetEmpty();
		}
		TArray<const void*> RawData;
		InElementHandle->AccessRawData(RawData);
		const FInt32Interval* Range = nullptr;
		for (const void* Raw : RawData)
		{
			const FInt32Interval* Candidate = static_cast<const FInt32Interval*>(Raw);
			if (Candidate == nullptr || (Range != nullptr && !(*Range == *Candidate)))
			{
				return FText::GetEmpty();
			}
			Range = Candidate;
		}
		if (Range == nullptr)
		{
			return FText::GetEmpty();
		}
		if (!IsValidRange(Range->Min, Range->Max))
		{
			bOutInvalid = true;
			return LOCTEXT("InvalidRange", "not a range of code points: Min must be at most Max, both from 0 to 10FFFF");
		}
		if (const FUnicodeBlockRange* Block = FindExactBlock(Range->Min, Range->Max))
		{
			return Block->GetDisplayName();
		}
		int32 TouchedBlocks = 0;
		if (const FUnicodeBlockRange* Block = FindContainingBlock(Range->Min, Range->Max, TouchedBlocks))
		{
			return FText::Format(LOCTEXT("RangeInsideBlock", "in {0}"), Block->GetDisplayName());
		}
		return TouchedBlocks > 1
			? FText::Format(LOCTEXT("RangeAcrossBlocks", "across {0} blocks"), FText::AsNumber(TouchedBlocks))
			: FText::GetEmpty();
	}

	void GenerateRangeRow(TSharedRef<IPropertyHandle> InElementHandle, int32 InArrayIndex, IDetailChildrenBuilder& InChildrenBuilder)
	{
		IDetailPropertyRow& Row = InChildrenBuilder.AddProperty(InElementHandle);
		const TSharedPtr<IPropertyHandle> MinHandle = InElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FInt32Interval, Min));
		const TSharedPtr<IPropertyHandle> MaxHandle = InElementHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FInt32Interval, Max));
		if (!MinHandle.IsValid() || !MaxHandle.IsValid())
		{
			return;
		}
		Row.CustomWidget()
		.NameContent()
		[
			InElementHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeCodepointBox(MinHandle.ToSharedRef())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(FText::FromString(TEXT("–")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeCodepointBox(MaxHandle.ToSharedRef())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text_Lambda([InElementHandle]()
				{
					bool bInvalid = false;
					return DescribeRangeRow(InElementHandle, bInvalid);
				})
				.ColorAndOpacity_Lambda([InElementHandle]() -> FSlateColor
				{
					bool bInvalid = false;
					DescribeRangeRow(InElementHandle, bInvalid);
					return bInvalid ? FStyleColors::Warning : FSlateColor::UseSubduedForeground();
				})
			]
		];
	}

	/**
	 * The ranges, each in hex beside the block it is, under a header that adds whole blocks. Long lists start collapsed:
	 * the entry's own header already says what they cover.
	 */
	class FRangesBuilder : public FDetailArrayBuilder
	{
	public:
		explicit FRangesBuilder(TSharedRef<IPropertyHandle> InRangesHandle)
			: FDetailArrayBuilder(InRangesHandle, true, true, true)
			, RangesHandle(InRangesHandle)
		{
			OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateStatic(&GenerateRangeRow));
		}

		virtual bool InitiallyCollapsed() const override
		{
			uint32 NumRanges = 0;
			const TSharedPtr<IPropertyHandleArray> RangesArray = RangesHandle->AsArray();
			return RangesArray.IsValid() && RangesArray->GetNumElements(NumRanges) == FPropertyAccess::Success && NumRanges > (uint32)MaxRangesShownExpanded;
		}

		virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override
		{
			FDetailArrayBuilder::GenerateHeaderRowContent(NodeRow);
			// What the base put there -- the element count, add and empty, reset -- then the block menu.
			const TSharedRef<SWidget> ArrayValueWidget = NodeRow.ValueWidget.Widget;
			NodeRow.ValueContent()
			.MinDesiredWidth(250.0f)
			.MaxDesiredWidth(600.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					ArrayValueWidget
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeAddBlockButton(RangesHandle)
				]
			];
		}

	private:
		TSharedRef<IPropertyHandle> RangesHandle;
	};

	/** Adds a culture to the list of every object being edited that does not have it yet; the others keep their text. */
	void AppendCulture(const TSharedRef<IPropertyHandle>& InCulturesHandle, const FString& InCultureName)
	{
		if (InCultureName.IsEmpty())
		{
			return;
		}
		TArray<FString> PerObjectValues;
		if (InCulturesHandle->GetPerObjectValues(PerObjectValues) != FPropertyAccess::Success)
		{
			return;
		}
		bool bAnyAdded = false;
		for (FString& Value : PerObjectValues)
		{
			TArray<FString> Names = SplitCultures(Value);
			if (!Names.ContainsByPredicate([&InCultureName](const FString& Name) { return Name.Equals(InCultureName, ESearchCase::IgnoreCase); }))
			{
				Names.Add(InCultureName);
				Value = FString::Join(Names, TEXT(";"));
				bAnyAdded = true;
			}
		}
		if (bAnyAdded)
		{
			InCulturesHandle->SetPerObjectValues(PerObjectValues);
		}
	}

	void AddCulturesRow(const TSharedRef<IPropertyHandle>& InCulturesHandle, IDetailChildrenBuilder& InChildBuilder)
	{
		InChildBuilder.AddProperty(InCulturesHandle).CustomWidget()
		.NameContent()
		[
			InCulturesHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				InCulturesHandle->CreatePropertyValueWidget(false)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.IsEnabled(TAttribute<bool>(InCulturesHandle, &IPropertyHandle::IsEditable))
				[
					DreamUICulturePicker::MakeComboButton(
						LOCTEXT("AddCulture", "Add culture"),
						LOCTEXT("AddCulture_Tooltip", "Add a culture to the ones this face is meant for. A text matches when its language, or a language it falls back to, is one of them: \"zh\" covers Chinese of any script."),
						FText::GetEmpty(),
						[InCulturesHandle](const FString& InCultureName) { AppendCulture(InCulturesHandle, InCultureName); })
				]
			]
		];
	}

	float GetMetaFloat(const TSharedRef<IPropertyHandle>& InHandle, const TCHAR* InKey, float InFallback)
	{
		const FString& Value = InHandle->GetMetaData(FName(InKey));
		return Value.IsEmpty() ? InFallback : FCString::Atof(*Value);
	}

	void AddScaleRow(const TSharedRef<IPropertyHandle>& InScaleHandle, IDetailChildrenBuilder& InChildBuilder)
	{
		// A drag is one change to undo: interactive writes while the mouse holds the slider, a final one when it lets go.
		// The slider takes no keyboard focus, so nothing writes interactively outside a drag and leaves the change open.
		const TSharedRef<bool> bDragging = MakeShared<bool>(false);
		InChildBuilder.AddProperty(InScaleHandle).CustomWidget()
		.NameContent()
		[
			InScaleHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SSlider)
				.MinValue(GetMetaFloat(InScaleHandle, TEXT("UIMin"), 0.5f))
				.MaxValue(GetMetaFloat(InScaleHandle, TEXT("UIMax"), 2.0f))
				.StepSize(0.05f)
				.MouseUsesStep(true)
				.IsFocusable(false)
				.IsEnabled(TAttribute<bool>(InScaleHandle, &IPropertyHandle::IsEditable))
				.Value_Lambda([InScaleHandle]()
				{
					float Value = 1.0f;
					InScaleHandle->GetValue(Value);
					return Value;
				})
				.OnMouseCaptureBegin_Lambda([bDragging]() { *bDragging = true; })
				.OnValueChanged_Lambda([InScaleHandle, bDragging](float InValue)
				{
					InScaleHandle->SetValue(InValue, *bDragging ? EPropertyValueSetFlags::InteractiveChange : EPropertyValueSetFlags::DefaultFlags);
				})
				.OnMouseCaptureEnd_Lambda([InScaleHandle, bDragging]()
				{
					*bDragging = false;
					float Value = 1.0f;
					if (InScaleHandle->GetValue(Value) == FPropertyAccess::Success)
					{
						InScaleHandle->SetValue(Value);
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(64.0f)
				[
					InScaleHandle->CreatePropertyValueWidget(false)
				]
			]
		];
	}
}

TSharedRef<IPropertyTypeCustomization> FDreamUIFontFallbackCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIFontFallbackCustomization());
}

void FDreamUIFontFallbackCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace DreamUIFontFallbackCustomizationLocal;
	HeaderRow
	.NameContent()
	[
		MakeEntryNameWidget(PropertyHandle)
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(600.0f)
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		.Text_Lambda([PropertyHandle]() { return GetSummaryText(PropertyHandle); })
		.ToolTipText_Lambda([PropertyHandle]() { return GetSummaryText(PropertyHandle); })
	];
}

void FDreamUIFontFallbackCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace DreamUIFontFallbackCustomizationLocal;
	const TSharedPtr<IPropertyHandle> FontHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIFontFallback, Font));
	const TSharedPtr<IPropertyHandle> RangesHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIFontFallback, Ranges));
	const TSharedPtr<IPropertyHandle> CulturesHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIFontFallback, Cultures));
	const TSharedPtr<IPropertyHandle> ScaleHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIFontFallback, Scale));
	const TSharedPtr<IPropertyHandle> PreferHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIFontFallback, bPreferOverPrimary));
	if (!FontHandle.IsValid() || !RangesHandle.IsValid() || !RangesHandle->AsArray().IsValid() || !CulturesHandle.IsValid() || !ScaleHandle.IsValid() || !PreferHandle.IsValid())
	{
		// Whatever the struct has, as the default layout would show it.
		uint32 NumChildren = 0;
		PropertyHandle->GetNumChildren(NumChildren);
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			if (const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(ChildIndex))
			{
				ChildBuilder.AddProperty(Child.ToSharedRef());
			}
		}
		return;
	}

	ChildBuilder.AddProperty(FontHandle.ToSharedRef());
	ChildBuilder.AddCustomBuilder(MakeShared<FRangesBuilder>(RangesHandle.ToSharedRef()));
	AddCulturesRow(CulturesHandle.ToSharedRef(), ChildBuilder);
	AddScaleRow(ScaleHandle.ToSharedRef(), ChildBuilder);
	ChildBuilder.AddProperty(PreferHandle.ToSharedRef());
}

TSharedRef<SWidget> DreamUICulturePicker::MakeComboButton(const TAttribute<FText>& InButtonText, const FText& InToolTip, const FText& InNoneLabel,
	TFunction<void(const FString&)> InOnPicked, TFunction<FString()> InGetCurrent)
{
	const TSharedRef<SComboButton> ComboButton = SNew(SComboButton)
		.HasDownArrow(true)
		.ToolTipText(InToolTip)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(InButtonText)
		];
	// The button owns its menu, so the menu holds the button weakly to close it.
	const TWeakPtr<SComboButton> WeakComboButton = ComboButton;
	ComboButton->SetOnGetMenuContent(FOnGetContent::CreateLambda([WeakComboButton, InNoneLabel, InOnPicked, InGetCurrent]() -> TSharedRef<SWidget>
	{
		auto Pick = [WeakComboButton, InOnPicked](const FString& InCultureName)
		{
			if (const TSharedPtr<SComboButton> Button = WeakComboButton.Pin())
			{
				Button->SetIsOpen(false);
			}
			InOnPicked(InCultureName);
		};
		const TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
		if (!InNoneLabel.IsEmpty())
		{
			Menu->AddSlot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked_Lambda([Pick]()
				{
					Pick(FString());
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(InNoneLabel)
				]
			];
		}
		const FString CurrentName = InGetCurrent ? InGetCurrent() : FString();
		const FCulturePtr CurrentCulture = CurrentName.IsEmpty() ? FCulturePtr() : FInternationalization::Get().GetCulture(CurrentName);
		Menu->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.MaxDesiredHeight(400.0f)
			.MaxDesiredWidth(300.0f)
			[
				SNew(SCulturePicker)
				.InitialSelection(CurrentCulture)
				.DisplayNameFormat(SCulturePicker::ECultureDisplayFormat::ActiveAndNativeCultureDisplayName)
				.OnSelectionChanged_Lambda([Pick](FCulturePtr InCulture, ESelectInfo::Type InSelectInfo)
				{
					// The arrow keys walk the list; a click or Enter picks.
					if (InCulture.IsValid() && InSelectInfo != ESelectInfo::OnNavigation)
					{
						Pick(InCulture->GetName());
					}
				})
			]
		];
		return Menu;
	}));
	return ComboButton;
}

FText DreamUICulturePicker::GetCultureDisplayText(const FString& InCultureName, const FText& InEmptyText)
{
	const FString Name = InCultureName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		return InEmptyText;
	}
	if (const FCulturePtr Culture = FInternationalization::Get().GetCulture(Name))
	{
		return FText::Format(LOCTEXT("CultureDisplayName", "{0} ({1})"), FText::FromString(Culture->GetDisplayName()), FText::FromString(Name));
	}
	return FText::FromString(Name);
}

#undef LOCTEXT_NAMESPACE
