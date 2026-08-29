// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITextPatcher.h"

#include "Containers/ArrayView.h"
#include "Text/DreamUISourceFile.h"

/*
 * Text in, the same text with one value different, out.
 *
 * The whole file is built on one asymmetry: reading a .dui is the parser's job and it has a full
 * lexer to do it with, but WRITING one must not re-print anything it did not change. So this is not
 * a second parser. It is a set of small scans that answer four questions about the source text, and
 * nothing else:
 *
 *   where does this location fall in the string        OffsetOf
 *   where does this value end                          MeasureValue
 *   where is this node's block, if it has one          FindBlock
 *   where does a new line go, and how far indented     PlanInsert
 *
 * WHY THE SCANS ARE HAND WRITTEN AND NOT THE LEXER'S. Re-lexing the file to find a value's end
 * would be the obvious move and it is the wrong one twice over: the lexer lives in the runtime
 * module behind one entry point that returns an AST and no tokens (deliberately -- see
 * DreamUISourceFile.h), and it throws away exactly the thing this file needs, which is the raw
 * extent of every token including the whitespace and comments between them. What makes the small
 * scans safe is that the AST already told us what is there; they only have to measure it, and each
 * one CHECKS what it measured against what the AST said -- see TextAtIs and the slice compare in
 * PlanReplace. A scan that disagrees with the AST means the AST is stale, and that is a refusal
 * rather than an edit.
 *
 * THE ONE RULE THIS FILE MUST NEVER BREAK: it may not produce a .dui that no longer parses. An
 * author's layout file is hand-written work; corrupting it to store an anchor value is a trade
 * nobody would take. Everything defensive here -- the value probe, the location verification, the
 * refusal to touch a binding -- exists for that one rule, and each of them is cheaper than the bug
 * it prevents.
 *
 * ON THE DIAGNOSTIC CODES. Two of these refusals are raised under 1xxx/2xxx codes rather than the
 * 7xxx write-back band, and that is on purpose: the code table's rule is one code per CAUSE, and
 * when a caller hands over a value the grammar would reject, the cause genuinely is lexical. The
 * message says the write-back refused it and the location points at the line that would have been
 * edited, so the reader is not sent to the wrong stage. If the contract ever grows a dedicated
 * DUI7003 for "this value cannot be represented in text", these two sites are the ones to change --
 * they are marked.
 */

namespace DreamUIPatchLocal
{
	// --------------------------------------------------------------------------------------------
	// Character classes
	//
	// Copied from the lexer's, on purpose and required to stay copied: this file converts a
	// FDreamUISourceLocation back into an offset, and a location is only meaningful if both sides
	// agree on what ends a line. Let them drift and every location past the first disagreement is
	// off by a line -- which is not a crash, it is an edit landing on somebody else's property.
	// --------------------------------------------------------------------------------------------

	FORCEINLINE bool IsInlineWhitespace(TCHAR InChar)
	{
		return InChar == TEXT(' ') || InChar == TEXT('\t') || InChar == TEXT('\v') || InChar == TEXT('\f');
	}

	FORCEINLINE bool IsLineBreak(TCHAR InChar)
	{
		return InChar == TEXT('\n') || InChar == TEXT('\r');
	}

	FORCEINLINE bool StartsComment(const TCHAR* InChars, int32 InLength, int32 InOffset)
	{
		return InChars[InOffset] == TEXT('/') && InOffset + 1 < InLength
			&& (InChars[InOffset + 1] == TEXT('/') || InChars[InOffset + 1] == TEXT('*'));
	}

	FString Ellipsize(const FString& InText, int32 InMaxLength = 24)
	{
		const FString OneLine = InText.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
		return OneLine.Len() <= InMaxLength ? OneLine : (OneLine.Left(InMaxLength) + TEXT("..."));
	}

	// --------------------------------------------------------------------------------------------
	// Locations, lines and indentation
	// --------------------------------------------------------------------------------------------

	/**
	 * The inverse of the lexer's MakeLocation: a 1-based line and column back into a string index.
	 *
	 * Columns count TCHARs, not bytes, because that is what the lexer counted -- it subtracts two
	 * offsets into the same TCHAR array. A UTF-8 byte column would be a second convention nobody
	 * asked for, and the first CJK comment in a file would put every edit on that line four
	 * characters off. The parser's tests already pin columns on lines with CJK in them; this is the
	 * other half of that agreement.
	 *
	 * INDEX_NONE when the location does not describe this text at all -- too few lines, or a column
	 * past the end. The caller turns that into SourceFileChangedUnderEdit rather than clamping,
	 * because a clamped offset is an edit at a place the AST never pointed at.
	 */
	int32 OffsetOf(const FString& InText, const FDreamUISourceLocation& InLocation)
	{
		if (!InLocation.IsValid() || InLocation.Column < 1)
		{
			return INDEX_NONE;
		}

		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();

		int32 Offset = 0;
		int32 Line = 1;
		while (Line < InLocation.Line && Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (IsLineBreak(Char))
			{
				++Offset;
				// CRLF is one break, exactly as ConsumeLineBreak treats it. A lone CR is one too:
				// old Mac line endings are not worth supporting, but they are worth AGREEING about.
				if (Char == TEXT('\r') && Offset < Length && Chars[Offset] == TEXT('\n'))
				{
					++Offset;
				}
				++Line;
				continue;
			}
			++Offset;
		}

		if (Line != InLocation.Line)
		{
			return INDEX_NONE;
		}

		const int32 Result = Offset + InLocation.Column - 1;
		return Result <= Length ? Result : INDEX_NONE;
	}

