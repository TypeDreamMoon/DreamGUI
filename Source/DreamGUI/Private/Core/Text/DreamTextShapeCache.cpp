// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextShapeCache.h"
#include "Containers/LruCache.h"
#include "HAL/IConsoleManager.h"
#include "Hash/xxhash.h"
#include "Misc/CoreDelegates.h"
#include <atomic>

#if WITH_HARFBUZZ
#include "hb.h"
#include "hb-ot.h"
#endif

static int32 GDreamTextShapeCacheEnabled = 1;
static FAutoConsoleVariableRef CVarDreamTextShapeCache(
	TEXT("DreamGUI.Text.ShapeCache"),
	GDreamTextShapeCacheEnabled,
	TEXT("1: a word shaped before -- in any text, with the same face, size, script, direction, language and features -- is ")
	TEXT("taken from the shape cache instead of being shaped again. 0: every run is shaped whole, as before the cache."),
	ECVF_Default);

static int32 GDreamTextShapeCacheKB = 4096;
static FAutoConsoleVariableRef CVarDreamTextShapeCacheKB(
	TEXT("DreamGUI.Text.ShapeCacheKB"),
	GDreamTextShapeCacheKB,
	TEXT("The text shape cache's budget in KB; 4096 holds about 15,000 words. The least recently used words go first. A ")
	TEXT("change empties the cache; 0 keeps nothing."),
	ECVF_Default);

static FAutoConsoleCommand GDreamTextShapeCacheFlushCommand(
	TEXT("DreamGUI.Text.ShapeCacheFlush"),
	TEXT("Empties the text shape cache. Its counters are kept."),
	FConsoleCommandDelegate::CreateStatic(&FDreamTextShapeCache::Flush));

namespace DreamTextShapeCacheLocal
{
	/** An entry: the key it was stored under, whole -- the LRU is keyed by the key's hash -- and the glyphs. */
	struct FEntry
	{
		FDreamUIFontFaceIdentity Face;
		float Size = 0.0f;
		uint32 Script = 0;
		const void* Language = nullptr;
		uint8 Flags = 0;
		int32 PreContext = 0;
		int32 SegmentLength = 0;
		TArray<uint32> Codepoints;
		TArray<FDreamTextShapeCache::FGlyph> Glyphs;
		/** What the entry is counted at against the budget. */
		int64 Bytes = 0;

		bool Matches(const FDreamTextShapeCache::FKey& Key) const
		{
			return Face == Key.Face && Size == Key.Size && Script == Key.Script && Language == Key.Language && Flags == Key.Flags
				&& PreContext == Key.PreContext && SegmentLength == Key.SegmentLength && Codepoints.Num() == Key.Codepoints.Num()
				&& FMemory::Memcmp(Codepoints.GetData(), Key.Codepoints.GetData(), Codepoints.Num() * sizeof(uint32)) == 0;
		}
	};

	/** What an entry costs beyond its arrays: itself, the LRU's node around it and the lookup set's slot. */
	constexpr int64 EntryOverheadBytes = (int64)sizeof(FEntry) + 48;
	/**
	 * Budget bytes per entry the LRU may hold: its own cap, which reserves its lookup set up front and binds only for words
	 * far smaller than real ones -- the budget is what limits the cache.
	 */
	constexpr int64 BudgetBytesPerEntrySlot = 256;
	/** Faces whose rules are kept; past that the old epochs are forgotten and the faces still in use worked out again. */
	constexpr int32 MaxFaceRules = 64;

	struct FState
	{
		/** Made on first use, so a game that shapes nothing reserves nothing. */
		TUniquePtr<TLruCache<uint64, FEntry>> Entries;
		int64 BytesUsed = 0;
		/** The budget the entries were stored under: a different DreamGUI.Text.ShapeCacheKB empties the cache. */
		int64 BudgetBytes = -1;
		TMap<FDreamUIFontFaceIdentity, TUniquePtr<FDreamTextShapeCache::FFaceRules>> FaceRules;
		/** Inside Find or Add: a memory trim then waits for the call to end. */
		int32 CallDepth = 0;
	};

