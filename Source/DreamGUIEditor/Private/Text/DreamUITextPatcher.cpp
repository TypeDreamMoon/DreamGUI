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

	/**
	 * The lexer's identifier rule (IsIdentifierChar in DreamUISourceFile.cpp), copied for the reason the classes above
	 * are: the scans below decide where a WORD ends -- a node type, the `fill` of a shorthand, an `else` -- and a word
	 * that ends one character earlier here than in the lexer is an edit spliced into the middle of a name.
	 */
	FORCEINLINE bool IsIdentifierChar(TCHAR InChar)
	{
		return (InChar >= TEXT('0') && InChar <= TEXT('9'))
			|| InChar == TEXT('_')
			|| (InChar >= TEXT('a') && InChar <= TEXT('z'))
			|| (InChar >= TEXT('A') && InChar <= TEXT('Z'))
			|| InChar > 0x7F;
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

	/**
	 * True when nothing but whitespace or a comment stands on the line from InOffset on -- with bInAllowTerminators, a ';'
	 * too. A node or block whose lines are its own can be cut or given a block by the line; one that shares a line with
	 * another statement (`Text A; Text B`, `Button Ok { Text Label { } }`) cannot, and the line-wide cut took the other
	 * statement with it -- or the new block took it in.
	 */
	bool RestOfLineIsBlank(const FString& InText, int32 InOffset, bool bInAllowTerminators)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		for (int32 Offset = FMath::Max(0, InOffset); Offset < Length; ++Offset)
		{
			const TCHAR Char = Chars[Offset];
			if (IsLineBreak(Char) || StartsComment(Chars, Length, Offset))
			{
				return true;
			}
			if (!IsInlineWhitespace(Char) && !(bInAllowTerminators && Char == TEXT(';')))
			{
				return false;
			}
		}
		return true;
	}

	/** True when nothing but whitespace stands on InOffset's line before it. */
	bool LineBeforeIsBlank(const FString& InText, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		for (int32 Offset = FindLineStart(InText, InOffset); Offset < InOffset; ++Offset)
		{
			if (!IsInlineWhitespace(Chars[Offset]))
			{
				return false;
			}
		}
		return true;
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
		if (First == TEXT('#') || First == TEXT('@'))
		{
			// Both are delimiters, like a quote: '#' before a colour's digits, '@' before a resource
			// reference's name. Without this the '@' break below would measure a `@Accent` value as
			// zero characters, and a designer edit that bakes the reference to a literal would splice
			// itself in FRONT of the reference instead of over it.
			++Offset;
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

	/**
	 * Whether a node's TYPE is spelled at InOffset, and where it stops.
	 *
	 * The anchor every node edit is confirmed by, and it outgrew a plain substring compare the day a type could be more
	 * than one token. `nier.Row` and `@Row` reach the AST joined -- the parser glues `nier`, `.` and `Row` into one
	 * TypeName, and keeps the '@' of a resource type -- while the text may hold them with spaces between (`nier . Row`,
	 * `@ Row`), which the lexer reads the same. So whitespace is allowed where the lexer allows it, after a '.' or an
	 * '@' and before a '.', and nowhere else.
	 *
	 * The word must also END here: `TextBox Title` does not confirm a node the tree says is a `Text`, which the old
	 * prefix compare let through. That is the stale-tree guard doing its job one character further.
	 *
	 * OutEnd is the offset just past the type, which is where an id goes -- or, for an anonymous node, where one is
	 * written when it is given a name.
	 */
	bool MatchTypeAt(const FString& InText, int32 InOffset, const FString& InTypeName, int32& OutEnd)
	{
		OutEnd = INDEX_NONE;
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		if (InOffset < 0 || InOffset >= Length || InTypeName.IsEmpty())
		{
			return false;
		}

		int32 Cursor = InOffset;
		for (int32 Index = 0; Index < InTypeName.Len(); ++Index)
		{
			const TCHAR Expected = InTypeName[Index];
			// Inside an asset path a '.' is part of one unbroken token, and there is no whitespace to skip -- so the
			// skipping below is a no-op there and the dotted-tag case is the only one it changes.
			const bool bJoiner = Expected == TEXT('.') || (Expected == TEXT('@') && Index == 0);
			if (Expected == TEXT('.'))
			{
				while (Cursor < Length && IsInlineWhitespace(Chars[Cursor]))
				{
					++Cursor;
				}
			}
			// Case insensitive, as TextAtIs is: names are FNames downstream, and the parser keeps the spelling it read,
			// so the two only ever differ in a tree that is stale anyway.
			if (Cursor >= Length || FChar::ToLower(Chars[Cursor]) != FChar::ToLower(Expected))
			{
				return false;
			}
			++Cursor;
			if (bJoiner)
			{
				while (Cursor < Length && IsInlineWhitespace(Chars[Cursor]))
				{
					++Cursor;
				}
			}
		}
		if (Cursor < Length && (IsIdentifierChar(Chars[Cursor]) || Chars[Cursor] == TEXT('.') || Chars[Cursor] == TEXT('/')))
		{
			return false;
		}
		OutEnd = Cursor;
		return true;
	}

	/**
	 * How many blocks deep InOffset sits inside the block whose '{' is at InOpen: 0 for a statement of that block itself.
	 *
	 * The question every insert has to ask now that a block can hold blocks that are not nodes. An `if` holds the nodes
	 * of its branches, a `@slot { … }` holds slot properties, and the AST lowers both away -- a branch's nodes become the
	 * enclosing node's children, the slot block's lines its slot properties -- so "after the last child" or "after the
	 * last property" can name a line that sits INSIDE one of them. A new line written there would join the branch, or
	 * become a slot property, which is a different file from the one the caller asked for.
	 */
	int32 DepthWithin(const FString& InText, int32 InOpen, int32 InOffset)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		const int32 Stop = FMath::Min(InOffset, Length);
		int32 Depth = 0;
		int32 Offset = InOpen + 1;
		while (Offset < Stop)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('"'))
			{
				const int32 End = MeasureString(InText, Offset);
				if (End == INDEX_NONE)
				{
					break;
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
				Depth = FMath::Max(0, Depth - 1);
			}
			++Offset;
		}
		return Depth;
	}

	/** Offset of the next character that is not whitespace, a line break or a comment, from InOffset, or InStop. */
	int32 NextSignificant(const FString& InText, int32 InOffset, int32 InStop)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Offset = InOffset;
		while (Offset < InStop)
		{
			const TCHAR Char = Chars[Offset];
			if (IsInlineWhitespace(Char) || IsLineBreak(Char) || Char == TEXT(';'))
			{
				++Offset;
				continue;
			}
			if (StartsComment(Chars, Length, Offset))
			{
				Offset = SkipComment(InText, Offset);
				continue;
			}
			return Offset;
		}
		return InStop;
	}

	/** True when the keyword InWord, case sensitive like every keyword, stands whole at InOffset. */
	bool KeywordAt(const FString& InText, int32 InOffset, const TCHAR* InWord)
	{
		const int32 WordLength = FCString::Strlen(InWord);
		if (InOffset < 0 || InOffset + WordLength > InText.Len()
			|| !InText.Mid(InOffset, WordLength).Equals(InWord, ESearchCase::CaseSensitive))
		{
			return false;
		}
		return InOffset + WordLength >= InText.Len() || !IsIdentifierChar(InText[InOffset + WordLength]);
	}

	/**
	 * InOffset, moved out to the end of the line that closes whatever block construct it sits in, so that it is a place
	 * at the level of the block whose '{' is at InOpen. INDEX_NONE when no such line exists before InClose.
	 *
	 * An `if` is followed through its `else` and `else if` arms -- a line written between `}` and `else` would cut the
	 * chain in two, which no longer parses -- and the closing brace must end its line, or the place found would be in
	 * front of whatever statement shares it.
	 */
	int32 LeaveNestedConstructs(const FString& InText, int32 InOpen, int32 InClose, int32 InOffset)
	{
		int32 Depth = DepthWithin(InText, InOpen, InOffset);
		if (Depth == 0)
		{
			return InOffset;
		}

		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Offset = InOffset;
		while (Offset < InClose)
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
			if (Char == TEXT('{'))
			{
				++Depth;
			}
			else if (Char == TEXT('}'))
			{
				if (--Depth == 0)
				{
					// `else` is a keyword only before its '{' or `if` -- the parser's own rule, so that a property
					// called else still reads as one.
					const int32 Next = NextSignificant(InText, Offset + 1, InClose);
					const int32 AfterElse = NextSignificant(InText, Next + 4, InClose);
					if (KeywordAt(InText, Next, TEXT("else"))
						&& ((AfterElse < InClose && Chars[AfterElse] == TEXT('{')) || KeywordAt(InText, AfterElse, TEXT("if"))))
					{
						Offset = Next + 4;
						continue;
					}
					if (!RestOfLineIsBlank(InText, Offset + 1, /*bInAllowTerminators*/ true))
					{
						return INDEX_NONE;
					}
					return FindLineEnd(InText, Offset);
				}
			}
			++Offset;
		}
		return INDEX_NONE;
	}

	/**
	 * The '@' of the last `@slot { … }` block written directly in the block whose '{' is at InOpen, or INDEX_NONE.
	 *
	 * Found in the text because the AST does not keep it: the block's lines arrive as the node's slot properties, like
	 * any `@slot Name = Value` line, and nothing records where the block itself stood. It is where a new slot property
	 * goes when the node has one -- the author chose to group them, and a stray `@slot` line under the group is the kind
	 * of edit that reads as the tool not knowing what it was looking at.
	 */
	int32 FindSlotBlock(const FString& InText, int32 InOpen, int32 InClose)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Found = INDEX_NONE;
		int32 Depth = 0;
		int32 Offset = InOpen + 1;
		while (Offset < InClose)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('"'))
			{
				const int32 End = MeasureString(InText, Offset);
				if (End == INDEX_NONE)
				{
					return Found;
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
				Depth = FMath::Max(0, Depth - 1);
			}
			else if (Char == TEXT('@') && Depth == 0)
			{
				int32 Cursor = Offset + 1;
				while (Cursor < InClose && IsInlineWhitespace(Chars[Cursor]))
				{
					++Cursor;
				}
				if (KeywordAt(InText, Cursor, TEXT("slot")))
				{
					Cursor += 4;
					while (Cursor < InClose && IsInlineWhitespace(Chars[Cursor]))
					{
						++Cursor;
					}
					if (Cursor < InClose && Chars[Cursor] == TEXT('{'))
					{
						Found = Offset;
					}
				}
			}
			++Offset;
		}
		return Found;
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

	/** The node whose Children hold InChild, or null for the root and for a node of another tree. */
	const FDreamUINode* FindParentIn(const FDreamUINode& InScope, const FDreamUINode* InChild, int32 InDepth)
	{
		// The same crash guard ForEachNode has: a parsed tree cannot reach it, a hand-assembled one might.
		if (InDepth >= DreamUIAst::MaxNestingDepth)
		{
			return nullptr;
		}
		for (const FDreamUINode& Child : InScope.Children)
		{
			if (&Child == InChild)
			{
				return &InScope;
			}
			if (const FDreamUINode* Found = FindParentIn(Child, InChild, InDepth + 1))
			{
				return Found;
			}
		}
		return nullptr;
	}

	const FDreamUINode* FindParentOf(const FDreamUIAst& InAst, const FDreamUINode* InChild)
	{
		return InAst.bHasRoot ? FindParentIn(InAst.Root, InChild, 0) : nullptr;
	}

	/**
	 * The word a node's header starts with: its type, or the `slot` keyword -- TypeName is empty on a named slot. Loops
	 * are never asked: they have no id, so nothing addresses one.
	 */
	FString HeaderWordOf(const FDreamUINode& InNode)
	{
		return InNode.Kind == EDreamUINodeKind::NamedSlot ? FString(TEXT("slot")) : InNode.TypeName;
	}

	/** True when the text at InNode's location begins with its header word. OutTypeEnd: just past that word. */
	bool MatchNodeHeader(const FString& InText, const FDreamUINode& InNode, int32& OutTypeEnd)
	{
		return MatchTypeAt(InText, OffsetOf(InText, InNode.Location), HeaderWordOf(InNode), OutTypeEnd);
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
		/**
		 * For the body of a block written onto a node that had none (PlanInsert), the offset of that node's header: another
		 * property for the same node in the same batch joins this body instead of writing a block of its own.
		 */
		int32 SynthesizedBlockOwner = INDEX_NONE;
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

	/**
	 * A `rows` line asked for something only a node header could give: a value outside its columns, a slot line, a `+`
	 * block, or a place in the structure. Said rather than attempted -- the row's "header" is a run of values, and the
	 * edit would have to invent the line it needs inside a table.
	 */
	void RefuseRowLine(FDreamUIDiagnosticBag& OutDiagnostics, const FDreamUINode& InRow, const FString& InWhat)
	{
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::PatchSyntaxNotWritable, InRow.Location,
			FString::Printf(TEXT("%s: '%s' is a line of a 'rows' table, which spells only its columns -- write it in the text (the table's style, or a block at the end of that line)"),
				*InWhat, *InRow.Id));
	}

	/** True when InNode holds a `rows` table: a place the designer cannot put a node beside without landing inside it. */
	bool HoldsRows(const FDreamUINode& InNode)
	{
		return InNode.Children.ContainsByPredicate([](const FDreamUINode& InChild) { return !InChild.RowKey.IsEmpty(); });
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
		/** The node the edit is on; null for a style. */
		const FDreamUINode* Node = nullptr;
	};

	bool ResolveTarget(const FString& InText, const FDreamUIAst& InAst, const FDreamUIPropertyEdit& InEdit,
		FResolvedTarget& OutTarget, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InEdit.Target == EDreamUIPatchTarget::Style)
		{
			// A style block is a node-shaped thing with no type and no children: the header is the
			// keyword, the block holds property statements, and both the replace and the insert paths
			// downstream only ever needed those two facts. LOCAL styles only, because the AST records
			// an imported one's location in a file this text is not -- see FDreamUIStyle::SourceName.
			// A caller whose subject genuinely IS the library opens that file and patches it with this
			// same call, which is why the patcher takes a text rather than a document.
			const FDreamUIStyle* Style = InAst.Styles.FindByPredicate(
				[&InEdit](const FDreamUIStyle& InStyle) { return InStyle.Name == InEdit.NodeId; });
			if (Style == nullptr)
			{
				RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
					FString::Printf(TEXT("this file declares no style named '%s' -- an imported one is written in the file that declares it"),
						*Ellipsize(InEdit.NodeId)));
				return false;
			}
			const int32 StyleOffset = OffsetOf(InText, Style->Location);
			if (!TextAtIs(InText, StyleOffset, TEXT("style")))
			{
				RefuseStale(OutDiagnostics, Style->Location,
					FString::Printf(TEXT("line %d does not begin with 'style'"), Style->Location.Line));
				return false;
			}
			OutTarget.OwnerOffset = StyleOffset;
			OutTarget.Properties = &Style->Properties;
			OutTarget.Location = Style->Location;
			for (const FDreamUIProperty& Property : Style->Properties)
			{
				OutTarget.Statements.Add(&Property);
			}
			return true;
		}

		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named '%s'"), *Ellipsize(InEdit.NodeId)));
			return false;
		}

		OutTarget.Location = Node->Location;
		OutTarget.Node = Node;

		const int32 NodeOffset = OffsetOf(InText, Node->Location);
		int32 TypeEnd = INDEX_NONE;
		if (!MatchNodeHeader(InText, *Node, TypeEnd))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("line %d does not begin with '%s'"), Node->Location.Line, *HeaderWordOf(*Node)));
			return false;
		}

		if (Node->Kind == EDreamUINodeKind::NamedSlot)
		{
			// A slot may carry a block now -- `slot Rows default : RowList { + VerticalBox { Spacing = 15 } }` -- and a
			// DECLARATION's block is an ordinary home for properties, written into like any node's. Two cases still
			// have none.
			//
			// A FILL (`slot Detail { Text Note { } }`, inside a component instance) is the host handing content to a
			// slot the component declares; its block holds that content and nothing else, and the slot widget its
			// properties would describe belongs to the component's own file.
			if (Node->bFillsSlot)
			{
				RefuseTarget(OutDiagnostics, Node->Location,
					FString::Printf(TEXT("'slot %s' here fills a slot of its component: its block holds the content, and the slot's own properties are written in the file that declares it"),
						*Node->Id));
				return false;
			}
			// A declaration written bare, `slot Footer`, is a hole and nothing else, and it stays one until its author
			// says otherwise: a designer drag growing a block on it would turn a line that reads "content goes here"
			// into a styled panel nobody wrote. The way to give it properties is to give it the block.
			int32 Open = INDEX_NONE;
			int32 Close = INDEX_NONE;
			if (!FindBlock(InText, NodeOffset, Open, Close))
			{
				RefuseTarget(OutDiagnostics, Node->Location,
					FString::Printf(TEXT("'slot %s' is written without a block, so there is nowhere in the file for a property of it; write 'slot %s { }' to give it one"),
						*Node->Id, *Node->Id));
				return false;
			}
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

		// A node that groups its slot lines in a `@slot { … }` block gets a new one there, bare: the block is the owner
		// of the insert, the `@slot` in front of the name is the block's to say, and the statements that count are the
		// ones inside it -- PlanInsert keeps to the owner's block, so naming the node's others costs nothing. A REPLACE
		// does not come through here at all; it edits the value where it stands, inside the block or on its own line.
		if (InEdit.Target == EDreamUIPatchTarget::Slot)
		{
			int32 Open = INDEX_NONE;
			int32 Close = INDEX_NONE;
			if (FindBlock(InText, NodeOffset, Open, Close))
			{
				const int32 SlotBlock = FindSlotBlock(InText, Open, Close);
				if (SlotBlock != INDEX_NONE)
				{
					OutTarget.OwnerOffset = SlotBlock;
					OutTarget.LinePrefix.Reset();
				}
			}
		}
		return true;
	}

	/** Where a property statement's text stops, so the insert after it clears its whole line. */
	int32 StatementSearchOffset(const FString& InText, const FDreamUIProperty& InProperty, int32 InNameOffset)
	{
		// A made-up property has no value text of its own to measure: its location is the line that produced it (an
		// `@fill`, an `if`), and the end of that line is all an insert needs from it.
		if (InProperty.IsBinding() || InProperty.bSynthesized)
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

	/**
	 * `#AABBCC` written back as `#ABC`, when the line already said `#ABC` and nothing is lost by it.
	 *
	 * The printer only ever emits six or eight digits -- there is no colour it could not spell that
	 * way, so it has no reason to know about the short forms. The cost was that the FIRST write-back
	 * to touch a line expanded the author's spelling: a file full of `#ABC` came back full of
	 * `#AABBCC`, as a diff hunk on a line whose meaning did not change.
	 *
	 * Reading the answer off the FILE is what makes this safe, and is why the objection in
	 * TryParseHexText's comment ("remembering per-value which spelling was on disk would put a second
	 * source of truth next to the value") does not apply: nothing is remembered. The text being
	 * patched is right here, it already says which spelling the author chose, and the short form is
	 * only used when it reads back as exactly the same colour AND has exactly the digit count the
	 * line already had -- so a 3-digit line never silently grows an alpha, and a colour that is not a
	 * doubled nibble is written long, as it must be.
	 */
	FString MatchHexSpelling(const FString& InExisting, const FString& InNewValue)
	{
		if (!InExisting.StartsWith(TEXT("#"), ESearchCase::CaseSensitive)
			|| !InNewValue.StartsWith(TEXT("#"), ESearchCase::CaseSensitive))
		{
			return InNewValue;
		}
		const int32 ExistingDigits = InExisting.Len() - 1;
		const int32 NewDigits = InNewValue.Len() - 1;
		if ((ExistingDigits != 3 && ExistingDigits != 4) || (NewDigits != 6 && NewDigits != 8))
		{
			return InNewValue;
		}
		FString Short = TEXT("#");
		for (int32 Index = 1; Index + 1 < InNewValue.Len(); Index += 2)
		{
			if (FChar::ToUpper(InNewValue[Index]) != FChar::ToUpper(InNewValue[Index + 1]))
			{
				return InNewValue;
			}
			Short.AppendChar(InNewValue[Index]);
		}
		return (Short.Len() - 1 == ExistingDigits) ? Short : InNewValue;
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

		// A slot property starts at its '@' when it has a line of its own, and at its name when it stands inside a
		// `@slot { … }` block -- the block's '@' speaks for every line in it. Either is the statement the tree located.
		const int32 NameOffset = OffsetOf(InText, InProperty.Location);
		// A `rows` cell has no name in the text -- its column is written once, in the table's header -- and its
		// location IS its value, which the slice check below holds to the tree as it does any value.
		const bool bNameAgrees = InProperty.bRowCell
			|| TextAtIs(InText, NameOffset, FirstSegmentOf(InProperty.Name))
			|| (bInSlotNotation && TextAtIs(InText, NameOffset, TEXT("@")));
		if (!bNameAgrees)
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
		case EDreamUIValueKind::ResourceRef:
			// The branch this switch did not have, and its absence was the one value kind that could
			// be spliced against a STALE tree: bSliceAgrees starts true, so `@Accent` skipped the
			// check entirely and an edit landed wherever the old location pointed. Same shape as the
			// colour case -- Raw is the name with the '@' stripped by the lexer, so the sigil is put
			// back to compare against the source.
			bSliceAgrees = Slice.Equals(FString(TEXT("@")) + InProperty.Value.Raw, ESearchCase::CaseSensitive);
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

		// The author's own hex spelling is kept where the new colour can wear it -- see MatchHexSpelling.
		const FString NewValue = MatchHexSpelling(Slice, InEdit.NewValueText.TrimStartAndEnd());
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
			//
			// Made-up properties do not count: the `Shown <- …` an `if` gives every node of its branches stands on the
			// `if`'s line, not in the node's block, so a blockless node inside a branch has one and is still blockless.
			const bool bTreeHasStatements = InTarget.Statements.ContainsByPredicate(
				[](const FDreamUIProperty* InProperty) { return !InProperty->bSynthesized; });
			if (bTreeHasStatements)
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
			// Another property for the same node in this batch: into the block the first one is writing. A block each was
			// `Text OkText { A = 1 } { B = 2 }`, which does not parse -- written to disk all the same.
			for (FSplice& Planned : OutSplices)
			{
				if (Planned.SynthesizedBlockOwner == InTarget.OwnerOffset)
				{
					const FString Closing = LineEnding + OwnerIndent + TEXT("}");
					if (!Planned.Text.EndsWith(Closing))
					{
						RefuseStale(OutDiagnostics, InTarget.Location, TEXT("the block planned for this node is not one another property could join"));
						return false;
					}
					Planned.Text = Planned.Text.LeftChop(Closing.Len()) + LineEnding + Indent + NewLine + Closing;
					return true;
				}
			}
			// The block opens on the header's line and closes on a line of its own: whatever else stands on the header's line
			// would end up inside it. A ';' alone is no statement -- `Text Label;` gets `Text Label {;`, and a node body skips
			// separators -- but `Text A; Text B` has B after it.
			if (!RestOfLineIsBlank(InText, End, /*bInAllowTerminators*/ true))
			{
				RefuseStale(OutDiagnostics, InTarget.Location,
					TEXT("another statement shares this node's line, and a block written here would take it in; give the node a line and a block of its own"));
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
			Body.SynthesizedBlockOwner = InTarget.OwnerOffset;
			return true;
		}

		// The anchor is the last property statement already in this block. Properties go before the
		// subtree and before the '+' blocks on purpose: a file where the properties of a node are
		// interleaved with its children is one nobody can read the shape of at a glance, and since
		// the AST hands over each statement's location, "last property" is a question with an answer
		// rather than a guess.
		//
		// Statements of THIS block only, at its own level. The lines of a `@slot { … }` block arrive as the node's slot
		// properties and the `Shown` an `if` gives its branches as theirs, so the last statement the tree names can sit
		// inside a block of its own -- and a bare property written after it would become a slot property, or land in a
		// branch.
		const FDreamUIProperty* Anchor = nullptr;
		int32 AnchorOffset = INDEX_NONE;
		for (const FDreamUIProperty* Property : InTarget.Statements)
		{
			const int32 Offset = OffsetOf(InText, Property->Location);
			if (Offset > AnchorOffset && Offset > Open && Offset < Close && DepthWithin(InText, Open, Offset) == 0)
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
		//
		// Except the one count that keeps the line in its block: a statement whose line goes on to open a block of
		// another kind (`Spacing = 4  @slot {`) ends INSIDE that block, and the line is carried out past its close.
		const int32 AtBlockLevel = LeaveNestedConstructs(InText, Open, Close, InsertAt);
		if (AtBlockLevel == INDEX_NONE)
		{
			RefuseStale(OutDiagnostics, InTarget.Location,
				TEXT("the last line of this block opens another block that does not end on a line of its own, so there is no line to write after"));
			return false;
		}
		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = AtBlockLevel;
		Splice.Length = 0;
		Splice.Text = LineEnding + Indent + NewLine;
		Splice.Order = InOutOrder++;
		return true;
	}

	// --------------------------------------------------------------------------------------------
	// Properties the front end made
	//
	// Two constructs put properties into the tree that no line spells: an `if` gives every node of its branches a
	// `Shown <- Cond` standing on the `if`'s line, and `@fill` / `@fill 2` stand for `SizeRule = Fill` and
	// `FillWeight = 2`. The tree marks them (FDreamUIProperty::bSynthesized) and points them at the line that made
	// them, which is right for a diagnostic and wrong for a splice: there is no `Shown` on an `if` line to replace,
	// and treating it as one either refused as a stale tree -- the wrong reason -- or, worse, matched something.
	//
	// So each is decided on its own terms. The shorthand is rewritten when the result plainly means the same thing
	// with the one value changed, and refused otherwise; everything else is refused with DUI7004, which says the line
	// to go to.
	// --------------------------------------------------------------------------------------------

	/** Where an `@fill` statement stands in the text: its '@', its weight when it has one, and its end. */
	struct FFillShorthand
	{
		int32 Start = INDEX_NONE;
		int32 WeightStart = INDEX_NONE;
		int32 WeightEnd = INDEX_NONE;
		int32 End = INDEX_NONE;
	};

	/**
	 * The `@fill` a made-up slot property came from, measured in the text, or false when its location is not one.
	 *
	 * The location may be the '@' or the word after it -- the statement starts at the '@' either way, the way a
	 * `@slot` line's does -- and anything else there means the property came from a shorthand this scan does not
	 * know, which is a refusal, never a guess.
	 */
	bool MeasureFillShorthand(const FString& InText, const FDreamUIProperty& InProperty, FFillShorthand& OutFill)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		int32 Start = OffsetOf(InText, InProperty.Location);
		if (Start == INDEX_NONE || Start >= Length)
		{
			return false;
		}
		if (Chars[Start] != TEXT('@'))
		{
			int32 Back = Start;
			while (Back > 0 && IsInlineWhitespace(Chars[Back - 1]))
			{
				--Back;
			}
			if (Back == 0 || Chars[Back - 1] != TEXT('@'))
			{
				return false;
			}
			Start = Back - 1;
		}

		int32 Cursor = Start + 1;
		while (Cursor < Length && IsInlineWhitespace(Chars[Cursor]))
		{
			++Cursor;
		}
		if (!KeywordAt(InText, Cursor, TEXT("fill")))
		{
			return false;
		}
		Cursor += 4;
		OutFill.Start = Start;
		OutFill.End = Cursor;

		int32 Weight = Cursor;
		while (Weight < Length && IsInlineWhitespace(Chars[Weight]))
		{
			++Weight;
		}
		if (Weight < Length && ((Chars[Weight] >= TEXT('0') && Chars[Weight] <= TEXT('9')) || Chars[Weight] == TEXT('.')))
		{
			const int32 WeightEnd = MeasureValue(InText, Weight);
			if (WeightEnd == INDEX_NONE)
			{
				return false;
			}
			OutFill.WeightStart = Weight;
			OutFill.WeightEnd = WeightEnd;
			OutFill.End = WeightEnd;
		}
		return true;
	}

	/**
	 * Whether `@fill <weight>` reads back with exactly this weight -- asked of the parser, as ValidateValueText asks it
	 * about values, because the judge of what the shorthand takes has to be the thing that reads it.
	 */
	bool FillShorthandTakesWeight(const FString& InWeight)
	{
		const FString Probe = FString::Printf(
			TEXT("Widget DreamUIPatchProbe {\n\tWidget DreamUIPatchProbeChild {\n\t\t@fill %s\n\t}\n}"), *InWeight);
		FDreamUIAst ProbeAst;
		FDreamUIDiagnosticBag ProbeDiagnostics;
		if (!FDreamUISourceFile::Parse(Probe, FString(), ProbeAst, ProbeDiagnostics) || ProbeAst.Root.Children.Num() != 1)
		{
			return false;
		}
		const FDreamUIProperty* Weight = FindProperty(ProbeAst.Root.Children[0].SlotProperties, TEXT("FillWeight"));
		return Weight != nullptr && Weight->Value.Raw.Equals(InWeight, ESearchCase::CaseSensitive);
	}

	void RefuseNotWritable(FDreamUIDiagnosticBag& OutDiagnostics, const FDreamUISourceLocation& InLocation, FString InMessage)
	{
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::PatchSyntaxNotWritable, InLocation, MoveTemp(InMessage));
	}

	/**
	 * The refusal for a property an `if` decides: the same words whichever of its two faces was edited. The location is
	 * the `if` (or `else`) that made the Shown -- or, for a node that wrote a Shown of its own, that line, which the
	 * front end has joined to the branch's condition.
	 */
	void RefuseConditionalVisibility(FDreamUIDiagnosticBag& OutDiagnostics, const FDreamUIProperty& InShown,
		const FString& InNodeId, const FString& InEditedName)
	{
		RefuseNotWritable(OutDiagnostics, InShown.Location,
			FString::Printf(TEXT("'%s' on '%s' is decided by the 'if' block it stands in (see line %d): the branch shows or hides it, and no line of its own says so -- change the condition, or move the node out of the branch"),
				*InEditedName, *InNodeId, InShown.Location.Line));
	}

	bool PlanSynthesizedEdit(const FString& InText, const FDreamUIPropertyEdit& InEdit, const FResolvedTarget& InTarget,
		const FDreamUIProperty& InProperty, TArray<FSplice>& OutSplices, int32& InOutOrder,
		FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FString NewValue = InEdit.NewValueText.TrimStartAndEnd();
		const FString NodeId = InTarget.Node != nullptr ? InTarget.Node->Id : InEdit.NodeId;

		FFillShorthand Fill;
		if (InEdit.Target == EDreamUIPatchTarget::Slot && MeasureFillShorthand(InText, InProperty, Fill))
		{
			if (InProperty.Name == TEXT("FillWeight"))
			{
				// The number after `fill`, and nothing else on the line: the shorthand still means Fill with a weight,
				// which is what the author wrote, with the weight the designer chose.
				if (Fill.WeightStart == INDEX_NONE)
				{
					// A weight the front end gave a bare `@fill` has no text to change, and growing the shorthand into
					// `@fill 3` would collide with a rewrite of the same word in the same batch. A line of its own after
					// the others is the plain override: the later line wins, as two lines naming one property always do.
					return PlanInsert(InText, InEdit, InTarget, OutSplices, InOutOrder, OutDiagnostics);
				}
				const FString Slice = InText.Mid(Fill.WeightStart, Fill.WeightEnd - Fill.WeightStart);
				if (!Slice.Equals(InProperty.Value.Raw, ESearchCase::CaseSensitive))
				{
					RefuseStale(OutDiagnostics, InProperty.Location,
						FString::Printf(TEXT("the '@fill' on line %d reads '%s' where the tree says '%s'"),
							InProperty.Location.Line, *Ellipsize(Slice), *Ellipsize(InProperty.Value.Raw)));
					return false;
				}
				if (Slice.Equals(NewValue, ESearchCase::CaseSensitive))
				{
					return true;
				}
				if (!FillShorthandTakesWeight(NewValue))
				{
					RefuseNotWritable(OutDiagnostics, InProperty.Location,
						FString::Printf(TEXT("'%s' is not a weight '@fill' can be written with; write '@slot FillWeight = %s' in place of the shorthand"),
							*Ellipsize(NewValue), *Ellipsize(NewValue)));
					return false;
				}
				FSplice& Splice = OutSplices.AddDefaulted_GetRef();
				Splice.Offset = Fill.WeightStart;
				Splice.Length = Fill.WeightEnd - Fill.WeightStart;
				Splice.Text = NewValue;
				Splice.Order = InOutOrder++;
				return true;
			}
			if (InProperty.Name == TEXT("SizeRule"))
			{
				if (NewValue.Equals(InProperty.Value.Raw, ESearchCase::CaseSensitive))
				{
					return true;
				}
				if (Fill.WeightStart != INDEX_NONE)
				{
					// `@fill 2` is two properties in one word, and taking the Fill out of it leaves a weight with nothing
					// to say it -- the rewrite would be two lines where the author wrote one, which is theirs to choose.
					RefuseNotWritable(OutDiagnostics, InProperty.Location,
						FString::Printf(TEXT("the '@fill' on line %d also sets the weight, so it cannot simply stop meaning Fill; write '@slot SizeRule = %s' and '@slot FillWeight = %s' in its place"),
							InProperty.Location.Line, *Ellipsize(NewValue),
							*Ellipsize(InText.Mid(Fill.WeightStart, Fill.WeightEnd - Fill.WeightStart))));
					return false;
				}
				// A bare `@fill` is exactly `@slot SizeRule = Fill`, so it is replaced by the long spelling with the new value
				// -- the one rewrite of a shorthand that leaves nothing else it meant behind.
				FSplice& Splice = OutSplices.AddDefaulted_GetRef();
				Splice.Offset = Fill.Start;
				Splice.Length = Fill.End - Fill.Start;
				Splice.Text = FString(TEXT("@slot SizeRule = ")) + NewValue;
				Splice.Order = InOutOrder++;
				return true;
			}
		}

		if (InProperty.Name == TEXT("Shown") && InProperty.IsBinding())
		{
			RefuseConditionalVisibility(OutDiagnostics, InProperty, NodeId, InEdit.PropertyName);
			return false;
		}
		RefuseNotWritable(OutDiagnostics, InProperty.Location,
			FString::Printf(TEXT("'%s' on '%s' was made from line %d rather than written on a line of its own, so there is no text to edit in place"),
				*InProperty.Name, *NodeId, InProperty.Location.Line));
		return false;
	}

	bool PlanEdit(const FString& InText, const FDreamUIAst& InAst, const FDreamUIPropertyEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InEdit.PropertyName.TrimStartAndEnd().IsEmpty())
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(), TEXT("a property edit with no property name"));
			return false;
		}

		if (InEdit.Target == EDreamUIPatchTarget::Resource)
		{
			const FDreamUIResource* Entry = InAst.FindResource(InEdit.PropertyName);
			if (Entry == nullptr)
			{
				RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
					FString::Printf(TEXT("no resources entry is named '%s'"), *InEdit.PropertyName));
				return false;
			}
			if (!ValidateValueText(InEdit.NewValueText, Entry->Value.Location, OutDiagnostics))
			{
				return false;
			}
			// PlanReplace wants an FDreamUIProperty, and an entry is close enough to wear one: the
			// value and its location are real, and Name carries the TYPE keyword because that is what
			// the entry's line starts with -- the stale-check reads the text at Location and expects
			// the first word of Name there.
			FDreamUIProperty StandIn;
			StandIn.Name = Entry->TypeName;
			StandIn.Value = Entry->Value;
			StandIn.Location = Entry->Location;
			return PlanReplace(InText, InEdit, StandIn, /*bInSlotNotation*/false,
				OutSplices, InOutOrder, OutDiagnostics);
		}

		if (InEdit.Target != EDreamUIPatchTarget::Style)
		{
			// A `rows` line: what stands where a header would is a run of values, so the header resolution below has
			// nothing to read. A column is replaced in its cell, and a property the line's own block spells where it
			// stands; anything else has no line in this file to land on.
			const FDreamUINode* Row = FindNodeById(InAst, InEdit.NodeId);
			if (Row != nullptr && !Row->RowKey.IsEmpty())
			{
				const FDreamUIProperty* Spelled = InEdit.Target == EDreamUIPatchTarget::Node
					? FindProperty(Row->Properties, InEdit.PropertyName) : nullptr;
				if (Spelled == nullptr || Spelled->bSynthesized || Spelled->IsBinding())
				{
					RefuseRowLine(OutDiagnostics, *Row, FString::Printf(TEXT("'%s' cannot be written back"), *InEdit.PropertyName));
					return false;
				}
				if (!ValidateValueText(InEdit.NewValueText, Spelled->Location, OutDiagnostics))
				{
					return false;
				}
				return PlanReplace(InText, InEdit, *Spelled, /*bInSlotNotation*/false, OutSplices, InOutOrder, OutDiagnostics);
			}
		}

		FResolvedTarget Target;
		if (!ResolveTarget(InText, InAst, InEdit, Target, OutDiagnostics))
		{
			return false;
		}

		const FDreamUIProperty* Existing = FindProperty(*Target.Properties, InEdit.PropertyName);

		// `Shown` is a face over Visibility -- reading one reads the other -- so a node whose Shown is BOUND has its
		// visibility decided by that binding, whichever of the two names the edit arrives under. A Visibility line
		// written next to `Shown <- HasSave()` would be undone by the binding on the next tick; one written into a
		// branch of an `if` would quietly fight the condition the file states two lines up.
		if (InEdit.Target == EDreamUIPatchTarget::Node && Target.Node != nullptr
			&& (InEdit.PropertyName == TEXT("Visibility") || InEdit.PropertyName == TEXT("Shown")))
		{
			const FDreamUIProperty* Shown = FindProperty(Target.Node->Properties, TEXT("Shown"));
			if (Shown != nullptr && Shown->IsBinding())
			{
				if (Shown->bSynthesized)
				{
					RefuseConditionalVisibility(OutDiagnostics, *Shown, Target.Node->Id, InEdit.PropertyName);
				}
				else
				{
					RefuseTarget(OutDiagnostics, Shown->Location,
						FString::Printf(TEXT("'%s' on '%s' is decided by the 'Shown <-' binding on line %d: a value written back would be overwritten by it"),
							*InEdit.PropertyName, *Target.Node->Id, Shown->Location.Line));
				}
				return false;
			}
		}

		// Validated before anything is planned, and against the line it would land on, so the
		// diagnostic points at the file the author is looking at rather than at the value in the
		// abstract.
		const FDreamUISourceLocation ValueLocation = Existing != nullptr ? Existing->Location : Target.Location;
		if (!ValidateValueText(InEdit.NewValueText, ValueLocation, OutDiagnostics))
		{
			return false;
		}

		if (Existing != nullptr && Existing->bSynthesized)
		{
			return PlanSynthesizedEdit(InText, InEdit, Target, *Existing, OutSplices, InOutOrder, OutDiagnostics);
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

// -------------------------------------------------------------------------------------------------
// Structural edits
//
// The half this component spent its first year disclaiming. See ApplyStructuralEdits in the header
// for why the disclaimer expired; what follows is shaped entirely by the one rule that did not --
// the file this produces has to parse.
//
// Everything here works in WHOLE STATEMENTS. A node's text runs from the start of its header line to
// the end of the line its closing brace is on; a `+` block's likewise. That is what makes an insert
// a splice of complete lines, a remove a deletion of complete lines, and a move a lift-and-drop of a
// byte-identical run -- none of which can leave a half-statement behind. Every anchor is confirmed
// against the text before it is used (the same TextAtIs check the value path makes), so an AST that
// no longer describes this file is a refusal rather than a cut in the wrong place.
// -------------------------------------------------------------------------------------------------

namespace DreamUIPatchLocal
{
	/**
	 * A node's whole text: [line start of its header, end of the line its last character is on).
	 *
	 * The trailing line ending is NOT included, so a remove takes the line's characters and the
	 * caller deletes the break separately -- which is what lets the same extent serve a move, where
	 * the text is put back with whatever break the destination needs.
	 */
	bool MeasureNodeExtent(const FString& InText, const FDreamUINode& InNode, int32& OutStart, int32& OutEnd)
	{
		const int32 HeaderOffset = OffsetOf(InText, InNode.Location);
		if (HeaderOffset == INDEX_NONE)
		{
			return false;
		}
		OutStart = FindLineStart(InText, HeaderOffset);

		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		const bool bHasBlock = FindBlock(InText, HeaderOffset, Open, Close);
		// Whole lines, so only lines that are the node's own: anything before its header or after its end on those lines is
		// another statement -- its parent's header, a sibling -- which the cut would take with it.
		if (!LineBeforeIsBlank(InText, HeaderOffset)
			|| !RestOfLineIsBlank(InText, bHasBlock ? Close + 1 : HeaderEnd(InText, HeaderOffset), /*bInAllowTerminators*/ true))
		{
			return false;
		}
		OutEnd = bHasBlock
			? FindLineEnd(InText, Close)
			: FindLineEnd(InText, HeaderOffset);
		return true;
	}

	/** The same, for one `+ Class { }` block. */
	bool MeasureComponentExtent(const FString& InText, const FDreamUIComponent& InComponent, int32& OutStart, int32& OutEnd)
	{
		const int32 HeaderOffset = OffsetOf(InText, InComponent.Location);
		if (HeaderOffset == INDEX_NONE || !TextAtIs(InText, HeaderOffset, TEXT("+")))
		{
			return false;
		}
		OutStart = FindLineStart(InText, HeaderOffset);

		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		const bool bHasBlock = FindBlock(InText, HeaderOffset, Open, Close);
		// See MeasureNodeExtent: only lines that are the block's own.
		if (!LineBeforeIsBlank(InText, HeaderOffset)
			|| !RestOfLineIsBlank(InText, bHasBlock ? Close + 1 : HeaderEnd(InText, HeaderOffset), /*bInAllowTerminators*/ true))
		{
			return false;
		}
		OutEnd = bHasBlock
			? FindLineEnd(InText, Close)
			: FindLineEnd(InText, HeaderOffset);
		return true;
	}

	/** True when the text at this node's location still begins with what the tree says is there. */
	bool ConfirmNodeAnchor(const FString& InText, const FDreamUINode& InNode)
	{
		// A named slot's header starts with the keyword, not a type -- TypeName is empty on one -- and a type may be
		// more than one token (`nier.Row`, `@Row`). MatchNodeHeader knows both.
		int32 TypeEnd = INDEX_NONE;
		return MatchNodeHeader(InText, InNode, TypeEnd);
	}

	/**
	 * A node that is the one template of a `for` or an `each`: the widget written once and made per item. Taking it out,
	 * or moving it away, leaves a loop with nothing to repeat (DUI5012, DUI5021) -- a file that parses and does not
	 * build, which the write-back would then refuse wholesale. Said here instead, about the node, with what to do.
	 */
	const FDreamUINode* FindEnclosingLoop(const FDreamUIAst& InAst, const FDreamUINode& InNode)
	{
		const FDreamUINode* Parent = FindParentOf(InAst, &InNode);
		return Parent != nullptr && (Parent->Kind == EDreamUINodeKind::ForLoop || Parent->Kind == EDreamUINodeKind::EachLoop)
			? Parent : nullptr;
	}

	/** The children of InParent a caller may address by index: the authored ones, in file order. */
	void CollectAddressableChildren(const FDreamUINode& InParent, TArray<const FDreamUINode*>& OutChildren)
	{
		for (const FDreamUINode& Child : InParent.Children)
		{
			// A loop is not a position: its body is one template written once and expanded N times,
			// so "index 2 of the parent" has no meaning across it. Counting it as a child would also
			// let a caller drop a node INTO a loop by accident, which the builder would then refuse
			// with EachMisplaced on a line the caller never wrote.
			if (Child.Kind == EDreamUINodeKind::Widget || Child.Kind == EDreamUINodeKind::NamedSlot)
			{
				OutChildren.Add(&Child);
			}
		}
	}

	/**
	 * Where a new child's text goes inside InParent's block, and what indentation it wears.
	 *
	 * Returns false when there is no place: bOutParentHasBlock then says which kind of false it is. A parent with no
	 * block gets one from the caller, exactly as PlanInsert does for a property on a blockless node; a parent that HAS
	 * one and still yields no place is a text that shares its lines in a way a whole-line edit cannot work in, and the
	 * caller refuses. The two used to be one false, and a parent with a block was then given a second one.
	 *
	 * Every place found is at the level of the parent's own block. A child of an `if` branch is a child of the parent
	 * in the tree (the branch is lowered away) and inside the `if` in the text, so "after that child" is carried out
	 * past the whole `if … else …` -- a node written into the branch would quietly come and go with its condition.
	 */
	bool FindChildInsertPoint(const FString& InText, const FDreamUINode& InParent, int32 InChildIndex,
		int32& OutOffset, FString& OutIndent, bool& bOutParentHasBlock)
	{
		bOutParentHasBlock = false;
		const int32 HeaderOffset = OffsetOf(InText, InParent.Location);
		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		if (HeaderOffset == INDEX_NONE || !FindBlock(InText, HeaderOffset, Open, Close))
		{
			return false;
		}
		bOutParentHasBlock = true;

		TArray<const FDreamUINode*> Children;
		CollectAddressableChildren(InParent, Children);

		const FString ParentIndent = IndentAt(InText, HeaderOffset);
		OutIndent = ParentIndent + DetectIndentUnit(InText);

		// One level in from the header is only the FALLBACK: a block that already holds children
		// says how far in this file puts them, and copying that is what keeps a two-space file
		// two-space and a tab file tabs. The first child written at the block's own level -- one
		// inside an `if` branch is indented for the branch, a level deeper than the place found.
		for (const FDreamUINode* Child : Children)
		{
			const int32 ChildOffset = OffsetOf(InText, Child->Location);
			if (ChildOffset == INDEX_NONE || ChildOffset <= Open || ChildOffset >= Close
				|| DepthWithin(InText, Open, ChildOffset) != 0)
			{
				continue;
			}
			if (FindLineStart(InText, ChildOffset) != FindLineStart(InText, HeaderOffset))
			{
				OutIndent = IndentAt(InText, ChildOffset);
			}
			break;
		}

		const int32 Index = InChildIndex == INDEX_NONE ? Children.Num() : FMath::Clamp(InChildIndex, 0, Children.Num());
		if (Children.Num() == 0 || Index == 0)
		{
			// Before the first child, which for an empty block means after everything else in it:
			// properties and `+` blocks come first by the convention PlanInsert already keeps, so
			// the anchor is the last statement of any kind, or the brace itself -- of any kind at the
			// block's own level, that is: a slot property inside `@slot { … }` is the block's last line,
			// not the parent's.
			int32 Anchor = Open;
			auto ConsiderProperty = [&InText, &Anchor, Open, Close](const FDreamUIProperty& InProperty)
			{
				const int32 Offset = OffsetOf(InText, InProperty.Location);
				if (Offset > Anchor && Offset < Close && DepthWithin(InText, Open, Offset) == 0)
				{
					Anchor = StatementSearchOffset(InText, InProperty, Offset);
				}
			};
			for (const FDreamUIProperty& Property : InParent.Properties)
			{
				ConsiderProperty(Property);
			}
			for (const FDreamUIProperty& Property : InParent.SlotProperties)
			{
				ConsiderProperty(Property);
			}
			for (const FDreamUIComponent& Component : InParent.Components)
			{
				int32 Start = INDEX_NONE;
				int32 End = INDEX_NONE;
				if (MeasureComponentExtent(InText, Component, Start, End) && End > Anchor && End < Close)
				{
					Anchor = End;
				}
			}
			OutOffset = LeaveNestedConstructs(InText, Open, Close, FindLineEnd(InText, Anchor));
			return OutOffset != INDEX_NONE;
		}

		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		if (!MeasureNodeExtent(InText, *Children[Index - 1], Start, End))
		{
			return false;
		}
		OutOffset = LeaveNestedConstructs(InText, Open, Close, End);
		return OutOffset != INDEX_NONE;
	}

	/** Every line of InBlock re-indented from InFromIndent to InToIndent, first line included. */
	FString ReindentBlock(const FString& InBlock, const FString& InFromIndent, const FString& InToIndent,
		const FString& InLineEnding)
	{
		if (InFromIndent == InToIndent)
		{
			return InBlock;
		}
		TArray<FString> Lines;
		InBlock.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/false);
		for (FString& Line : Lines)
		{
			Line.RemoveFromEnd(TEXT("\r"));
			// Only a line that actually starts with the old indentation is shifted. A line indented
			// some other way was indented that way on purpose -- a continuation, a comment lined up
			// with something -- and re-flowing it would be this file reformatting somebody's work.
			if (InFromIndent.IsEmpty())
			{
				Line = InToIndent + Line;
			}
			else if (Line.StartsWith(InFromIndent, ESearchCase::CaseSensitive))
			{
				Line = InToIndent + Line.RightChop(InFromIndent.Len());
			}
		}
		return FString::Join(Lines, *InLineEnding);
	}

	bool PlanInsertNode(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InEdit.NewId.IsEmpty() || InEdit.TypeName.IsEmpty())
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				TEXT("a node cannot be inserted without both a type and an id"));
			return false;
		}
		if (FindNodeById(InAst, InEdit.NewId) != nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("this file already declares a node called %s; two would be DUI3001"), *InEdit.NewId));
			return false;
		}

		const FDreamUINode* Parent = InEdit.ParentId.IsEmpty()
			? (InAst.bHasRoot ? &InAst.Root : nullptr)
			: FindNodeById(InAst, InEdit.ParentId);
		if (Parent == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s to insert into"), *Ellipsize(InEdit.ParentId)));
			return false;
		}
		if (!Parent->RowKey.IsEmpty())
		{
			RefuseRowLine(OutDiagnostics, *Parent, TEXT("a node cannot be inserted into it"));
			return false;
		}
		if (HoldsRows(*Parent))
		{
			// The insert point is found from the children's places, and a row's place is inside its table: a node
			// "after the last child" would be written in among the rows.
			RefuseTarget(OutDiagnostics, Parent->Location,
				FString::Printf(TEXT("%s holds a 'rows' table, and the designer cannot place a node beside its lines -- write it in the text"),
					*Parent->Id));
			return false;
		}
		if (Parent->Kind == EDreamUINodeKind::NamedSlot && !Parent->bFillsSlot)
		{
			// A DECLARATION is a hole: its block may style it, never fill it -- the content comes from the host, and a
			// block holding both is DUI2019. A FILL (`slot Detail { … }` inside a component instance) is the opposite:
			// its block is nothing but content, and a node goes in like into any other block.
			RefuseTarget(OutDiagnostics, Parent->Location,
				FString::Printf(TEXT("%s declares a slot, which a host fills: nothing can be written inside it"),
					*Parent->Id));
			return false;
		}
		if (!ConfirmNodeAnchor(InText, *Parent))
		{
			RefuseStale(OutDiagnostics, Parent->Location,
				FString::Printf(TEXT("line %d does not begin with %s"), Parent->Location.Line, *HeaderWordOf(*Parent)));
			return false;
		}

		// Content for a NAMED slot of a component instance goes into the instance's fill of that slot -- the one already
		// written, or a new one around the node. Nested in the instance's own block instead, it would be the instance's
		// default-slot content on the next compile, which is not where it was dropped.
		const FDreamUINode* Destination = Parent;
		bool bWriteFill = false;
		if (!InEdit.FillSlotName.IsEmpty())
		{
			if (Parent->Kind != EDreamUINodeKind::Widget)
			{
				RefuseTarget(OutDiagnostics, Parent->Location,
					FString::Printf(TEXT("%s is not a component instance, so it has no slot %s to fill"), *Parent->Id, *InEdit.FillSlotName));
				return false;
			}
			const FDreamUINode* Fill = Parent->Children.FindByPredicate([&InEdit](const FDreamUINode& InChild)
			{
				return InChild.Kind == EDreamUINodeKind::NamedSlot && InChild.bFillsSlot && InChild.Id == InEdit.FillSlotName;
			});
			if (Fill != nullptr)
			{
				if (!ConfirmNodeAnchor(InText, *Fill))
				{
					RefuseStale(OutDiagnostics, Fill->Location,
						FString::Printf(TEXT("line %d does not begin with 'slot'"), Fill->Location.Line));
					return false;
				}
				Destination = Fill;
			}
			else
			{
				bWriteFill = true;
			}
		}

		const FString LineEnding = DetectLineEnding(InText);
		const FString IndentUnit = DetectIndentUnit(InText);
		// The new statement at a given indentation, its line ending in front: the node, or the fill holding it.
		auto StatementText = [&InEdit, &LineEnding, &IndentUnit, bWriteFill](const FString& InIndent)
		{
			const FString NodeIndent = bWriteFill ? InIndent + IndentUnit : InIndent;
			const FString Node = LineEnding + NodeIndent + InEdit.TypeName + TEXT(" ") + InEdit.NewId + TEXT(" {")
				+ LineEnding + NodeIndent + TEXT("}");
			return bWriteFill
				? LineEnding + InIndent + TEXT("slot ") + InEdit.FillSlotName + TEXT(" {") + Node + LineEnding + InIndent + TEXT("}")
				: Node;
		};

		int32 InsertAt = INDEX_NONE;
		FString Indent;
		bool bParentHasBlock = false;
		// A position among the instance's children means nothing inside a fill, and a new fill goes after them all.
		const int32 ChildIndex = (Destination != Parent || bWriteFill) ? INDEX_NONE : InEdit.ChildIndex;
		if (FindChildInsertPoint(InText, *Destination, ChildIndex, InsertAt, Indent, bParentHasBlock))
		{
			FSplice& Splice = OutSplices.AddDefaulted_GetRef();
			Splice.Offset = InsertAt;
			Splice.Length = 0;
			Splice.Text = StatementText(Indent);
			Splice.Order = InOutOrder++;
			return true;
		}
		if (bParentHasBlock)
		{
			RefuseStale(OutDiagnostics, Destination->Location,
				FString::Printf(TEXT("there is no line in %s's block to write a node after: the child before the place shares its lines with another statement"),
					*Destination->Id));
			return false;
		}

		// No block on the parent -- `Widget Root` alone on a line. The block is written with the
		// child in it, opening brace on the header line where the grammar requires it. Same two
		// splices and the same planning order as PlanInsert's blockless path.
		const int32 HeaderOffset = OffsetOf(InText, Destination->Location);
		const int32 End = HeaderEnd(InText, HeaderOffset);
		if (End <= HeaderOffset)
		{
			RefuseStale(OutDiagnostics, Destination->Location, TEXT("the header this node would go inside is not there"));
			return false;
		}
		const FString ParentIndent = IndentAt(InText, HeaderOffset);
		const FString ChildIndent = ParentIndent + IndentUnit;

		FSplice& Brace = OutSplices.AddDefaulted_GetRef();
		Brace.Offset = End;
		Brace.Length = 0;
		Brace.Text = TEXT(" {");
		Brace.Order = InOutOrder++;

		FSplice& Body = OutSplices.AddDefaulted_GetRef();
		Body.Offset = FindLineEnd(InText, End);
		Body.Length = 0;
		Body.Text = StatementText(ChildIndent) + LineEnding + ParentIndent + TEXT("}");
		Body.Order = InOutOrder++;
		return true;
	}

	/** The line break BEFORE an extent, so removing a statement leaves no blank line where it stood. */
	int32 ExtendBackOverLineBreak(const FString& InText, int32 InStart)
	{
		int32 Start = InStart;
		if (Start > 0 && InText[Start - 1] == TEXT('\n'))
		{
			--Start;
			if (Start > 0 && InText[Start - 1] == TEXT('\r'))
			{
				--Start;
			}
		}
		return Start;
	}

	bool PlanRemoveNode(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s"), *Ellipsize(InEdit.NodeId)));
			return false;
		}
		if (!Node->RowKey.IsEmpty())
		{
			RefuseRowLine(OutDiagnostics, *Node, TEXT("a row is removed by deleting its line"));
			return false;
		}
		if (InAst.bHasRoot && Node == &InAst.Root)
		{
			// A .dui holds exactly one root (DUI2006), so removing it produces a file that does not
			// parse -- the one outcome this component may not have.
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("%s is the root: a .dui holds exactly one, so it cannot be removed"), *Node->Id));
			return false;
		}
		if (const FDreamUINode* Loop = FindEnclosingLoop(InAst, *Node))
		{
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("%s is what the '%s' on line %d repeats, and a loop with nothing to repeat does not build; remove the loop instead"),
					*Node->Id, Loop->Kind == EDreamUINodeKind::ForLoop ? TEXT("for") : TEXT("each"), Loop->Location.Line));
			return false;
		}
		if (!ConfirmNodeAnchor(InText, *Node))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("line %d does not begin with %s"), Node->Location.Line, *HeaderWordOf(*Node)));
			return false;
		}

		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		if (!MeasureNodeExtent(InText, *Node, Start, End))
		{
			RefuseStale(OutDiagnostics, Node->Location, TEXT("this node's text is not where the tree says it is, or shares its lines with another statement"));
			return false;
		}

		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = ExtendBackOverLineBreak(InText, Start);
		Splice.Length = End - Splice.Offset;
		Splice.Order = InOutOrder++;
		return true;
	}

	bool PlanMoveNode(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s"), *Ellipsize(InEdit.NodeId)));
			return false;
		}
		if (!Node->RowKey.IsEmpty())
		{
			RefuseRowLine(OutDiagnostics, *Node, TEXT("a row is moved by moving its line"));
			return false;
		}
		if (InAst.bHasRoot && Node == &InAst.Root)
		{
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("%s is the root and has nowhere to move to"), *Node->Id));
			return false;
		}
		const FDreamUINode* Parent = InEdit.ParentId.IsEmpty()
			? (InAst.bHasRoot ? &InAst.Root : nullptr)
			: FindNodeById(InAst, InEdit.ParentId);
		if (Parent == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s to move into"), *Ellipsize(InEdit.ParentId)));
			return false;
		}
		if (const FDreamUINode* Loop = FindEnclosingLoop(InAst, *Node))
		{
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("%s is what the '%s' on line %d repeats; moving it out would leave the loop nothing to repeat -- move the loop"),
					*Node->Id, Loop->Kind == EDreamUINodeKind::ForLoop ? TEXT("for") : TEXT("each"), Loop->Location.Line));
			return false;
		}
		if (Parent->Kind == EDreamUINodeKind::NamedSlot && !Parent->bFillsSlot)
		{
			RefuseTarget(OutDiagnostics, Parent->Location,
				FString::Printf(TEXT("%s declares a slot, which a host fills: nothing can be moved inside it"), *Parent->Id));
			return false;
		}
		if (!ConfirmNodeAnchor(InText, *Node) || !ConfirmNodeAnchor(InText, *Parent))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				TEXT("a line this move depends on does not say what the tree says"));
			return false;
		}

		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		if (!MeasureNodeExtent(InText, *Node, Start, End))
		{
			RefuseStale(OutDiagnostics, Node->Location, TEXT("this node's text is not where the tree says it is, or shares its lines with another statement"));
			return false;
		}

		const FString LineEnding = DetectLineEnding(InText);
		int32 InsertAt = INDEX_NONE;
		FString Indent;
		bool bParentHasBlock = false;
		if (!FindChildInsertPoint(InText, *Parent, InEdit.ChildIndex, InsertAt, Indent, bParentHasBlock))
		{
			// A blockless destination would mean writing the brace AND lifting the subtree into it in
			// one planning pass, with the lifted text overlapping its own insertion point when the
			// two nodes are adjacent. Refused with the way out, which is one insert then one move.
			RefuseTarget(OutDiagnostics, Parent->Location,
				bParentHasBlock
					? FString::Printf(TEXT("there is no line in %s's block to move %s after: the child before the place shares its lines with another statement"),
						*Parent->Id, *Node->Id)
					: FString::Printf(TEXT("%s has no block to move %s into; give it one first"), *Parent->Id, *Node->Id));
			return false;
		}
		if (InsertAt > Start && InsertAt < End)
		{
			// Into itself. The splices would overlap and the file would lose the subtree.
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("%s cannot be moved inside itself"), *Node->Id));
			return false;
		}

		const FString Lifted = InText.Mid(Start, End - Start);
		const FString FromIndent = IndentAt(InText, OffsetOf(InText, Node->Location));

		FSplice& Removal = OutSplices.AddDefaulted_GetRef();
		Removal.Offset = ExtendBackOverLineBreak(InText, Start);
		Removal.Length = End - Removal.Offset;
		Removal.Order = InOutOrder++;

		FSplice& Insertion = OutSplices.AddDefaulted_GetRef();
		Insertion.Offset = InsertAt;
		Insertion.Length = 0;
		Insertion.Text = LineEnding + ReindentBlock(Lifted, FromIndent, Indent, LineEnding);
		Insertion.Order = InOutOrder++;
		return true;
	}

	bool PlanRenameNode(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InEdit.NewId.IsEmpty())
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(), TEXT("a rename needs a new id"));
			return false;
		}
		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s"), *Ellipsize(InEdit.NodeId)));
			return false;
		}
		if (!Node->RowKey.IsEmpty())
		{
			// Its id is made from its first value; a name of its own means writing it as a node of its own.
			RefuseRowLine(OutDiagnostics, *Node, TEXT("a row is named by its first value"));
			return false;
		}
		if (FindNodeById(InAst, InEdit.NewId) != nullptr)
		{
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("this file already declares a node called %s"), *InEdit.NewId));
			return false;
		}
		// The id sits after the type (or after the `slot` keyword), which the anchor check confirms
		// and measures. Found by scanning rather than from a location because the AST records where
		// the NODE starts, not where its id does -- and a scan checked against the id the tree says is
		// there cannot land on the wrong word.
		int32 TypeEnd = INDEX_NONE;
		if (!MatchNodeHeader(InText, *Node, TypeEnd))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("line %d does not begin with %s"), Node->Location.Line, *HeaderWordOf(*Node)));
			return false;
		}

		if (Node->bAnonymous)
		{
			// An anonymous node has no id in the text: the one in the tree was made up from where it stands, and the
			// rename is the author's first name for it. It goes where an id goes, straight after the type -- the
			// header then reads exactly as if it had been written with one from the start.
			//
			// Without `(was:)`, and on purpose. The made-up id named a member nothing can see (hidden from graphs,
			// from the variable list), so there are no graph references for the clause to carry; and the made-up id
			// does not go away -- the next anonymous sibling of the same type takes it over on the next parse, so a
			// clause naming it would be DUI3010, a rename from an id still in use, and the file would stop building.
			int32 After = TypeEnd;
			while (After < InText.Len() && IsInlineWhitespace(InText[After]))
			{
				++After;
			}
			if (After < InText.Len() && IsIdentifierChar(InText[After]))
			{
				// A word where the tree says there is none: the text has an id the tree does not know about.
				RefuseStale(OutDiagnostics, Node->Location,
					FString::Printf(TEXT("line %d names a node where the tree says it has no name"), Node->Location.Line));
				return false;
			}
			FSplice& Named = OutSplices.AddDefaulted_GetRef();
			Named.Offset = TypeEnd;
			Named.Length = 0;
			Named.Text = TEXT(" ") + InEdit.NewId;
			Named.Order = InOutOrder++;
			return true;
		}

		const int32 LineEnd = FindLineEnd(InText, TypeEnd);
		int32 Cursor = TypeEnd;
		while (Cursor < LineEnd && IsInlineWhitespace(InText[Cursor]))
		{
			++Cursor;
		}
		if (!TextAtIs(InText, Cursor, Node->Id))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("line %d does not name %s where the tree says it does"), Node->Location.Line, *Node->Id));
			return false;
		}

		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = Cursor;
		Splice.Length = Node->Id.Len();
		Splice.Text = InEdit.NewId;
		Splice.Order = InOutOrder++;

		if (!Node->WasId.IsEmpty())
		{
			// Already migrating. The clause keeps naming where this node STARTED, because everything
			// still pointing at that name has to arrive here in one hop -- rewriting it to the name
			// we are leaving would strand exactly those references.
			return true;
		}

		// `(was: OldId)` goes straight after the id, which is the canonical spelling and the position
		// the parser reads either clause from.
		FSplice& Clause = OutSplices.AddDefaulted_GetRef();
		Clause.Offset = Cursor + Node->Id.Len();
		Clause.Length = 0;
		Clause.Text = FString::Printf(TEXT(" (was: %s)"), *Node->Id);
		Clause.Order = InOutOrder++;
		return true;
	}

	bool PlanInsertComponent(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InEdit.ComponentClassName.IsEmpty())
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(), TEXT("a '+' block needs a class name"));
			return false;
		}
		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s"), *Ellipsize(InEdit.NodeId)));
			return false;
		}
		if (!Node->RowKey.IsEmpty())
		{
			RefuseRowLine(OutDiagnostics, *Node, FString::Printf(TEXT("'+ %s' cannot be added"), *InEdit.ComponentClassName));
			return false;
		}
		if (!ConfirmNodeAnchor(InText, *Node))
		{
			RefuseStale(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("line %d does not begin with %s"), Node->Location.Line, *HeaderWordOf(*Node)));
			return false;
		}

		const FString LineEnding = DetectLineEnding(InText);
		const int32 HeaderOffset = OffsetOf(InText, Node->Location);
		const FString NodeIndent = IndentAt(InText, HeaderOffset);
		const FString NewLine = FString(TEXT("+ ")) + InEdit.ComponentClassName + TEXT(" { }");

		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		const bool bHasBlock = FindBlock(InText, HeaderOffset, Open, Close);
		if (Node->Kind == EDreamUINodeKind::NamedSlot && (Node->bFillsSlot || !bHasBlock))
		{
			// A declaration's block may carry behaviours (`slot Rows default { + VerticalBox { } }`); a bare
			// declaration is left the hole its author wrote, for the reason ResolveTarget gives, and a fill's block
			// is content only.
			RefuseTarget(OutDiagnostics, Node->Location,
				Node->bFillsSlot
					? FString::Printf(TEXT("'slot %s' here fills a slot of its component: its block holds content, not behaviours"), *Node->Id)
					: FString::Printf(TEXT("'slot %s' is written without a block; write 'slot %s { }' to give it behaviours"), *Node->Id, *Node->Id));
			return false;
		}
		if (!bHasBlock)
		{
			const int32 End = HeaderEnd(InText, HeaderOffset);
			if (End <= HeaderOffset)
			{
				RefuseStale(OutDiagnostics, Node->Location, TEXT("the header this behaviour belongs to is not there"));
				return false;
			}
			FSplice& Brace = OutSplices.AddDefaulted_GetRef();
			Brace.Offset = End;
			Brace.Length = 0;
			Brace.Text = TEXT(" {");
			Brace.Order = InOutOrder++;

			FSplice& Body = OutSplices.AddDefaulted_GetRef();
			Body.Offset = FindLineEnd(InText, End);
			Body.Length = 0;
			Body.Text = LineEnding + NodeIndent + DetectIndentUnit(InText) + NewLine
				+ LineEnding + NodeIndent + TEXT("}");
			Body.Order = InOutOrder++;
			return true;
		}

		// After the last `+` block there already is, and after the properties when there are none:
		// the same order PlanInsert keeps, so a file stays properties-then-behaviours-then-children
		// however many passes have edited it.
		int32 Anchor = Open;
		FString Indent = NodeIndent + DetectIndentUnit(InText);
		for (const FDreamUIProperty& Property : Node->Properties)
		{
			// The node's own lines only: a `Shown` an `if` made stands on the `if`'s line, outside this block.
			const int32 Offset = OffsetOf(InText, Property.Location);
			if (Offset > Anchor && Offset < Close && DepthWithin(InText, Open, Offset) == 0)
			{
				Anchor = StatementSearchOffset(InText, Property, Offset);
				Indent = IndentAt(InText, Offset);
			}
		}
		for (const FDreamUIComponent& Component : Node->Components)
		{
			int32 Start = INDEX_NONE;
			int32 End = INDEX_NONE;
			if (MeasureComponentExtent(InText, Component, Start, End) && End > Anchor && End < Close)
			{
				Anchor = End;
				Indent = IndentAt(InText, Start);
			}
		}

		const int32 InsertAt = LeaveNestedConstructs(InText, Open, Close, FindLineEnd(InText, Anchor));
		if (InsertAt == INDEX_NONE)
		{
			RefuseStale(OutDiagnostics, Node->Location,
				TEXT("the last line of this block opens another block that does not end on a line of its own, so there is no line to write after"));
			return false;
		}
		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = InsertAt;
		Splice.Length = 0;
		Splice.Text = LineEnding + Indent + NewLine;
		Splice.Order = InOutOrder++;
		return true;
	}

	bool PlanRemoveComponent(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		const FDreamUINode* Node = FindNodeById(InAst, InEdit.NodeId);
		if (Node == nullptr)
		{
			RefuseTarget(OutDiagnostics, FDreamUISourceLocation(),
				FString::Printf(TEXT("no node in this file is named %s"), *Ellipsize(InEdit.NodeId)));
			return false;
		}
		if (!Node->RowKey.IsEmpty())
		{
			RefuseRowLine(OutDiagnostics, *Node, TEXT("a '+' block cannot be removed"));
			return false;
		}
		if (!Node->Components.IsValidIndex(InEdit.ComponentIndex))
		{
			RefuseTarget(OutDiagnostics, Node->Location,
				FString::Printf(TEXT("node %s has %d '+' blocks, so there is no number %d"),
					*Node->Id, Node->Components.Num(), InEdit.ComponentIndex));
			return false;
		}

		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		if (!MeasureComponentExtent(InText, Node->Components[InEdit.ComponentIndex], Start, End))
		{
			RefuseStale(OutDiagnostics, Node->Components[InEdit.ComponentIndex].Location,
				TEXT("this '+' block's text is not where the tree says it is, or shares its lines with another statement"));
			return false;
		}

		FSplice& Splice = OutSplices.AddDefaulted_GetRef();
		Splice.Offset = ExtendBackOverLineBreak(InText, Start);
		Splice.Length = End - Splice.Offset;
		Splice.Order = InOutOrder++;
		return true;
	}

	bool PlanStructuralEdit(const FString& InText, const FDreamUIAst& InAst, const FDreamUIStructuralEdit& InEdit,
		TArray<FSplice>& OutSplices, int32& InOutOrder, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		switch (InEdit.Kind)
		{
		case EDreamUIStructuralEditKind::InsertNode:
			return PlanInsertNode(InText, InAst, InEdit, OutSplices, InOutOrder, OutDiagnostics);
		case EDreamUIStructuralEditKind::RemoveNode:
			return PlanRemoveNode(InText, InAst, InEdit, OutSplices, InOutOrder, OutDiagnostics);
		case EDreamUIStructuralEditKind::MoveNode:
			return PlanMoveNode(InText, InAst, InEdit, OutSplices, InOutOrder, OutDiagnostics);
		case EDreamUIStructuralEditKind::RenameNode:
			return PlanRenameNode(InText, InAst, InEdit, OutSplices, InOutOrder, OutDiagnostics);
		case EDreamUIStructuralEditKind::InsertComponent:
			return PlanInsertComponent(InText, InAst, InEdit, OutSplices, InOutOrder, OutDiagnostics);
		case EDreamUIStructuralEditKind::RemoveComponent:
			return PlanRemoveComponent(InText, InAst, InEdit, OutSplices, InOutOrder, OutDiagnostics);
		}
		return false;
	}
}

bool FDreamUITextPatcher::ApplyStructuralEdits(FString& InOutText, const FDreamUIAst& InAst,
	TArrayView<const FDreamUIStructuralEdit> InEdits, FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamUIPatchLocal;

	bool bAllPlanned = true;
	int32 Order = 0;
	TArray<FSplice> Splices;
	for (const FDreamUIStructuralEdit& Edit : InEdits)
	{
		if (!PlanStructuralEdit(InOutText, InAst, Edit, Splices, Order, OutDiagnostics))
		{
			// The rest of the batch still applies, for the reason SetProperties gives: a caller
			// flushing nine good gestures and one bad one is better off with the nine.
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
		RefuseStale(OutDiagnostics, FDreamUISourceLocation(), TEXT("two structural edits landed on the same characters"));
		return false;
	}

	InOutText = MoveTemp(Patched);
	return bAllPlanned;
}