	/** Offset of the line break that ends the line containing InOffset, or the length of the text. */
	int32 FindLineEnd(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Offset = FMath::Max(0, InOffset);
		while (Offset < Length && !IsLineBreak(Chars[Offset]))
		{
			++Offset;
		}
		return Offset;
	}

	/** Offset of the first character of the line containing InOffset. */
	int32 FindLineStart(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		int32 Offset = FMath::Clamp(InOffset, 0, InText.Len());
		while (Offset > 0 && !IsLineBreak(Chars[Offset - 1]))
		{
			--Offset;
		}
		return Offset;
	}

	/** The leading whitespace of the line containing InOffset, verbatim -- tabs stay tabs. */
	FString IndentAt(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		const int32 Start = FindLineStart(InText, InOffset);
		int32 End = Start;
		while (End < Length && IsInlineWhitespace(Chars[End]))
		{
			++End;
		}
		return InText.Mid(Start, End - Start);
	}

	/**
	 * CRLF or LF, whichever this file already uses. Ties go to CRLF because a tie means the file was
	 * saved by a Windows tool at least once.
	 *
	 * Getting this wrong is not cosmetic. A single LF inserted into a CRLF file is invisible in the
	 * editor and turns the next `git diff` into a whole-file rewrite for whoever has autocrlf on --
	 * one property change, four hundred modified lines, and a review nobody can read.
	 */
	FString DetectLineEnding(const FString& InText)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 CrLf = 0;
		int32 Lf = 0;
		for (int32 Offset = 0; Offset < Length; ++Offset)
		{
			if (Chars[Offset] == TEXT('\r'))
			{
				if (Offset + 1 < Length && Chars[Offset + 1] == TEXT('\n'))
				{
					++CrLf;
					++Offset;
				}
			}
			else if (Chars[Offset] == TEXT('\n'))
			{
				++Lf;
			}
		}