	FState& GetState()
	{
		static FState State;
		return State;
	}

	std::atomic<int64> ShapeCalls{ 0 };
	std::atomic<int64> ShapedCodepoints{ 0 };
	std::atomic<int64> Lookups{ 0 };
	std::atomic<int64> Hits{ 0 };
	std::atomic<int64> Evictions{ 0 };
	/** Memory was asked back, or a flush asked for, where the entries could not be dropped there and then. */
	std::atomic<bool> bDropRequested{ false };

	int64 GetBudgetBytes()
	{
		return (int64)FMath::Max(GDreamTextShapeCacheKB, 0) * 1024;
	}

	void DropEntries(FState& State)
	{
		State.Entries.Reset();
		State.BytesUsed = 0;
	}

	void EvictLeastRecent(FState& State)
	{
		const FEntry Evicted = State.Entries->RemoveLeastRecent();
		State.BytesUsed -= Evicted.Bytes;
		Evictions.fetch_add(1, std::memory_order_relaxed);
	}

	/** Find and Add start here: a drop asked for while the cache could not be touched happens now. */
	struct FCallScope
	{
		FState& State;
		explicit FCallScope(FState& InState)
			: State(InState)
		{
			if (State.CallDepth == 0 && bDropRequested.exchange(false))
			{
				DropEntries(State);
			}
			State.CallDepth++;
		}
		~FCallScope()
		{
			State.CallDepth--;
		}
	};

	/** The engine wants memory back (a map load, a low-memory warning): the words go, on the game thread when it is safe. */
	void HandleMemoryTrim()
	{
		FState& State = GetState();
		if (IsInGameThread() && State.CallDepth == 0)
		{
			DropEntries(State);
		}
		else
		{
			bDropRequested.store(true);
		}
	}

	/** Bound to the engine's memory trim from the first entry on, for as long as the module is loaded. */
	struct FTrimBinding
	{
		FDelegateHandle Handle;
		FTrimBinding()
		{
			Handle = FCoreDelegates::GetMemoryTrimDelegate().AddStatic(&HandleMemoryTrim);
		}
		~FTrimBinding()
		{
			FCoreDelegates::GetMemoryTrimDelegate().Remove(Handle);
		}
	};

	void EnsureTrimBound()
	{
		// Made after the engine's delegate, so it goes before it.
		static FTrimBinding Binding;
	}

	uint64 HashKey(const FDreamTextShapeCache::FKey& Key)
	{
		uint32 SizeBits = 0;
		FMemory::Memcpy(&SizeBits, &Key.Size, sizeof(SizeBits));
		const uint64 LanguageBits = (uint64)(UPTRINT)Key.Language;
		TArray<uint32, TInlineAllocator<64>> Words;
		Words.Reserve(8 + Key.Codepoints.Num());
		Words.Add(GetTypeHash(Key.Face));
		Words.Add(Key.Face.Epoch);
		Words.Add(SizeBits);
		Words.Add(Key.Script);
		Words.Add((uint32)LanguageBits);
		Words.Add((uint32)(LanguageBits >> 32));
		Words.Add((uint32)Key.Flags | ((uint32)Key.PreContext << 8));
		Words.Add((uint32)Key.SegmentLength);
		Words.Append(Key.Codepoints.GetData(), Key.Codepoints.Num());
		return FXxHash64::HashBuffer(Words.GetData(), (uint64)Words.Num() * sizeof(uint32)).Hash;
	}

#if WITH_HARFBUZZ
	/** Big-endian reads from an OpenType table, each checked against its end: a table that points outside itself is unusable. */
	struct FTableReader
	{
		const uint8* Data = nullptr;
		uint64 Size = 0;
		bool bOk = true;