		if (CrLf == 0 && Lf == 0)
		{
			// No line breaks at all -- a one-line file, which has no habit to follow. LF is the
			// checked-in form of every .dui in the repository, so it is the least surprising guess.
			return TEXT("\n");
		}
		return CrLf >= Lf ? TEXT("\r\n") : TEXT("\n");
	}

	/**
	 * One level of indentation, inferred from the file rather than assumed.
	 *
	 * Only used when a block has no lines of its own to copy, which is the one case where there is
	 * nothing better to go on. A tab anywhere wins outright; otherwise the smallest non-zero indent
	 * in the file is the unit, which reads 4 from the reference sample and 2 from a file written by
	 * somebody who likes 2.
	 */
	FString DetectIndentUnit(const FString& InText)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();

		int32 Smallest = MAX_int32;
		int32 Offset = 0;
		while (Offset < Length)
		{
			int32 Indent = 0;
			while (Offset < Length && IsInlineWhitespace(Chars[Offset]))
			{
				if (Chars[Offset] == TEXT('\t'))
				{
					return TEXT("\t");
				}
				++Indent;
				++Offset;
			}

			// Blank lines are skipped: their "indentation" is trailing whitespace nobody typed on
			// purpose, and counting it would make the unit one space in half the files in existence.
			if (Offset < Length && !IsLineBreak(Chars[Offset]) && Indent > 0)
			{
				Smallest = FMath::Min(Smallest, Indent);
			}

			while (Offset < Length && !IsLineBreak(Chars[Offset]))
			{
				++Offset;
			}
			if (Offset < Length)
			{
				const TCHAR Break = Chars[Offset++];
				if (Break == TEXT('\r') && Offset < Length && Chars[Offset] == TEXT('\n'))
				{
					++Offset;
				}
			}
		}

		return Smallest == MAX_int32 ? FString(TEXT("    ")) : FString::ChrN(Smallest, TEXT(' '));
	}

	// --------------------------------------------------------------------------------------------
	// Measuring what is already in the text
	// --------------------------------------------------------------------------------------------

	/** Offset just past a comment starting at InOffset. A line comment stops AT its line break. */
	int32 SkipComment(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		if (Chars[InOffset + 1] == TEXT('/'))
		{
			int32 Offset = InOffset + 2;
			while (Offset < Length && !IsLineBreak(Chars[Offset]))
			{
				++Offset;
			}
			return Offset;
		}

		int32 Offset = InOffset + 2;
		while (Offset + 1 < Length)
		{
			if (Chars[Offset] == TEXT('*') && Chars[Offset + 1] == TEXT('/'))
			{
				return Offset + 2;
			}
			++Offset;
		}
		return Length;
	}

	/** Offset just past a string literal starting at its opening quote, or INDEX_NONE if it never closes. */
	int32 MeasureString(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Offset = InOffset + 1;
		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('\\') && Offset + 1 < Length)
			{
				// The escape is stepped over whole without being interpreted. What \q means is the
				// lexer's business (it keeps the backslash, so that a round trip is lossless); all
				// this needs to know is that the next character cannot close the string.
				Offset += 2;
				continue;
			}
			if (Char == TEXT('"'))
			{
				return Offset + 1;
			}
			if (IsLineBreak(Char))
			{
				return INDEX_NONE;
			}
			++Offset;
		}
		return INDEX_NONE;
	}

	/** Offset just past a parenthesised tuple starting at its '(', or INDEX_NONE if it never closes. */
	int32 MeasureTuple(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Depth = 0;
		int32 Offset = InOffset;
		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('"'))
			{
				const int32 End = MeasureString(InText, Offset);
				if (End == INDEX_NONE)
				{
					return INDEX_NONE;
				}
				Offset = End;
				continue;
			}
			if (StartsComment(Chars, Length, Offset))
			{
				Offset = SkipComment(InText, Offset);
				continue;
			}
			if (Char == TEXT('('))
			{
				++Depth;
				++Offset;
				continue;
			}
			if (Char == TEXT(')'))
			{
				++Offset;
				if (--Depth == 0)
				{
					return Offset;
				}
				continue;
			}
			if (Char == TEXT('}'))
			{
				// The same rule the parser uses: a '}' ends the hunt for a ')'. Running past it would
				// swallow the rest of the enclosing node and measure a "value" spanning half the file.
				return INDEX_NONE;
			}
			++Offset;
		}
		return INDEX_NONE;
	}

	/**
	 * Offset just past the value that starts at InOffset, or INDEX_NONE if there is not one there.
	 *
	 * This is the measurement the whole replace path rests on, and it is why an edit keeps the
	 * author's trailing comment: only the value's own characters are replaced, so the alignment
	 * before it and the `@key("...")` or `// note` after it are never in the edited range at all.
	 * A patcher that replaced "from the '=' to the end of the line" would eat both.
	 */
	int32 MeasureValue(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		if (InOffset < 0 || InOffset >= Length)
		{
			return INDEX_NONE;
		}

		const TCHAR First = Chars[InOffset];
		if (First == TEXT('"'))
		{
			return MeasureString(InText, InOffset);
		}
		if (First == TEXT('('))
		{
			return MeasureTuple(InText, InOffset);
		}

		// Identifier, number, asset path and hex colour are all one unbroken run. They are measured
		// together rather than told apart because the AST already knows which one it is -- the only
		// thing left to find is where it stops, and they all stop at the same characters.
		int32 Offset = InOffset;
		if (First == TEXT('#'))
		{
			++Offset; // The '#' is a delimiter, like a quote; the digits are the token.
		}
		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (IsInlineWhitespace(Char) || IsLineBreak(Char))
			{
				break;
			}
			if (Char == TEXT(',') || Char == TEXT(')') || Char == TEXT('(')
				|| Char == TEXT('{') || Char == TEXT('}')
				|| Char == TEXT(';') || Char == TEXT('@') || Char == TEXT('"'))
			{
				break;
			}
			if (StartsComment(Chars, Length, Offset))
			{
				break;
			}
			++Offset;
		}
		return Offset > InOffset ? Offset : INDEX_NONE;
	}

	/**
	 * Find the `{ ... }` belonging to a header that starts at InHeaderOffset.
	 *
	 * The brace has to be on the header's own line, and that is the grammar's rule rather than a
	 * shortcut: the parser checks for OpenBrace immediately after the header with no separator
	 * skipping, so a brace on the next line is not this node's block -- it is a syntax error. Which
	 * means "no brace before the line ends" is a reliable answer to "this node has no block", and
	 * that answer is what tells the insert path it has to create one.
	 */
	bool FindBlock(const FString& InText, int32 InHeaderOffset, int32& OutOpen, int32& OutClose)
	{
		OutOpen = INDEX_NONE;
		OutClose = INDEX_NONE;

		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();

		int32 Offset = InHeaderOffset;
		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (IsLineBreak(Char) || Char == TEXT(';'))
			{
				return false;
			}
			if (StartsComment(Chars, Length, Offset))
			{
				if (Chars[Offset + 1] == TEXT('/'))
				{
					return false; // The rest of the line is a comment, so no brace can follow on it.
				}
				Offset = SkipComment(InText, Offset);
				continue;
			}
			if (Char == TEXT('"'))
			{
				const int32 End = MeasureString(InText, Offset);
				if (End == INDEX_NONE)
				{
					return false;
				}
				Offset = End;
				continue;
			}
			if (Char == TEXT('{'))
			{
				OutOpen = Offset;
				break;
			}
			if (Char == TEXT('}'))
			{
				return false;
			}
			++Offset;
		}

		if (OutOpen == INDEX_NONE)
		{
			return false;
		}

		int32 Depth = 0;
		Offset = OutOpen;
		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('"'))
			{
				const int32 End = MeasureString(InText, Offset);
				if (End == INDEX_NONE)
				{
					return false;
				}
				Offset = End;
				continue;
			}
			if (StartsComment(Chars, Length, Offset))
			{
				Offset = SkipComment(InText, Offset);
				continue;
			}
			if (Char == TEXT('{'))
			{
				++Depth;
			}
			else if (Char == TEXT('}'))
			{
				if (--Depth == 0)
				{
					OutClose = Offset;
					return true;
				}
			}
			++Offset;
		}

		// An unbalanced block cannot come from a file that parsed, so this is a stale AST, and the
		// caller says so. Editing it anyway would append a line into whatever the '{' really opened.
		return false;
	}

	/** Offset just past the last non-whitespace character of the header line, ignoring its comment. */
	int32 HeaderEnd(const FString& InText, int32 InHeaderOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Offset = InHeaderOffset;
		int32 End = InHeaderOffset;
		while (Offset < Length)
		{
			const TCHAR Char = Chars[Offset];
			if (IsLineBreak(Char) || Char == TEXT(';') || StartsComment(Chars, Length, Offset))
			{
				break;
			}
			if (!IsInlineWhitespace(Char))
			{
				End = Offset + 1;
			}
			++Offset;
		}
		return End;
	}

	/** First statement character inside a block, comments included, or INDEX_NONE when it is empty. */
	int32 FirstStatementInBlock(const FString& InText, int32 InOpen, int32 InClose)
	{
		const TCHAR* Chars = *InText;
		int32 Offset = InOpen + 1;
		while (Offset < InClose)
		{
			const TCHAR Char = Chars[Offset];
			if (!IsInlineWhitespace(Char) && !IsLineBreak(Char) && Char != TEXT(';'))
			{
				return Offset;
			}
			++Offset;
		}
		return INDEX_NONE;
	}

	// --------------------------------------------------------------------------------------------
	// Finding things in the AST
	// --------------------------------------------------------------------------------------------

	const FDreamUINode* FindNodeById(const FDreamUIAst& InAst, const FString& InNodeId)
	{
		const FDreamUINode* Found = nullptr;
		InAst.ForEachNode([&Found, &InNodeId](const FDreamUINode& InNode)
		{
			// FString's own comparison, so case insensitive -- the same rule the parser's duplicate
			// check uses, and for the same reason: these ids become FNames, which do not distinguish
			// case. Matching case sensitively here would refuse an edit the file would have accepted.
			if (Found == nullptr && !InNode.Id.IsEmpty() && InNode.Id == InNodeId)
			{
				Found = &InNode;
			}
		});
		// ForEachNode walks loop bodies too, so a node inside `each Slot in GetSlots()` is found and
		// patched like any other. It is written once in the file and expanded N times at build, and
		// the file is what this edits.
		return Found;
	}

	/**
	 * The property that decides the built value, which is the LAST one with this name, not the first.
	 *
	 * A block may name a property twice -- the parser allows it and the builder applies them in
	 * order, so the last write wins. Patching the first would leave the file saying one thing and
	 * the editor showing another, with the difference two lines apart and invisible.
	 */
	const FDreamUIProperty* FindProperty(const TArray<FDreamUIProperty>& InProperties, const FString& InName)
	{
		const FDreamUIProperty* Found = nullptr;
		for (const FDreamUIProperty& Property : InProperties)
		{
			if (Property.Name == InName)
			{
				Found = &Property;
			}
		}
		return Found;
	}

	// --------------------------------------------------------------------------------------------
	// Splices
	// --------------------------------------------------------------------------------------------

	/** Replace [Offset, Offset + Length) with Text. A pure insert has Length 0. */
	struct FSplice
	{
		int32 Offset = 0;
		int32 Length = 0;
		FString Text;
		/** The order this splice was planned in. See the sort in Apply for what it decides. */
		int32 Order = 0;
	};

	bool Apply(FString& InOutText, TArray<FSplice>& InSplices)
	{
		// Back to front. Every offset in here was measured against the text as it arrived, and an
		// edit only moves the characters AFTER it, so applying from the end means no offset is ever
		// consulted once the ground under it has shifted. The alternative -- applying forwards and
		// adding a running delta to every later offset -- is the same algorithm with one more thing
		// to get wrong, and the way it goes wrong is an edit landing one line off.
		//
		// Ties are broken by planning order, descending, so that two splices at the same offset come
		// out in the order they were asked for: the later one is applied first and ends up to the
		// right of the earlier one. That is what makes `Text OkText` grow ` {` and its block body in
		// the right order when both are inserted at the same end-of-header offset.
		InSplices.Sort([](const FSplice& InLeft, const FSplice& InRight)
		{
			return InLeft.Offset != InRight.Offset ? InLeft.Offset > InRight.Offset : InLeft.Order > InRight.Order;
		});

		int32 LowestTouched = MAX_int32;
		for (const FSplice& Splice : InSplices)
		{
			if (Splice.Offset < 0 || Splice.Offset + Splice.Length > InOutText.Len())
			{
				return false;
			}
			if (Splice.Offset + Splice.Length > LowestTouched)
			{
				// Two splices over the same characters. Impossible from well formed input (duplicate
				// targets are refused before planning), so it means the locations disagreed with the
				// text, and the caller reports that rather than letting one edit chew the other.
				return false;
			}
			LowestTouched = FMath::Min(LowestTouched, Splice.Offset);

			InOutText = InOutText.Left(Splice.Offset) + Splice.Text + InOutText.Mid(Splice.Offset + Splice.Length);
		}
		return true;
	}

	// --------------------------------------------------------------------------------------------
	// Refusals
	// --------------------------------------------------------------------------------------------

	void RefuseTarget(FDreamUIDiagnosticBag& OutDiagnostics, const FDreamUISourceLocation& InLocation, FString InMessage)
	{
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::PatchTargetNotFound, InLocation, MoveTemp(InMessage));
	}

	void RefuseStale(FDreamUIDiagnosticBag& OutDiagnostics, const FDreamUISourceLocation& InLocation, FString InMessage)
	{
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::SourceFileChangedUnderEdit, InLocation,
			FString::Printf(TEXT("%s -- the text no longer matches the tree it was parsed from, so nothing was written"),
				*InMessage));
	}

	/**
	 * Is the text at InOffset the thing the AST says is there?
	 *
	 * The one guard that stands between a stale AST and a mangled file. It costs a substring compare
	 * per edit and it catches the whole family at once: an AST parsed from a different file, an AST
	 * from before another edit moved these lines, an AST from a version the user has since reverted
	 * on disk. Without it the failure is silent and the evidence is a corrupted layout file.
	 */
	bool TextAtIs(const FString& InText, int32 InOffset, const FString& InExpected)
	{
		if (InOffset < 0 || InExpected.IsEmpty() || InOffset + InExpected.Len() > InText.Len())
		{
			return false;
		}
		return InText.Mid(InOffset, InExpected.Len()) == InExpected;
	}

	/** The head of a dotted path: what `AnchorData.SizeDelta` actually starts with in the file. */
	FString FirstSegmentOf(const FString& InName)
	{
		int32 Dot = INDEX_NONE;
		return InName.FindChar(TEXT('.'), Dot) ? InName.Left(Dot) : InName;
	}

	/**
	 * Would this text read back as the value it is being written as?
	 *
	 * Three checks, and each one is a file-corrupting bug the day it is missing:
	 *
	 * 1. Non-finite floats. A float property holding inf or NaN prints through %g as `inf`, which
	 *    the lexer reads as an IDENTIFIER, not a number -- so the file saves, and reopening it fails
	 *    with a type mismatch on a line the author never wrote. The implementation plan called this
	 *    out as the thing to settle before write-back existed, and refusing at the write is the
	 *    option it left open: teaching the lexer the words `inf` and `nan` would put two floating
	 *    point spellings into a user interface layout language forever.
	 *
	 * 2. The grammar itself, via a throwaway parse. The judge of what a value is has to BE the
	 *    parser -- a second opinion written here would drift from it, and the drift would show up as
	 *    a file that this component wrote and the compiler will not read.
	 *
	 * 3. That the value is the WHOLE of the text handed over. `24 // hi` parses perfectly well and
	 *    splicing it in would comment out the rest of the author's line, closing brace included.
	 */
	bool ValidateValueText(const FString& InValueText, const FDreamUISourceLocation& InLocation,
		FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FString Trimmed = InValueText.TrimStartAndEnd();

		// (1) The non-finite spellings, matched exactly rather than by prefix so that an enum value
		// that merely begins with those letters is not caught by it.
		// MARKER: this is one of the two sites to move to a dedicated DUI7xxx code, if one is added.
		{
			FString Bare = Trimmed.ToLower();
			if (Bare.StartsWith(TEXT("-")) || Bare.StartsWith(TEXT("+")))
			{
				Bare.RightChopInline(1);
			}
			static const TCHAR* NonFinite[] =
			{
				TEXT("inf"), TEXT("infinity"), TEXT("nan"), TEXT("nan(ind)"),
				TEXT("1.#inf"), TEXT("1.#ind"), TEXT("1.#qnan"), TEXT("1.#snan"),
			};
			for (const TCHAR* Spelling : NonFinite)
			{
				if (Bare == Spelling)
				{
					OutDiagnostics.AddError(EDreamUIDiagnosticCode::MalformedNumber, InLocation,
						FString::Printf(TEXT("'%s' cannot be written into a .dui: the file would not parse back, so the value was not written"),
							*Ellipsize(Trimmed)));
					return false;
				}
			}
		}

		if (Trimmed.Contains(TEXT("\n")) || Trimmed.Contains(TEXT("\r")))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::UnexpectedToken, InLocation,
				TEXT("a value written back has to fit on one line"));
			return false;
		}

		// (2) The grammar's own opinion, asked in the smallest legal file that can hold a value.
		const FString Probe = FString::Printf(TEXT("Widget DreamUIPatchProbe {\n\tP = %s\n}"), *Trimmed);
		FDreamUIAst ProbeAst;
		FDreamUIDiagnosticBag ProbeDiagnostics;
		if (!FDreamUISourceFile::Parse(Probe, FString(), ProbeAst, ProbeDiagnostics))
		{
			// The probe's own code is forwarded, not replaced: the cause really is the one the
			// parser named, and a reader who looks up DUI1002 finds the page describing exactly what
			// is wrong with the value. Only the location is rewritten, to the line being edited --
			// the probe's own line 2 would name a file that does not exist.
			// MARKER: the second of the two sites, if a dedicated DUI7xxx code is added.
			const FDreamUIDiagnostic* First = ProbeDiagnostics.Diagnostics.FindByPredicate(
				[](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.IsError(); });
			OutDiagnostics.AddError(First != nullptr ? First->Code : EDreamUIDiagnosticCode::UnexpectedToken, InLocation,
				FString::Printf(TEXT("'%s' is not a value this file can hold: %s"),
					*Ellipsize(Trimmed), First != nullptr ? *First->Message : TEXT("it does not parse")));
			return false;
		}

		// (3) One value, all of it.
		if (MeasureValue(Trimmed, 0) != Trimmed.Len())
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::UnexpectedToken, InLocation,
				FString::Printf(TEXT("'%s' is more than one value, so writing it would change the rest of the line"),
					*Ellipsize(Trimmed)));
			return false;
		}

		return true;
	}

	// --------------------------------------------------------------------------------------------
	// Planning one edit
	// --------------------------------------------------------------------------------------------

	/** Everything about where an edit lands, resolved from the AST and verified against the text. */
	struct FResolvedTarget
	{
		/** Where the header that owns the block starts: the node's type word, or the component's '+'. */
		int32 OwnerOffset = INDEX_NONE;
		/** The array a matching property would be in. */
		const TArray<FDreamUIProperty>* Properties = nullptr;
		/** Every property statement written inside this block, whichever notation wrote it. */
		TArray<const FDreamUIProperty*> Statements;
		/** "" or "@slot ", written in front of the name on an inserted line. */
		FString LinePrefix;
		/** Where a diagnostic about this target points. */
		FDreamUISourceLocation Location;
	};

	bool ResolveTarget(const FString& InText, const FDreamUIAst& InAst, const FDreamUIPropertyEdit& InEdit,
		FResolvedTarget& OutTarget, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named '%s'"), *Ellipsize(InEdit.NodeId)));
			return false;
		}

		OutTarget.Location = Node->Location;

		if (Node->Kind == EDreamUINodeKind::NamedSlot)
		{
			// Not an oversight and not a TODO: `slot Footer` declares a hole, and the grammar refuses
			// a block on it outright. There is nowhere in the file for the property to go, and
			// writing one anyway would produce a .dui that no longer parses -- the single outcome
			// this component is not allowed to have. If named slots ever need properties, the
			// grammar has to grow a block for them first.
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("'%s' is a named slot: the grammar gives it no block, so it can hold no properties"),
					*Node->Id));
			return false;
		}

		const int32 NodeOffset = OffsetOf(InText, Node->Location);
		if (!TextAtIs(InText, NodeOffset, Node->TypeName))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("line %d does not begin with '%s'"), Node->Location.Line, *Node->TypeName));
			return false;
		}

		if (InEdit.Target == EDreamUIPatchTarget::Component)
		{
			if (!Node->Components.IsValidIndex(InEdit.ComponentIndex))
			{
				RefuseTarget(OutDiagnostics, Node->Location,
					FString::Printf(TEXT("node '%s' has %d '+' blocks, so there is no number %d"),
						*Node->Id, Node->Components.Num(), InEdit.ComponentIndex));
				return false;
			}

			const FDreamUIComponent& Component = Node->Components[InEdit.ComponentIndex];
			const int32 ComponentOffset = OffsetOf(InText, Component.Location);
			if (!TextAtIs(InText, ComponentOffset, TEXT("+")))
			{
				RefuseStale(OutDiagnostics, Component.Location,
					FString::Printf(TEXT("line %d does not begin with the '+' of '%s'"),
						Component.Location.Line, *Component.ClassName));
				return false;
			}

			OutTarget.OwnerOffset = ComponentOffset;
			OutTarget.Properties = &Component.Properties;
			for (const FDreamUIProperty& Property : Component.Properties)
			{
				OutTarget.Statements.Add(&Property);
			}
			OutTarget.Location = Component.Location;
			return true;
		}

		OutTarget.OwnerOffset = NodeOffset;
		OutTarget.Properties = InEdit.Target == EDreamUIPatchTarget::Slot ? &Node->SlotProperties : &Node->Properties;
		OutTarget.LinePrefix = InEdit.Target == EDreamUIPatchTarget::Slot ? TEXT("@slot ") : TEXT("");

		// Both notations are counted as statements of this block, because both are property LINES and
		// the insert wants to land after the last of them. Keeping the bare names and the @slot lines
		// in two groups would mean an inserted @slot jumping above properties the author wrote below
		// it, which reads as a reordering nobody asked for.
		for (const FDreamUIProperty& Property : Node->Properties)
		{
			OutTarget.Statements.Add(&Property);
		}
		for (const FDreamUIProperty& Property : Node->SlotProperties)
		{
			OutTarget.Statements.Add(&Property);
		}
		return true;
	}

	/** Where a property statement's text stops, so the insert after it clears its whole line. */
	int32 StatementSearchOffset(const FString& InText, const FDreamUIProperty& InProperty, int32 InNameOffset)
	{
		if (InProperty.IsBinding())
		{
			return InNameOffset;
		}
		const int32 ValueOffset = OffsetOf(InText, InProperty.Value.Location);
		if (ValueOffset == INDEX_NONE)
		{
			return InNameOffset;
		}
		// The end of the VALUE, not of the name: a tuple is allowed to break across lines, and an
		// insert measured from the name would land in the middle of one.
		const int32 ValueEnd = MeasureValue(InText, ValueOffset);
		return ValueEnd == INDEX_NONE ? InNameOffset : ValueEnd;
	}

	bool PlanReplace(const FString& InText, const FDreamUIPropertyEdit& InEdit, const FDreamUIProperty& InProperty,
		bool bInSlotNotation, TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InProperty.IsBinding())
		{
			// Refused rather than converted to `= value`. The details panel shows a bound property's
			// CURRENT value like any other, so one drag on an anchor would turn `AnchorData.SizeDelta
			// <- GetSize()` into a literal and delete behaviour the author wrote, to store a number
			// the binding was going to overwrite on the next tick anyway. Making it a refusal puts
			// the decision where it belongs: in the text, where the author can see the arrow.
			RefuseTarget(OutDiagnostics, InProperty.Location,
				FString::Printf(TEXT("'%s' is bound with '<- %s()': a value written back would silently replace the binding"),
					*InProperty.Name, *InProperty.BindingFunction));
			return false;
		}

		const int32 NameOffset = OffsetOf(InText, InProperty.Location);
		const FString Expected = bInSlotNotation ? FString(TEXT("@")) : FirstSegmentOf(InProperty.Name);
		if (!TextAtIs(InText, NameOffset, Expected))
		{
			RefuseStale(OutDiagnostics, InProperty.Location,
				FString::Printf(TEXT("line %d does not hold '%s'"), InProperty.Location.Line, *InProperty.Name));
			return false;
		}

		const int32 ValueOffset = OffsetOf(InText, InProperty.Value.Location);
		const int32 ValueEnd = MeasureValue(InText, ValueOffset);
		if (ValueOffset == INDEX_NONE || ValueEnd == INDEX_NONE)
		{
			RefuseStale(OutDiagnostics, InProperty.Value.Location,
				FString::Printf(TEXT("the value of '%s' is not where the tree says it is"), *InProperty.Name));
			return false;
		}

		// The measured slice is compared against what the parser recorded, which is a free and very
		// strong check: Raw is the source text verbatim for four of the six kinds, and IS the whole
		// parenthesised slice for a tuple. A stale AST hardly ever survives it.
		const FString Slice = InText.Mid(ValueOffset, ValueEnd - ValueOffset);
		bool bSliceAgrees = true;
		switch (InProperty.Value.Kind)
		{
		case EDreamUIValueKind::Identifier:
		case EDreamUIValueKind::Number:
		case EDreamUIValueKind::AssetPath:
		case EDreamUIValueKind::Tuple:
			bSliceAgrees = Slice.Equals(InProperty.Value.Raw, ESearchCase::CaseSensitive);
			break;
		case EDreamUIValueKind::HexColor:
			bSliceAgrees = Slice.Equals(FString(TEXT("#")) + InProperty.Value.Raw, ESearchCase::CaseSensitive);
			break;
		case EDreamUIValueKind::String:
			// Raw is unescaped, so it cannot be compared to the source. The delimiters are all there
			// is to check, and MeasureString already proved the closing one exists.
			bSliceAgrees = Slice.StartsWith(TEXT("\""), ESearchCase::CaseSensitive);
			break;
		}
		if (!bSliceAgrees)
		{
			RefuseStale(OutDiagnostics, InProperty.Value.Location,
				FString::Printf(TEXT("the text of '%s' reads '%s' where the tree says '%s'"),
					*InProperty.Name, *Ellipsize(Slice), *Ellipsize(InProperty.Value.Raw)));
			return false;
		}

		const FString NewValue = InEdit.NewValueText.TrimStartAndEnd();
		if (Slice.Equals(NewValue, ESearchCase::CaseSensitive))
		{
			// Already says it. No splice at all, which is what makes a second save produce a byte
			// identical file -- and it is the reason idempotence here is a property of the algorithm
			// rather than a test that happens to pass.
			return true;
		}

		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = ValueOffset;
		Splice.Length = ValueEnd - ValueOffset;
		Splice.Text = NewValue;
		Splice.Order = InOutOrder++;
		return true;
	}

	bool PlanInsert(const FString& InText, const FDreamUIPropertyEdit& InEdit, const FResolvedTarget& InTarget,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FString LineEnding = DetectLineEnding(InText);
		const FString OwnerIndent = IndentAt(InText, InTarget.OwnerOffset);
		const FString NewLine = InTarget.LinePrefix + InEdit.PropertyName + TEXT(" = ") + InEdit.NewValueText.TrimStartAndEnd();

		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		if (!FindBlock(InText, InTarget.OwnerOffset, Open, Close))
		{
			// No block at all -- `Text OkText` on a line of its own, or `+ UIButton` with nothing
			// after it. Both are legal and both are common, so this is not an error case: the block
			// gets written, opening brace on the header line where the grammar requires it.
			if (InTarget.Statements.Num() > 0)
			{
				// Except when the tree says this node already has properties, which can only be
				// inside a block. Text and tree disagree, so this is a stale tree, and writing a
				// second block onto a node that has one would leave the file unparseable.
				RefuseStale(OutDiagnostics, InTarget.Location,
					TEXT("the tree has properties for this node and the text has no block to hold them"));
				return false;
			}

			const FString Indent = OwnerIndent + DetectIndentUnit(InText);
			const int32 End = HeaderEnd(InText, InTarget.OwnerOffset);
			if (End <= InTarget.OwnerOffset)
			{
				RefuseStale(OutDiagnostics, InTarget.Location, TEXT("the header this property belongs to is not there"));
				return false;
			}

			// Two splices, and their planning order is what puts them in the right sequence when the
			// header has no trailing comment and both land on the same offset. See the sort in Apply.
			FSplice& Brace = OutSplices.AddDefaulted_GetRef();
			Brace.Offset = End;
			Brace.Length = 0;
			Brace.Text = TEXT(" {");
			Brace.Order = InOutOrder++;

			FSplice& Body = OutSplices.AddDefaulted_GetRef();
			Body.Offset = FindLineEnd(InText, End);
			Body.Length = 0;
			Body.Text = LineEnding + Indent + NewLine + LineEnding + OwnerIndent + TEXT("}");
			Body.Order = InOutOrder++;
			return true;
		}

		// The anchor is the last property statement already in this block. Properties go before the
		// subtree and before the '+' blocks on purpose: a file where the properties of a node are
		// interleaved with its children is one nobody can read the shape of at a glance, and since
		// the AST hands over each statement's location, "last property" is a question with an answer
		// rather than a guess.
		const FDreamUIProperty* Anchor = nullptr;
		int32 AnchorOffset = INDEX_NONE;
		for (const FDreamUIProperty* Property : InTarget.Statements)
		{
			const int32 Offset = OffsetOf(InText, Property->Location);
			if (Offset > AnchorOffset && Offset > Open && Offset < Close)
			{
				Anchor = Property;
				AnchorOffset = Offset;
			}
		}

		// Indentation is copied from a line the block already has, and only invented when it has
		// none. Two separate questions, deliberately: WHERE the line goes is decided by the last
		// property, HOW FAR IN it sits is decided by whatever the author already indented in here --
		// including a child node, when there are no properties yet to copy.
		int32 SearchFrom = Open;
		int32 IndentSource = INDEX_NONE;
		if (Anchor != nullptr)
		{
			SearchFrom = StatementSearchOffset(InText, *Anchor, AnchorOffset);
			IndentSource = AnchorOffset;
		}
		else
		{
			IndentSource = FirstStatementInBlock(InText, Open, Close);
		}

		// A block written on one line has no indentation worth copying -- the "line" of everything
		// inside it is the header's line, so copying it would put the new property level with the
		// node that owns it. One level in from the header is the only sensible answer there.
		const bool bIndentSourceIsOwnLine = IndentSource != INDEX_NONE
			&& FindLineStart(InText, IndentSource) != FindLineStart(InText, InTarget.OwnerOffset);
		const FString Indent = bIndentSourceIsOwnLine
			? IndentAt(InText, IndentSource)
			: OwnerIndent + DetectIndentUnit(InText);

		const int32 InsertAt = FindLineEnd(InText, SearchFrom);
		if (Close < InsertAt)
		{
			// `Widget X { }` and `+ UIButton { A = 1 }`: the closing brace is on this line, so the end
			// of the line is OUTSIDE the block. The new line goes in front of the brace instead, and
			// the brace is pushed down onto its own line -- which is also what turns a one-line block
			// into an ordinary one the first time somebody adds to it.
			int32 BraceStart = Close;
			while (BraceStart > Open + 1 && IsInlineWhitespace(InText[BraceStart - 1]))
			{
				--BraceStart;
			}

			FSplice& Splice = OutSplices.AddDefaulted_GetRef();
			Splice.Offset = BraceStart;
			Splice.Length = Close - BraceStart;
			Splice.Text = LineEnding + Indent + NewLine + LineEnding + OwnerIndent;
			Splice.Order = InOutOrder++;
			return true;
		}

		// The ordinary case, and the one the brace-tree grammar was chosen for: find the line, put a
		// line after it. No reflowing, no bracket counting, no decision about where to break.
		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = InsertAt;
		Splice.Length = 0;
		Splice.Text = LineEnding + Indent + NewLine;
		Splice.Order = InOutOrder++;
		return true;
	}

	bool PlanEdit(const FString& InText, const FDreamUIAst& InAst, const FDreamUIPropertyEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InEdit.PropertyName.TrimStartAndEnd().IsEmpty())
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(), TEXT("a property edit with no property name"));
			return false;
		}

		FResolvedTarget Target;
		if (!ResolveTarget(InText, InAst, InEdit, Target, OutDiagnostics))
		{
			return false;
		}

		const FDreamUIProperty* Existing = FindProperty(*Target.Properties, InEdit.PropertyName);

		// Validated before anything is planned, and against the line it would land on, so the
		// diagnostic points at the file the author is looking at rather than at the value in the
		// abstract.
		const FDreamUISourceLocation ValueLocation = Existing != nullptr ? Existing->Location : Target.Location;
		if (!ValidateValueText(InEdit.NewValueText, ValueLocation, OutDiagnostics))
		{
			return false;
		}

		if (Existing != nullptr)
		{
			return PlanReplace(InText, InEdit, *Existing, InEdit.Target == EDreamUIPatchTarget::Slot,
				OutSplices, InOutOrder, OutDiagnostics);
		}
		return PlanInsert(InText, InEdit, Target, OutSplices, InOutOrder, OutDiagnostics);
	}
}

bool FDreamUITextPatcher::SetProperty(FString& InOutText, const FDreamUIAst& InAst,
	const FString& InNodeId,
	EDreamUIPatchTarget InTarget, int32 InComponentIndex,
	const FString& InPropertyName, const FString& InNewValueText,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	FDreamUIPropertyEdit Edit;
	Edit.NodeId = InNodeId;
	Edit.Target = InTarget;
	Edit.ComponentIndex = InComponentIndex;
	Edit.PropertyName = InPropertyName;
	Edit.NewValueText = InNewValueText;

	// Forwarded rather than implemented twice. One edit is a batch of one, and the day the two
	// diverge is the day the single-edit path grows a bug the batch tests cannot see.
	return SetProperties(InOutText, InAst, TArrayView<const FDreamUIPropertyEdit>(&Edit, 1), OutDiagnostics);
}

bool FDreamUITextPatcher::SetProperties(FString& InOutText, const FDreamUIAst& InAst,
	TArrayView<const FDreamUIPropertyEdit> InEdits,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamUIPatchLocal;

	if (InEdits.Num() == 0)
	{
		return true;
	}

	if (!InAst.bHasRoot)
	{
		// A tree that never had a root came from a parse that failed. Editing the text it failed on
		// would be editing on the strength of locations that describe a file the parser gave up on.
		RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
			TEXT("this file did not parse into a tree, so there is nothing to write a property into"));
		return false;
	}

	// Two edits to one property are refused as a pair before either is planned. Both would be
	// measured against a text in which the property appears once, so applying them would either
	// write the line twice or splice one over the other -- and picking a winner here would be this
	// component deciding which of the caller's two values is the real one.
	TSet<int32> Duplicated;
	for (int32 Left = 0; Left < InEdits.Num(); ++Left)
	{
		for (int32 Right = Left + 1; Right < InEdits.Num(); ++Right)
		{
			const bool bSameTarget = InEdits[Left].NodeId == InEdits[Right].NodeId
				&& InEdits[Left].Target == InEdits[Right].Target
				&& (InEdits[Left].Target != EDreamUIPatchTarget::Component
					|| InEdits[Left].ComponentIndex == InEdits[Right].ComponentIndex)
				&& InEdits[Left].PropertyName == InEdits[Right].PropertyName;
			if (bSameTarget)
			{
				Duplicated.Add(Left);
				Duplicated.Add(Right);
			}
		}
	}

	bool bAllPlanned = true;
	int32 Order = 0;
	TArray<FSplice> Splices;
	for (int32 Index = 0; Index < InEdits.Num(); ++Index)
	{
		if (Duplicated.Contains(Index))
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("'%s' on node '%s' was given two values in one write; neither was written"),
					*InEdits[Index].PropertyName, *InEdits[Index].NodeId));
			bAllPlanned = false;
			continue;
		}
		if (!PlanEdit(InOutText, InAst, InEdits[Index], Splices, Order, OutDiagnostics))
		{
			// The rest of the batch is still planned and still applied. A designer flushing ten
			// dirty properties is better served by nine writes and one complaint than by losing all
			// ten to one of them -- and the one that failed is in the bag, named and located.
			bAllPlanned = false;
		}
	}

	if (Splices.IsEmpty())
	{
		return bAllPlanned;
	}

	FString Patched = InOutText;
	if (!Apply(Patched, Splices))
	{
		RefuseStale(OutDiagnostics, FDreamUISourceLocation(), TEXT("two edits landed on the same characters"));
		return false;
	}

	InOutText = MoveTemp(Patched);
	return bAllPlanned;
}