		uint16 U16(uint64 Offset)
		{
			if (Offset + 2 > Size)
			{
				bOk = false;
				return 0;
			}
			return (uint16)((Data[Offset] << 8) | Data[Offset + 1]);
		}
		uint32 U32(uint64 Offset)
		{
			if (Offset + 4 > Size)
			{
				bOk = false;
				return 0;
			}
			return ((uint32)Data[Offset] << 24) | ((uint32)Data[Offset + 1] << 16) | ((uint32)Data[Offset + 2] << 8) | (uint32)Data[Offset + 3];
		}
	};

	/** A table of the face, held for as long as it is read. */
	struct FTable
	{
		hb_blob_t* Blob = nullptr;
		FTableReader Reader;

		FTable(hb_face_t* Face, hb_tag_t Tag)
		{
			Blob = hb_face_reference_table(Face, Tag);
			unsigned int Length = 0;
			const char* Bytes = hb_blob_get_data(Blob, &Length);
			Reader.Data = reinterpret_cast<const uint8*>(Bytes);
			Reader.Size = Bytes != nullptr ? Length : 0;
		}
		~FTable()
		{
			hb_blob_destroy(Blob);
		}
		bool IsEmpty() const
		{
			return Reader.Size == 0;
		}
	};

	bool HasTable(hb_face_t* Face, hb_tag_t Tag)
	{
		hb_blob_t* Blob = hb_face_reference_table(Face, Tag);
		const bool bHas = hb_blob_get_length(Blob) > 0;
		hb_blob_destroy(Blob);
		return bHas;
	}

	/** Whether the face's GSUB or GPOS lists a feature. */
	bool HasFeature(hb_face_t* Face, hb_tag_t TableTag, hb_tag_t FeatureTag)
	{
		constexpr unsigned int MaxTags = 32;
		hb_tag_t Tags[MaxTags];
		unsigned int Start = 0;
		for (;;)
		{
			unsigned int Count = MaxTags;
			const unsigned int Total = hb_ot_layout_table_get_feature_tags(Face, TableTag, Start, &Count, Tags);
			for (unsigned int Index = 0; Index < Count; Index++)
			{
				if (Tags[Index] == FeatureTag)
				{
					return true;
				}
			}
			Start += Count;
			if (Count == 0 || Start >= Total)
			{
				return false;
			}
		}
	}

	/** What one lookup of GSUB or GPOS does, as far as cutting a run goes. */
	struct FLookupKind
	{
		/** It matches more than one glyph: a ligature, a context, a pair, a cursive or a mark attachment. */
		bool bReadsSeveral = false;
		/** A GSUB substitution of one glyph whatever stands around it (single, multiple, alternate). */
		bool bReplacesOne = false;
		/** It reads glyphs no list of it names (FDreamTextShapeCache::FFaceRules::bSegmentable). */
		bool bUnlisted = false;
	};

	/**
	 * Whether a class-based context (format 2) has a rule matching class 0 past its first glyph: any glyph its class
	 * definitions leave out, which no glyph list of the lookup names.
	 */
	bool ContextMatchesClassZero(FTableReader& Reader, uint64 Subtable, bool bChained)
	{
		const uint32 SetCount = Reader.U16(Subtable + (bChained ? 10 : 6));
		const uint64 SetOffsets = Subtable + (bChained ? 12 : 8);
		for (uint32 SetIndex = 0; SetIndex < SetCount && Reader.bOk; SetIndex++)
		{
			const uint32 SetOffset = Reader.U16(SetOffsets + 2 * SetIndex);
			if (SetOffset == 0)
			{
				continue;
			}
			const uint64 Set = Subtable + SetOffset;
			const uint32 RuleCount = Reader.U16(Set);
			for (uint32 RuleIndex = 0; RuleIndex < RuleCount && Reader.bOk; RuleIndex++)
			{
				uint64 At = Set + Reader.U16(Set + 2 + 2 * RuleIndex);
				if (bChained)
				{
					// Backtrack classes, the input classes after the first, lookahead classes.
					for (int32 Part = 0; Part < 3 && Reader.bOk; Part++)
					{
						uint32 Count = Reader.U16(At);
						At += 2;
						if (Part == 1)
						{
							if (Count == 0)
							{
								return true;
							}
							Count--;
						}
						for (uint32 Index = 0; Index < Count; Index++)
						{
							if (Reader.U16(At + 2 * Index) == 0)
							{
								return true;
							}
						}
						At += 2 * (uint64)Count;
					}
				}
				else
				{
					const uint32 GlyphCount = Reader.U16(At);
					if (GlyphCount == 0)
					{
						return true;
					}
					for (uint32 Index = 0; Index + 1 < GlyphCount; Index++)
					{
						if (Reader.U16(At + 4 + 2 * Index) == 0)
						{
							return true;
						}
					}
				}
			}
		}
		return !Reader.bOk;
	}

	/** Whether a class-based pair (PairPos format 2) moves anything against class 0: any second glyph its classes leave out. */
	bool PairMovesAgainstClassZero(FTableReader& Reader, uint64 Subtable)
	{
		const uint64 RecordBytes = 2 * (uint64)(FMath::CountBits((uint32)Reader.U16(Subtable + 4)) + FMath::CountBits((uint32)Reader.U16(Subtable + 6)));
		const uint32 Class1Count = Reader.U16(Subtable + 12);
		const uint32 Class2Count = Reader.U16(Subtable + 14);
		if (RecordBytes == 0 || Class2Count == 0)
		{
			return !Reader.bOk;
		}
		for (uint32 Class1 = 0; Class1 < Class1Count && Reader.bOk; Class1++)
		{
			// Class 2 = 0 is the first record of each row.
			const uint64 Record = Subtable + 16 + (uint64)Class1 * Class2Count * RecordBytes;
			for (uint64 Byte = 0; Byte < RecordBytes; Byte += 2)
			{
				if (Reader.U16(Record + Byte) != 0)
				{
					return true;
				}
			}
		}
		return !Reader.bOk;
	}

	void ClassifySubtable(FTableReader& Reader, bool bSubstitution, uint32 Type, uint64 Subtable, FLookupKind& Kind)
	{
		const uint16 Format = Reader.U16(Subtable);
		if (bSubstitution)
		{
			switch (Type)
			{
			case 1: case 2: case 3:
				Kind.bReplacesOne = true;
				return;
			case 4: case 8:
				Kind.bReadsSeveral = true;
				return;
			case 5: case 6:
				Kind.bReadsSeveral = true;
				Kind.bUnlisted |= Format == 2 && ContextMatchesClassZero(Reader, Subtable, Type == 6);
				return;
			default:
				Kind.bUnlisted = true;
				return;
			}
		}
		switch (Type)
		{
		case 1:
			// Single adjustment: one glyph, whatever stands around it.
			return;
		case 2:
			Kind.bReadsSeveral = true;
			Kind.bUnlisted |= Format == 2 && PairMovesAgainstClassZero(Reader, Subtable);
			return;
		case 3: case 4: case 5: case 6:
			Kind.bReadsSeveral = true;
			return;
		case 7: case 8:
			Kind.bReadsSeveral = true;
			Kind.bUnlisted |= Format == 2 && ContextMatchesClassZero(Reader, Subtable, Type == 8);
			return;
		default:
			Kind.bUnlisted = true;
			return;
		}
	}

	/** Every lookup of the face's GSUB or GPOS, classified (none when the face has no such table). False when it cannot be read. */
	bool ClassifyLookups(hb_face_t* Face, hb_tag_t Tag, TArray<FLookupKind>& OutKinds)
	{
		OutKinds.Reset();
		FTable Table(Face, Tag);
		if (Table.IsEmpty())
		{
			return true;
		}
		FTableReader& Reader = Table.Reader;
		const bool bSubstitution = Tag == HB_OT_TAG_GSUB;
		const uint64 LookupList = Reader.U16(8);
		const uint32 LookupCount = Reader.U16(LookupList);
		OutKinds.SetNum(Reader.bOk ? (int32)LookupCount : 0);
		for (uint32 LookupIndex = 0; LookupIndex < (uint32)OutKinds.Num() && Reader.bOk; LookupIndex++)
		{
			const uint64 Lookup = LookupList + Reader.U16(LookupList + 2 + 2 * (uint64)LookupIndex);
			const uint32 Type = Reader.U16(Lookup);
			const uint32 Flag = Reader.U16(Lookup + 2);
			const uint32 SubtableCount = Reader.U16(Lookup + 4);
			const bool bExtension = Type == (bSubstitution ? 7u : 9u);
			FLookupKind& Kind = OutKinds[(int32)LookupIndex];
			for (uint32 SubtableIndex = 0; SubtableIndex < SubtableCount && Reader.bOk; SubtableIndex++)
			{
				uint64 Subtable = Lookup + Reader.U16(Lookup + 6 + 2 * (uint64)SubtableIndex);
				uint32 SubtableType = Type;
				if (bExtension)
				{
					SubtableType = Reader.U16(Subtable + 2);
					Subtable += Reader.U32(Subtable + 4);
				}
				ClassifySubtable(Reader, bSubstitution, SubtableType, Subtable, Kind);
				// A lookup that skips base glyphs can match the marks of two bases with a base between them. The mark
				// attachments look for their bases their own way.
				const bool bAttachment = !bSubstitution && SubtableType >= 4 && SubtableType <= 6;
				if ((Flag & 0x0002) != 0 && Kind.bReadsSeveral && !bAttachment)
				{
					Kind.bUnlisted = true;
				}
			}
		}
		return Reader.bOk;
	}

	/** The glyphs of a legacy kern table's pairs, which HarfBuzz applies when GPOS kerns nothing. False for anything but plain pairs. */
	bool AddKernPairs(hb_face_t* Face, hb_set_t* Touched)
	{
		FTable Table(Face, HB_TAG('k', 'e', 'r', 'n'));
		if (Table.IsEmpty())
		{
			return true;
		}
		FTableReader& Reader = Table.Reader;
		// Apple's version 1 kern, with its state machines, starts with a 32-bit version.
		if (Reader.U16(0) != 0)
		{
			return false;
		}
		const uint32 SubtableCount = Reader.U16(2);
		uint64 At = 4;
		for (uint32 SubtableIndex = 0; SubtableIndex < SubtableCount && Reader.bOk; SubtableIndex++)
		{
			const uint32 Coverage = Reader.U16(At + 4);
			if ((Coverage >> 8) != 0)
			{
				return false;
			}
			const uint32 PairCount = Reader.U16(At + 6);
			for (uint32 Pair = 0; Pair < PairCount && Reader.bOk; Pair++)
			{
				hb_set_add(Touched, Reader.U16(At + 14 + 6 * (uint64)Pair));
				hb_set_add(Touched, Reader.U16(At + 16 + 6 * (uint64)Pair));
			}
			// Format 0 is a 14-byte header and its pairs; its own 16-bit length overflows for a big one.
			At += 14 + 6 * (uint64)PairCount;
		}
		return Reader.bOk;
	}

	void CopySet(const hb_set_t* Set, TArray<hb_codepoint_t>& Out)
	{
		Out.Reset((int32)hb_set_get_population(Set));
		hb_codepoint_t Value = HB_SET_VALUE_INVALID;
		while (hb_set_next(Set, &Value))
		{
			Out.Add(Value);
		}
	}

	void ComputeFaceRules(hb_font_t* Font, FDreamTextShapeCache::FFaceRules& Out)
	{
		Out.bSegmentable = false;
		Out.TouchedGlyphs.Reset();
		hb_face_t* Face = Font != nullptr ? hb_font_get_face(Font) : nullptr;
		if (Face == nullptr)
		{
			return;
		}
		// Apple's layout tables run state machines over the whole run, which no glyph list describes.
		if (HasTable(Face, HB_TAG('m', 'o', 'r', 'x')) || HasTable(Face, HB_TAG('m', 'o', 'r', 't')) || HasTable(Face, HB_TAG('k', 'e', 'r', 'x')))
		{
			return;
		}
		// A face with no space glyph has HarfBuzz delete the default-ignorables it would hide, and merge their clusters into
		// the glyph beside them -- the one across a cut, when they stand next to it.
		hb_codepoint_t SpaceGlyph = 0;
		if (!hb_font_get_nominal_glyph(Font, 0x0020, &SpaceGlyph))
		{
			return;
		}
		// Syriac's stretching ('stch') spreads a glyph over the width of the word beside it, symbols included, measured on
		// the buffer rather than through a lookup.
		if (HasFeature(Face, HB_OT_TAG_GSUB, HB_TAG('s', 't', 'c', 'h')))
		{
			return;
		}
		hb_set_t* Touched = hb_set_create();
		hb_set_t* Input = hb_set_create();
		hb_set_t* Output = hb_set_create();
		struct FReplacement
		{
			TArray<hb_codepoint_t> From;
			TArray<hb_codepoint_t> To;
			bool bFollowed = false;
		};
		TArray<FReplacement> Replacements;
		TArray<FLookupKind> Kinds;
		bool bOk = true;
		for (const hb_tag_t Tag : { (hb_tag_t)HB_OT_TAG_GSUB, (hb_tag_t)HB_OT_TAG_GPOS })
		{
			if (!ClassifyLookups(Face, Tag, Kinds))
			{
				bOk = false;
				break;
			}
			// Every lookup, not only those a feature names: a lookup a context calls can reach past the context's own glyphs.
			for (int32 LookupIndex = 0; LookupIndex < Kinds.Num() && bOk; LookupIndex++)
			{
				const FLookupKind& Kind = Kinds[LookupIndex];
				if (Kind.bUnlisted)
				{
					bOk = false;
				}
				else if (Kind.bReadsSeveral)
				{
					hb_ot_layout_lookup_collect_glyphs(Face, Tag, (unsigned int)LookupIndex, Touched, Touched, Touched, nullptr);
				}
				else if (Kind.bReplacesOne)
				{
					hb_set_clear(Input);
					hb_set_clear(Output);
					hb_ot_layout_lookup_collect_glyphs(Face, Tag, (unsigned int)LookupIndex, nullptr, Input, nullptr, Output);
					FReplacement& Replacement = Replacements.AddDefaulted_GetRef();
					CopySet(Input, Replacement.From);
					CopySet(Output, Replacement.To);
				}
			}
			if (!bOk)
			{
				break;
			}
		}
		bOk = bOk && AddKernPairs(Face, Touched);
		// The glyphs GDEF calls ligatures or marks: a lookup flagged to skip them reads past them to the glyph beyond, so a
		// cut never stands next to one. (A face without glyph classes has its marks from Unicode, which no cut stands next to.)
		if (bOk)
		{
			hb_ot_layout_get_glyphs_in_class(Face, HB_OT_LAYOUT_GLYPH_CLASS_LIGATURE, Touched);
			hb_ot_layout_get_glyphs_in_class(Face, HB_OT_LAYOUT_GLYPH_CLASS_MARK, Touched);
		}
		// The lookups that read several glyphs see each glyph after the substitutions before them, so a glyph that can be
		// turned into a touched one is touched too. Followed until nothing changes: each pass follows a replacement or ends.
		bool bChanged = bOk;
		while (bChanged)
		{
			bChanged = false;
			for (FReplacement& Replacement : Replacements)
			{
				if (Replacement.bFollowed)
				{
					continue;
				}
				for (const hb_codepoint_t To : Replacement.To)
				{
					if (hb_set_has(Touched, To))
					{
						for (const hb_codepoint_t From : Replacement.From)
						{
							hb_set_add(Touched, From);
						}
						Replacement.bFollowed = true;
						bChanged = true;
						break;
					}
				}
			}
		}
		if (bOk)
		{
			const uint32 GlyphCount = hb_face_get_glyph_count(Face);
			Out.TouchedGlyphs.Init(false, (int32)GlyphCount);
			hb_codepoint_t Glyph = HB_SET_VALUE_INVALID;
			while (hb_set_next(Touched, &Glyph))
			{
				if (Glyph < GlyphCount)
				{
					Out.TouchedGlyphs[(int32)Glyph] = true;
				}
			}
			Out.bSegmentable = true;
		}
		hb_set_destroy(Output);
		hb_set_destroy(Input);
		hb_set_destroy(Touched);
	}
#endif
}

bool FDreamTextShapeCache::IsEnabled()
{
	return GDreamTextShapeCacheEnabled != 0;
}

void FDreamTextShapeCache::Flush()
{
	DreamTextShapeCacheLocal::FState& State = DreamTextShapeCacheLocal::GetState();
	if (!IsInGameThread() || State.CallDepth > 0)
	{
		DreamTextShapeCacheLocal::bDropRequested.store(true);
		return;
	}
	DreamTextShapeCacheLocal::DropEntries(State);
	State.FaceRules.Reset();
}

FDreamTextShapeCache::FStats FDreamTextShapeCache::GetStats()
{
	FStats Stats;
	Stats.ShapeCalls = DreamTextShapeCacheLocal::ShapeCalls.load(std::memory_order_relaxed);
	Stats.ShapedCodepoints = DreamTextShapeCacheLocal::ShapedCodepoints.load(std::memory_order_relaxed);
	Stats.Lookups = DreamTextShapeCacheLocal::Lookups.load(std::memory_order_relaxed);
	Stats.Hits = DreamTextShapeCacheLocal::Hits.load(std::memory_order_relaxed);
	Stats.Evictions = DreamTextShapeCacheLocal::Evictions.load(std::memory_order_relaxed);
	return Stats;
}

void FDreamTextShapeCache::ResetStats()
{
	DreamTextShapeCacheLocal::ShapeCalls.store(0);
	DreamTextShapeCacheLocal::ShapedCodepoints.store(0);
	DreamTextShapeCacheLocal::Lookups.store(0);
	DreamTextShapeCacheLocal::Hits.store(0);
	DreamTextShapeCacheLocal::Evictions.store(0);
}

int32 FDreamTextShapeCache::GetNumEntries()
{
	const DreamTextShapeCacheLocal::FState& State = DreamTextShapeCacheLocal::GetState();
	return State.Entries.IsValid() ? State.Entries->Num() : 0;
}

int64 FDreamTextShapeCache::GetBytesUsed()
{
	return DreamTextShapeCacheLocal::GetState().BytesUsed;
}

const FDreamTextShapeCache::FFaceRules* FDreamTextShapeCache::GetFaceRules(const FDreamUIFontFaceIdentity& Face, void* ShapingFont)
{
#if WITH_HARFBUZZ
	check(IsInGameThread());
	if (ShapingFont == nullptr || !Face.IsValid())
	{
		return nullptr;
	}
	DreamTextShapeCacheLocal::FState& State = DreamTextShapeCacheLocal::GetState();
	if (const TUniquePtr<FFaceRules>* Known = State.FaceRules.Find(Face))
	{
		return Known->Get();
	}
	if (State.FaceRules.Num() >= DreamTextShapeCacheLocal::MaxFaceRules)
	{
		State.FaceRules.Reset();
	}
	TUniquePtr<FFaceRules> Rules = MakeUnique<FFaceRules>();
	DreamTextShapeCacheLocal::ComputeFaceRules(static_cast<hb_font_t*>(ShapingFont), *Rules);
	return State.FaceRules.Add(Face, MoveTemp(Rules)).Get();
#else
	return nullptr;
#endif
}

const TArray<FDreamTextShapeCache::FGlyph>* FDreamTextShapeCache::Find(const FKey& Key)
{
	check(IsInGameThread());
	DreamTextShapeCacheLocal::FState& State = DreamTextShapeCacheLocal::GetState();
	const DreamTextShapeCacheLocal::FCallScope Scope(State);
	DreamTextShapeCacheLocal::Lookups.fetch_add(1, std::memory_order_relaxed);
	if (!State.Entries.IsValid())
	{
		return nullptr;
	}
	DreamTextShapeCacheLocal::FEntry* Entry = State.Entries->FindAndTouch(DreamTextShapeCacheLocal::HashKey(Key));
	if (Entry == nullptr || !Entry->Matches(Key))
	{
		return nullptr;
	}
	DreamTextShapeCacheLocal::Hits.fetch_add(1, std::memory_order_relaxed);
	return &Entry->Glyphs;
}

void FDreamTextShapeCache::Add(const FKey& Key, TConstArrayView<FGlyph> Glyphs)
{
	using FEntry = DreamTextShapeCacheLocal::FEntry;
	check(IsInGameThread());
	DreamTextShapeCacheLocal::FState& State = DreamTextShapeCacheLocal::GetState();
	const DreamTextShapeCacheLocal::FCallScope Scope(State);
	const int64 Budget = DreamTextShapeCacheLocal::GetBudgetBytes();
	if (Budget != State.BudgetBytes)
	{
		// A new budget empties the cache; the next entry makes it again under the new cap.
		DreamTextShapeCacheLocal::DropEntries(State);
		State.BudgetBytes = Budget;
	}
	if (Budget <= 0)
	{
		return;
	}
	if (!State.Entries.IsValid())
	{
		DreamTextShapeCacheLocal::EnsureTrimBound();
		const int64 MaxEntries = FMath::Clamp<int64>(Budget / DreamTextShapeCacheLocal::BudgetBytesPerEntrySlot, 16, 1 << 20);
		State.Entries = MakeUnique<TLruCache<uint64, FEntry>>((int32)MaxEntries);
	}
	TLruCache<uint64, FEntry>& Entries = *State.Entries;
	const uint64 Hash = DreamTextShapeCacheLocal::HashKey(Key);
	if (const FEntry* Existing = Entries.Find(Hash))
	{
		// The same key again, or another one with the same hash: the new glyphs take its place.
		State.BytesUsed -= Existing->Bytes;
		Entries.Remove(Hash);
	}
	// The LRU would drop its least recent entry by itself at its cap; it is dropped here so the bytes stay counted.
	if (Entries.Num() >= Entries.Max())
	{
		DreamTextShapeCacheLocal::EvictLeastRecent(State);
	}
	FEntry& Entry = Entries.AddUninitialized_GetRef(Hash);
	Entry.Face = Key.Face;
	Entry.Size = Key.Size;
	Entry.Script = Key.Script;
	Entry.Language = Key.Language;
	Entry.Flags = Key.Flags;
	Entry.PreContext = Key.PreContext;
	Entry.SegmentLength = Key.SegmentLength;
	Entry.Codepoints = TArray<uint32>(Key.Codepoints.GetData(), Key.Codepoints.Num());
	Entry.Glyphs = TArray<FGlyph>(Glyphs.GetData(), Glyphs.Num());
	Entry.Bytes = DreamTextShapeCacheLocal::EntryOverheadBytes + (int64)Entry.Codepoints.GetAllocatedSize() + (int64)Entry.Glyphs.GetAllocatedSize();
	State.BytesUsed += Entry.Bytes;
	while (State.BytesUsed > Budget && Entries.Num() > 0)
	{
		DreamTextShapeCacheLocal::EvictLeastRecent(State);
	}
}

void FDreamTextShapeCache::CountShape(int32 NumCodepoints)
{
	DreamTextShapeCacheLocal::ShapeCalls.fetch_add(1, std::memory_order_relaxed);
	DreamTextShapeCacheLocal::ShapedCodepoints.fetch_add(NumCodepoints, std::memory_order_relaxed);
}
