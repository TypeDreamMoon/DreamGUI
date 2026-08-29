// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceFile.h"

/*
 * A hand written lexer and a recursive descent parser, in that order, both private to this file.
 *
 * Hand written rather than table driven because the grammar is small enough to read in one sitting
 * and the thing that actually matters about it is not speed -- it is that every token remembers the
 * byte offset it started at. The write-back patcher edits the .dui in place, and it finds the line
 * to change from the location on the AST node. A location that is merely near the property produces
 * an edit that lands on the property above it, silently, in a file the author did not open.
 *
 * The two stages are separate for one concrete reason: the lexer can report and then keep going.
 * A stray character, an unterminated string, a hex colour with five digits -- all of those still
 * yield a token of the right SHAPE, so the parser sees a well formed statement and can go on to
 * find the four other things wrong further down. Fusing the stages would make every lexical mistake
 * also a structural one.
 *
 * Two conventions run through the whole file and are worth stating once:
 *
 *   Names compare case insensitively, keywords compare case sensitively. A node id becomes an FName
 *   member variable downstream and FName does not distinguish case, so treating OkBtn and okbtn as
 *   two ids here would be stricter than the thing this feeds. Keywords are grammar, not names, and
 *   `For` is not `for` any more than it is in C.
 *
 *   Every error path consumes at least one token. The block loops assert this by remembering the
 *   token index and forcing an advance if a statement made no progress -- without it a recovery
 *   path that returns without consuming turns a malformed file into a hang, which is the one
 *   failure mode a syntax checker is never allowed to have.
 */

namespace DreamUIText
{
	// --------------------------------------------------------------------------------------------
	// Tokens
	// --------------------------------------------------------------------------------------------

	enum class ETokenKind : uint8
	{
		EndOfFile,
		/**
		 * A newline, or a `;`.
		 *
		 * The two are the same token because `;` is tolerated rather than meaningful: a model writing
		 * .dui has spent its life writing C-family languages and will put semicolons in. Dropping
		 * them entirely would merge `A = 1; B = 2` into one unreadable statement, so they end
		 * statements exactly like the newline the author should have written.
		 */
		Separator,
		Identifier,
		Number,
		/** Quotes stripped, escapes resolved. */
		String,
		/** The digits only -- the '#' is delimiter, like a quote. */
		HexColor,
		AssetPath,
		OpenBrace,
		CloseBrace,
		OpenParen,
		CloseParen,
		Comma,
		Dot,
		Colon,
		Equals,
		/** `<-`, the binding arrow. */
		Arrow,
		Plus,
		At,
	};

	struct FToken
	{
		ETokenKind Kind = ETokenKind::EndOfFile;
		/** Identifier / Number / AssetPath: as written. String: unescaped. HexColor: digits, no '#'. */
		FString Text;
		FDreamUISourceLocation Location;
		/**
		 * Offsets into the source, first character and one past the last.
		 *
		 * Carried alongside the line/column pair because tuple elements are kept as RAW SOURCE TEXT
		 * (see FDreamUIValue::Elements) and the honest way to produce raw text is to slice the file
		 * between two token boundaries, not to re-print the tokens and hope the spacing survives.
		 */
		int32 Start = 0;
		int32 End = 0;

		/**
		 * Number only: digits glued straight onto letters -- 24px, 2ndPanel.
		 *
		 * The one shape the lexer genuinely cannot judge on its own. In a value it is a malformed
		 * number; in a node header it is a name that begins with a digit; and the two want different
		 * codes and completely different advice. So it is carried out unreported and diagnosed by
		 * whichever parser position it lands in -- which is why "2ndPanel" is not first accused of
		 * being a bad number when it was plainly meant as a name.
		 */
		bool bDigitLeadingWord = false;
	};

	// --------------------------------------------------------------------------------------------
	// Character classes
	// --------------------------------------------------------------------------------------------

	// Digits and hex digits are spelled out rather than delegated to FChar, because these are the
	// grammar's definition of a digit, not the platform's: FChar's are locale-shaped and would
	// quietly accept a full-width digit on one machine and not another.

	FORCEINLINE bool IsDigit(TCHAR InChar)
	{
		return InChar >= TEXT('0') && InChar <= TEXT('9');
	}

	FORCEINLINE bool IsHexDigit(TCHAR InChar)
	{
		return IsDigit(InChar)
			|| (InChar >= TEXT('a') && InChar <= TEXT('f'))
			|| (InChar >= TEXT('A') && InChar <= TEXT('F'));
	}

	/**
	 * The identifier rule, COPIED FROM UDreamWidgetTree::SanitizeIdentifier and required to stay
	 * copied. If that function changes, this changes with it, in the same commit.
	 *
	 * That function is what turns a widget's DisplayName into its class member variable name, and a
	 * node's id IS its DisplayName. So the two rules agreeing is not tidiness -- it is the property
	 * that an id this parser accepts survives SanitizeIdentifier unchanged. Let them drift and you
	 * get the worst class of bug this language can produce: an id that is legal in the .dui, legal
	 * in the tree, and silently a DIFFERENT name by the time the Blueprint declares a variable for
	 * it, so every binding that named it comes back null and nothing anywhere reported a thing.
	 *
	 * Hence `> 0x7F`, verbatim from the sanitizer: CJK display names are ordinary here, the runtime
	 * keeps them (DreamUserWidgetAutomationTests has a case asserting a CJK display name survives as
	 * a variable name), and an ASCII-only .dui could not express a name the designer already can. It
	 * follows the sanitizer in what it does NOT
	 * examine, too -- a non-ASCII space would be taken for a letter by both, which is the price of
	 * having one rule instead of two that disagree.
	 */
	FORCEINLINE bool IsIdentifierChar(TCHAR InChar)
	{
		return IsDigit(InChar)
			|| InChar == TEXT('_')
			|| (InChar >= TEXT('a') && InChar <= TEXT('z'))
			|| (InChar >= TEXT('A') && InChar <= TEXT('Z'))
			|| InChar > 0x7F;
	}

	/** The same set minus the digits -- the sanitizer prefixes an underscore rather than keep one. */
	FORCEINLINE bool IsIdentifierStart(TCHAR InChar)
	{
		return IsIdentifierChar(InChar) && !IsDigit(InChar);
	}

	/**
	 * `/Game/UI/WBP_SlotCard.WBP_SlotCard_C` -- an object path as the engine spells one.
	 *
	 * Non-ASCII comes along through IsIdentifierChar, which is correct rather than incidental: an
	 * asset may be named in Chinese, and its path is then the only way to refer to it.
	 */
	FORCEINLINE bool IsPathChar(TCHAR InChar)
	{
		return IsIdentifierChar(InChar) || InChar == TEXT('/') || InChar == TEXT('.');
	}

	FORCEINLINE bool IsInlineWhitespace(TCHAR InChar)
	{
		return InChar == TEXT(' ') || InChar == TEXT('\t') || InChar == TEXT('\v') || InChar == TEXT('\f');
	}

	/** Keywords, and therefore the words a node id may not be. Case sensitive, see the file comment. */
	bool IsReservedWord(const FString& InWord)
	{
		static const TCHAR* Reserved[] = { TEXT("class"), TEXT("style"), TEXT("slot"), TEXT("for"), TEXT("each"), TEXT("in"), TEXT("was") };
		for (const TCHAR* Word : Reserved)
		{
			if (InWord.Equals(Word, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	/** Clamped so a .dui path accidentally aimed at a .png reports a readable snippet, not the file. */
	FString Ellipsize(const FString& InText, int32 InMaxLength = 16)
	{
		return InText.Len() <= InMaxLength ? InText : (InText.Left(InMaxLength) + TEXT("..."));
	}

	// --------------------------------------------------------------------------------------------
	// Lexer
	// --------------------------------------------------------------------------------------------

	class FLexer
	{
	public:
		FLexer(const FString& InText, FDreamUIDiagnosticBag& InDiagnostics)
			: Text(InText)
			, Chars(*InText)
			, Length(InText.Len())
			, Diagnostics(InDiagnostics)
		{
		}

		void Run(TArray<FToken>& OutTokens)
		{
			while (Offset < Length)
			{
				const TCHAR Char = Chars[Offset];

				if (IsInlineWhitespace(Char))
				{
					++Offset;
					continue;
				}
				if (Char == TEXT('\n') || Char == TEXT('\r'))
				{
					LexLineBreak(OutTokens);
					continue;
				}
				if (Char == TEXT('/'))
				{
					// Order matters and only here: `//` and `/*` both start with the character that
					// also starts every asset path, and `/Game/...` is by far the most common of the
					// three. Comments are checked first because a path can never contain `//` or `/*`
					// but a comment can perfectly well contain a path.
					const TCHAR Next = PeekChar(1);
					if (Next == TEXT('/'))
					{
						SkipLineComment();
						continue;
					}
					if (Next == TEXT('*'))
					{
						SkipBlockComment(OutTokens);
						continue;
					}
					LexAssetPath(OutTokens);
					continue;
				}
				if (IsIdentifierStart(Char))
				{
					LexIdentifier(OutTokens);
					continue;
				}
				if (IsDigit(Char) || Char == TEXT('-'))
				{
					LexNumber(OutTokens);
					continue;
				}
				if (Char == TEXT('"'))
				{
					LexString(OutTokens);
					continue;
				}
				if (Char == TEXT('#'))
				{
					LexHexColor(OutTokens);
					continue;
				}
				if (Char == TEXT('<') && PeekChar(1) == TEXT('-'))
				{
					EmitPunctuation(OutTokens, ETokenKind::Arrow, 2);
					continue;
				}
				if (Char == TEXT(';'))
				{
					EmitPunctuation(OutTokens, ETokenKind::Separator, 1);
					continue;
				}

				switch (Char)
				{
				case TEXT('{'): EmitPunctuation(OutTokens, ETokenKind::OpenBrace, 1); continue;
				case TEXT('}'): EmitPunctuation(OutTokens, ETokenKind::CloseBrace, 1); continue;
				case TEXT('('): EmitPunctuation(OutTokens, ETokenKind::OpenParen, 1); continue;
				case TEXT(')'): EmitPunctuation(OutTokens, ETokenKind::CloseParen, 1); continue;
				case TEXT(','): EmitPunctuation(OutTokens, ETokenKind::Comma, 1); continue;
				case TEXT('.'): EmitPunctuation(OutTokens, ETokenKind::Dot, 1); continue;
				case TEXT(':'): EmitPunctuation(OutTokens, ETokenKind::Colon, 1); continue;
				case TEXT('='): EmitPunctuation(OutTokens, ETokenKind::Equals, 1); continue;
				case TEXT('+'): EmitPunctuation(OutTokens, ETokenKind::Plus, 1); continue;
				case TEXT('@'): EmitPunctuation(OutTokens, ETokenKind::At, 1); continue;
				default: break;
				}

				LexUnexpectedRun();
			}

			FToken EndToken;
			EndToken.Kind = ETokenKind::EndOfFile;
			EndToken.Location = MakeLocation(Offset);
			EndToken.Start = Length;
			EndToken.End = Length;
			OutTokens.Add(MoveTemp(EndToken));
		}

	private:
		const FString& Text;
		const TCHAR* Chars = nullptr;
		int32 Length = 0;
		FDreamUIDiagnosticBag& Diagnostics;

		int32 Offset = 0;
		int32 Line = 1;
		/** Offset of the first character of the current line, so a column is one subtraction. */
		int32 LineStart = 0;

		FORCEINLINE TCHAR PeekChar(int32 InAhead) const
		{
			const int32 At = Offset + InAhead;
			return At < Length ? Chars[At] : TEXT('\0');
		}

		FORCEINLINE FDreamUISourceLocation MakeLocation(int32 InOffset) const
		{
			return FDreamUISourceLocation(Line, InOffset - LineStart + 1);
		}

		FString Slice(int32 InStart, int32 InEnd) const
		{
			return Text.Mid(InStart, FMath::Max(0, InEnd - InStart));
		}

		void Emit(TArray<FToken>& OutTokens, ETokenKind InKind, int32 InStart, FString InText)
		{
			FToken Token;
			Token.Kind = InKind;
			Token.Text = MoveTemp(InText);
			Token.Location = MakeLocation(InStart);
			Token.Start = InStart;
			Token.End = Offset;
			OutTokens.Add(MoveTemp(Token));
		}

		void EmitPunctuation(TArray<FToken>& OutTokens, ETokenKind InKind, int32 InLength)
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);
			Offset += InLength;

			FToken Token;
			Token.Kind = InKind;
			Token.Text = Slice(Start, Offset);
			Token.Location = Location;
			Token.Start = Start;
			Token.End = Offset;
			OutTokens.Add(MoveTemp(Token));
		}

		/**
		 * Steps over one line ending of any flavour and moves the line counter with it.
		 *
		 * Shared by the top level scan and the block comment scan on purpose: a comment that spans
		 * lines still has to move the counter, and a second copy of "is this \r\n or a lone \r" is
		 * how every line number after the first block comment ends up one too small.
		 */
		bool ConsumeLineBreak()
		{
			if (Offset >= Length)
			{
				return false;
			}
			const TCHAR Char = Chars[Offset];
			if (Char != TEXT('\n') && Char != TEXT('\r'))
			{
				return false;
			}
			++Offset;
			if (Char == TEXT('\r') && Offset < Length && Chars[Offset] == TEXT('\n'))
			{
				++Offset;
			}
			++Line;
			LineStart = Offset;
			return true;
		}

		void LexLineBreak(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);
			ConsumeLineBreak();

			FToken Token;
			Token.Kind = ETokenKind::Separator;
			Token.Location = Location;
			Token.Start = Start;
			Token.End = Offset;
			OutTokens.Add(MoveTemp(Token));
		}

		void SkipLineComment()
		{
			while (Offset < Length && Chars[Offset] != TEXT('\n') && Chars[Offset] != TEXT('\r'))
			{
				++Offset;
			}
			// The line break itself is left for the main loop, which turns it into the separator that
			// ends the statement the comment was trailing.
		}

		void SkipBlockComment(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);
			Offset += 2;

			bool bClosed = false;
			bool bCrossedLine = false;
			while (Offset < Length)
			{
				if (Chars[Offset] == TEXT('*') && PeekChar(1) == TEXT('/'))
				{
					Offset += 2;
					bClosed = true;
					break;
				}
				if (ConsumeLineBreak())
				{
					bCrossedLine = true;
					continue;
				}
				++Offset;
			}

			if (!bClosed)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::UnterminatedComment, Location,
					TEXT("this '/*' never reaches a '*/'"));
			}

			// A comment that crossed a line still ends the statement it started on. Deleting the line
			// break with the comment would silently join two statements -- the author sees two lines,
			// the parser sees one, and the resulting complaint names a token nowhere near the comment.
			if (bCrossedLine)
			{
				FToken Token;
				Token.Kind = ETokenKind::Separator;
				Token.Location = MakeLocation(Offset);
				Token.Start = Offset;
				Token.End = Offset;
				OutTokens.Add(MoveTemp(Token));
			}
		}

		void LexIdentifier(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			while (Offset < Length && IsIdentifierChar(Chars[Offset]))
			{
				++Offset;
			}
			Emit(OutTokens, ETokenKind::Identifier, Start, Slice(Start, Offset));
		}

		void LexNumber(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);

			const bool bSigned = Chars[Offset] == TEXT('-');
			if (bSigned)
			{
				++Offset;
			}

			int32 IntegerDigits = 0;
			while (Offset < Length && IsDigit(Chars[Offset]))
			{
				++Offset;
				++IntegerDigits;
			}

			// A '.' only belongs to the number when a digit follows it. Without that lookahead the
			// lexer would eat the dot out of a construct that has nothing to do with numbers, and the
			// resulting complaint would name a token two statements away.
			if (Offset < Length && Chars[Offset] == TEXT('.') && IsDigit(PeekChar(1)))
			{
				++Offset;
				while (Offset < Length && IsDigit(Chars[Offset]))
				{
					++Offset;
				}
			}

			// The exponent, and it is not a nicety. DreamUIValueFormat::Print falls back to scientific
			// notation once a magnitude reaches 1e17, or when a shortest-round-trip decimal would run
			// past 24 places -- so `1e+20` and `1e-45` are text the DESIGNER writes into a .dui, not
			// text a human would type. A lexer that stopped at the 'e' would break the round trip in
			// the one direction nobody checks by hand: drag a value, save, reopen, and the file no
			// longer compiles on a line the author never touched.
			//
			// `e` is also an ordinary identifier character, so without this the trailing sweep below
			// would swallow it and call the whole thing a malformed number.
			bool bBrokenExponent = false;
			if (IntegerDigits > 0 && Offset < Length && (Chars[Offset] == TEXT('e') || Chars[Offset] == TEXT('E')))
			{
				int32 Cursor = Offset + 1;
				if (Cursor < Length && (Chars[Cursor] == TEXT('+') || Chars[Cursor] == TEXT('-')))
				{
					++Cursor;
				}
				if (Cursor < Length && IsDigit(Chars[Cursor]))
				{
					Offset = Cursor;
					while (Offset < Length && IsDigit(Chars[Offset]))
					{
						++Offset;
					}
				}
				else
				{
					// `1e`, `400e`, `1e+`. The 'e' is consumed anyway so the complaint names the whole
					// word rather than reporting a clean `400` and leaving a stray `e` to fail again
					// as something else two tokens later.
					Offset = Cursor;
					bBrokenExponent = true;
				}
			}

			// Everything that reads like part of the number but cannot be: a second decimal point, a
			// trailing one, an exponent with no digits after it, a unit glued on the end (24px).
			// Swallowed into this token rather than left behind, because leaving `px` would produce a
			// bare identifier that looks exactly like a node header and buries the real mistake under
			// a second, invented one.
			bool bTrailingDot = false;
			bool bTrailingWord = false;
			while (Offset < Length && (Chars[Offset] == TEXT('.') || IsIdentifierChar(Chars[Offset])))
			{
				bTrailingDot |= Chars[Offset] == TEXT('.');
				bTrailingWord |= IsIdentifierChar(Chars[Offset]);
				++Offset;
			}

			// Plain digits followed by plain letters is the ambiguous shape, and only the position
			// the token lands in can settle it -- see FToken::bDigitLeadingWord. A sign, a stray dot
			// or a half-written exponent removes the ambiguity: nobody writes a name as -3px, 1.2.3
			// or 400e.
			const bool bDigitLeadingWord = !bSigned && !bBrokenExponent
				&& IntegerDigits > 0 && bTrailingWord && !bTrailingDot;

			const FString Raw = Slice(Start, Offset);
			if (!bDigitLeadingWord && (IntegerDigits == 0 || bTrailingDot || bTrailingWord || bBrokenExponent))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedNumber, Location,
					FString::Printf(TEXT("'%s' is not a number: write it as -12, 0.95 or 1e-45"), *Ellipsize(Raw)));
			}

			// Emitted even when malformed. The statement around it is probably fine, and the parser
			// finding a value where a value belongs is what lets it go on to the rest of the file.
			Emit(OutTokens, ETokenKind::Number, Start, Raw);
			OutTokens.Last().bDigitLeadingWord = bDigitLeadingWord;
		}

		void LexString(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);
			++Offset;

			FString Value;
			bool bClosed = false;
			while (Offset < Length)
			{
				const TCHAR Char = Chars[Offset];

				// Strings do not span lines. A missing closing quote is otherwise the single most
				// destructive mistake possible in this grammar -- it would swallow the rest of the
				// file into one literal and report the failure hundreds of lines from the typo.
				if (Char == TEXT('\n') || Char == TEXT('\r'))
				{
					break;
				}
				if (Char == TEXT('"'))
				{
					++Offset;
					bClosed = true;
					break;
				}
				if (Char == TEXT('\\'))
				{
					const TCHAR Escaped = PeekChar(1);
					if (Offset + 1 >= Length || Escaped == TEXT('\n') || Escaped == TEXT('\r'))
					{
						++Offset;
						break;
					}
					Offset += 2;
					switch (Escaped)
					{
					case TEXT('"'):  Value.AppendChar(TEXT('"'));  break;
					case TEXT('\\'): Value.AppendChar(TEXT('\\')); break;
					case TEXT('n'):  Value.AppendChar(TEXT('\n')); break;
					case TEXT('t'):  Value.AppendChar(TEXT('\t')); break;
					case TEXT('r'):  Value.AppendChar(TEXT('\r')); break;
					default:
						// An escape nobody defined keeps both characters instead of eating the
						// backslash. There is no diagnostic for it because the code table has none,
						// and losing a character silently is the worse of the two failures: the
						// patcher round trips this text, and what it cannot see it cannot preserve.
						Value.AppendChar(TEXT('\\'));
						Value.AppendChar(Escaped);
						break;
					}
					continue;
				}

				Value.AppendChar(Char);
				++Offset;
			}

			if (!bClosed)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::UnterminatedString, Location,
					TEXT("this string has no closing quote before the end of the line"));
			}

			Emit(OutTokens, ETokenKind::String, Start, MoveTemp(Value));
		}

		void LexHexColor(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);
			++Offset;

			// The whole word is taken, not just the hex-looking prefix, so #GGGGGG reports once and
			// says what is wrong with it rather than leaving GGGGGG behind as a bare identifier that
			// then fails again somewhere else for an unrelated-looking reason.
			const int32 DigitsStart = Offset;
			while (Offset < Length && IsIdentifierChar(Chars[Offset]))
			{
				++Offset;
			}
			const FString Digits = Slice(DigitsStart, Offset);

			bool bValid = Digits.Len() == 3 || Digits.Len() == 4 || Digits.Len() == 6 || Digits.Len() == 8;
			if (bValid)
			{
				for (int32 Index = 0; Index < Digits.Len(); ++Index)
				{
					if (!IsHexDigit(Digits[Index]))
					{
						bValid = false;
						break;
					}
				}
			}

			if (!bValid)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedHexColor, Location,
					FString::Printf(TEXT("'#%s' is not a colour: a '#' takes 3, 4, 6 or 8 hex digits"), *Ellipsize(Digits)));
			}

			Emit(OutTokens, ETokenKind::HexColor, Start, Digits);
		}

		void LexAssetPath(TArray<FToken>& OutTokens)
		{
			const int32 Start = Offset;
			while (Offset < Length && IsPathChar(Chars[Offset]))
			{
				++Offset;
			}
			Emit(OutTokens, ETokenKind::AssetPath, Start, Slice(Start, Offset));
		}

		/** True when the character cannot begin any token, so a run of them is one mistake, not many. */
		bool IsUnexpectedAt(int32 InOffset) const
		{
			const TCHAR Char = Chars[InOffset];
			if (Char == TEXT('<'))
			{
				// Only unexpected when it is not the arrow, so a run never swallows a `<-`.
				return !(InOffset + 1 < Length && Chars[InOffset + 1] == TEXT('-'));
			}
			if (IsInlineWhitespace(Char) || Char == TEXT('\n') || Char == TEXT('\r'))
			{
				return false;
			}
			if (IsIdentifierStart(Char) || IsDigit(Char))
			{
				return false;
			}
			switch (Char)
			{
			case TEXT('-'): case TEXT('"'): case TEXT('#'): case TEXT('/'): case TEXT(';'):
			case TEXT('{'): case TEXT('}'): case TEXT('('): case TEXT(')'): case TEXT(','):
			case TEXT('.'): case TEXT(':'): case TEXT('='): case TEXT('+'): case TEXT('@'):
				return false;
			default:
				return true;
			}
		}

		void LexUnexpectedRun()
		{
			const int32 Start = Offset;
			const FDreamUISourceLocation Location = MakeLocation(Start);
			while (Offset < Length && IsUnexpectedAt(Offset))
			{
				++Offset;
			}

			// Reported as a run rather than per character, because the case that gets here in practice
			// is a Source File aimed at something that is not a .dui at all -- per-character
			// diagnostics over a binary would bury every other problem in the message log. Letters
			// never reach here, non-ASCII ones included; see IsIdentifierChar.
			const FString Run = Slice(Start, Offset);
			Diagnostics.AddError(EDreamUIDiagnosticCode::UnexpectedCharacter, Location,
				FString::Printf(TEXT("'%s' cannot begin anything this grammar recognises"), *Ellipsize(Run)));
		}
	};

	// --------------------------------------------------------------------------------------------
	// Parser
	// --------------------------------------------------------------------------------------------

	class FParser
	{
	public:
		FParser(const FString& InText, const TArray<FToken>& InTokens, FDreamUIDiagnosticBag& InDiagnostics)
			: Text(InText)
			, Tokens(InTokens)
			, Diagnostics(InDiagnostics)
		{
		}

		void ParseFile(FDreamUIAst& OutAst)
		{
			for (;;)
			{
				SkipSeparators();
				if (IsAtEnd())
				{
					break;
				}

				const int32 IndexBefore = Index;

				if (CheckKeyword(TEXT("class")))
				{
					ParseClassDeclaration(OutAst);
				}
				else if (CheckKeyword(TEXT("style")))
				{
					// Accepted anywhere at file scope, including after the root, and that is on
					// purpose rather than an oversight to be tidied up later: styles resolve by name
					// in a pass over the finished tree, so their position carries no meaning and
					// there is nothing for a diagnostic to be about. Putting them at the top is a
					// convention for readers, not a rule for the parser.
					ParseStyleDeclaration(OutAst);
				}
				else if (CheckKeyword(TEXT("slot")) || CheckKeyword(TEXT("for")) || CheckKeyword(TEXT("each")))
				{
					// Caught here rather than left to fall through as a node header, which is what it
					// looks like: `for Slot in GetSlots()` at file scope would otherwise parse as a
					// node of type `for` named `Slot` and complain about the `in`.
					Diagnostics.AddError(EDreamUIDiagnosticCode::UnexpectedToken, Current().Location,
						FString::Printf(TEXT("'%s' belongs inside a node, not at the top of the file"), *Current().Text));
					RecoverToStatementBoundary();
				}
				else
				{
					FDreamUINode Node;
					if (ParseNode(Node))
					{
						if (!OutAst.bHasRoot)
						{
							OutAst.Root = MoveTemp(Node);
							OutAst.bHasRoot = true;
						}
						else
						{
							// The first one wins and the rest are reported where they stand, so an
							// author who pasted a second tree sees which one is the intruder rather
							// than a complaint about the file as a whole.
							Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedRoot, Node.Location,
								FString::Printf(TEXT("a .dui holds exactly one root node, and '%s' is a second one"), *Node.Id));
						}
					}
				}

				// Forward progress, unconditionally. Every recovery path is supposed to consume, and
				// this is what makes "supposed to" not matter.
				if (Index == IndexBefore)
				{
					Advance();
				}
			}

			if (!OutAst.bHasRoot)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedRoot, Tokens.Last().Location,
					TEXT("this file declares no root node"));
			}

			CheckNamesAcrossTheTree(OutAst);
		}

	private:
		const FString& Text;
		const TArray<FToken>& Tokens;
		FDreamUIDiagnosticBag& Diagnostics;
		int32 Index = 0;

		/** The loop variables in scope at the current point, innermost last. Only used for shadowing. */
		TArray<FString> ActiveLoopVariables;

		// --- token stream -------------------------------------------------------------------------

		// Peek clamps to the trailing EndOfFile token, which the lexer always emits, so there is no
		// bounds check anywhere else in the parser and no path where lookahead reads off the end.
		const FToken& Peek(int32 InAhead = 0) const
		{
			return Tokens[FMath::Min(Index + InAhead, Tokens.Num() - 1)];
		}
		const FToken& Current() const { return Peek(0); }
		bool Check(ETokenKind InKind) const { return Current().Kind == InKind; }
		bool IsAtEnd() const { return Current().Kind == ETokenKind::EndOfFile; }

		bool CheckKeyword(const TCHAR* InWord) const
		{
			return Current().Kind == ETokenKind::Identifier && Current().Text.Equals(InWord, ESearchCase::CaseSensitive);
		}

		void Advance()
		{
			if (Index < Tokens.Num() - 1)
			{
				++Index;
			}
		}

		void SkipSeparators()
		{
			while (Check(ETokenKind::Separator))
			{
				Advance();
			}
		}

		FString Slice(int32 InStart, int32 InEnd) const
		{
			return Text.Mid(InStart, FMath::Max(0, InEnd - InStart));
		}

		// --- recovery -----------------------------------------------------------------------------

		/**
		 * Walk to the end of the statement that went wrong, leaving its terminator for the caller.
		 *
		 * Depth tracking is the whole point: a mistake in a node header must not throw away the block
		 * that follows it, because the block is where the other four mistakes are. The one thing an
		 * author cannot use is a front end that reports the first problem and stops -- generated
		 * files come with several of the same kind, and one per round trip is several round trips.
		 */
		void RecoverToStatementBoundary()
		{
			int32 Depth = 0;
			while (!IsAtEnd())
			{
				const ETokenKind Kind = Current().Kind;
				if (Depth == 0 && (Kind == ETokenKind::Separator || Kind == ETokenKind::CloseBrace))
				{
					return;
				}
				if (Kind == ETokenKind::OpenBrace || Kind == ETokenKind::OpenParen)
				{
					++Depth;
				}
				else if (Kind == ETokenKind::CloseBrace || Kind == ETokenKind::CloseParen)
				{
					Depth = FMath::Max(0, Depth - 1);
				}
				Advance();
			}
		}

		void SkipBalancedBlock()
		{
			if (!Check(ETokenKind::OpenBrace))
			{
				return;
			}
			int32 Depth = 0;
			do
			{
				if (Check(ETokenKind::OpenBrace))
				{
					++Depth;
				}
				else if (Check(ETokenKind::CloseBrace))
				{
					--Depth;
				}
				Advance();
			}
			while (Depth > 0 && !IsAtEnd());
		}

		/** Past the next `)`, but never out of the statement -- a missing one must not eat the block. */
		void SkipPastCloseParen()
		{
			while (!IsAtEnd() && !Check(ETokenKind::Separator) && !Check(ETokenKind::CloseBrace))
			{
				const bool bWasClose = Check(ETokenKind::CloseParen);
				Advance();
				if (bWasClose)
				{
					return;
				}
			}
		}

		void RaiseUnexpectedToken(FString InExpectation)
		{
			Diagnostics.AddError(EDreamUIDiagnosticCode::UnexpectedToken, Current().Location,
				FString::Printf(TEXT("%s, found '%s'"), *InExpectation, *DescribeCurrent()));
		}

		/** What the reader sees at the cursor, in the words they wrote, so a message can quote it. */
		FString DescribeCurrent() const
		{
			switch (Current().Kind)
			{
			case ETokenKind::EndOfFile:  return TEXT("the end of the file");
			case ETokenKind::Separator:  return TEXT("the end of the line");
			case ETokenKind::String:     return FString::Printf(TEXT("\"%s\""), *Ellipsize(Current().Text));
			case ETokenKind::HexColor:   return FString::Printf(TEXT("#%s"), *Ellipsize(Current().Text));
			default:                     return Ellipsize(Current().Text);
			}
		}

		// --- file scope ---------------------------------------------------------------------------

		void ParseClassDeclaration(FDreamUIAst& OutAst)
		{
			Advance(); // 'class'

			if (!Check(ETokenKind::AssetPath))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedClassDeclaration, Current().Location,
					FString::Printf(TEXT("'class' takes an asset path, as in 'class /Game/UI/WBP_SavePanel', found '%s'"), *DescribeCurrent()));
				RecoverToStatementBoundary();
				return;
			}

			const FToken& PathToken = Current();
			if (PathToken.Text.Len() <= 1)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedClassDeclaration, PathToken.Location,
					TEXT("this 'class' declaration has an empty path"));
				Advance();
				return;
			}
			if (!OutAst.ClassPath.IsEmpty())
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedClassDeclaration, PathToken.Location,
					FString::Printf(TEXT("this file already compiles into '%s'; a .dui names one class"), *OutAst.ClassPath));
				Advance();
				return;
			}

			OutAst.ClassPath = PathToken.Text;
			// The path, not the keyword: the patcher retargets a file by rewriting this token, and it
			// needs the column of the thing it replaces.
			OutAst.ClassPathLocation = PathToken.Location;
			Advance();
		}

		void ParseStyleDeclaration(FDreamUIAst& OutAst)
		{
			const FDreamUISourceLocation KeywordLocation = Current().Location;
			Advance(); // 'style'

			if (!Check(ETokenKind::Identifier))
			{
				RaiseUnexpectedToken(TEXT("expected a style name after 'style'"));
				RecoverToStatementBoundary();
				return;
			}

			FDreamUIStyle Style;
			Style.Location = KeywordLocation;
			Style.Name = Current().Text;
			Advance();

			if (!Check(ETokenKind::OpenBrace))
			{
				RaiseUnexpectedToken(FString::Printf(TEXT("style '%s' needs a '{ ... }' block"), *Style.Name));
				RecoverToStatementBoundary();
				return;
			}
			const FDreamUISourceLocation OpenLocation = Current().Location;
			Advance();
			ParsePropertyOnlyBlock(Style.Properties, OpenLocation, TEXT("a style"));

			if (OutAst.FindStyle(Style.Name) != nullptr)
			{
				// Dropped rather than appended, so FindStyle keeps naming the first one for everybody
				// downstream. Two entries under one name is the kind of state where the builder and
				// the patcher can disagree about which properties a node got.
				Diagnostics.AddError(EDreamUIDiagnosticCode::DuplicateStyle, Style.Location,
					FString::Printf(TEXT("style '%s' is declared twice"), *Style.Name));
				return;
			}
			OutAst.Styles.Add(MoveTemp(Style));
		}

		// --- nodes --------------------------------------------------------------------------------

		bool ParseNode(FDreamUINode& OutNode)
		{
			if (!Check(ETokenKind::Identifier) && !Check(ETokenKind::AssetPath))
			{
				RaiseUnexpectedToken(TEXT("expected a node type or a property name"));
				RecoverToStatementBoundary();
				return false;
			}

			const FToken& TypeToken = Current();
			OutNode.Kind = EDreamUINodeKind::Widget;
			OutNode.TypeName = TypeToken.Text;
			OutNode.Location = TypeToken.Location;
			const FString TypeName = TypeToken.Text;
			const FDreamUISourceLocation TypeLocation = TypeToken.Location;
			Advance();

			// The type is taken exactly as written and never checked. Whether `Image` is a tag and
			// whether /Game/UI/WBP_SlotCard loads needs reflection and the asset registry, which is
			// the builder's half -- UnknownNodeType is a 3xxx code raised there. A parser that kept
			// its own list of legal tags would be a second source of truth for the tag table, and it
			// would be wrong the first time somebody adds a widget.

			if (Check(ETokenKind::Identifier))
			{
				const FToken& IdToken = Current();
				OutNode.Id = IdToken.Text;
				if (IsReservedWord(OutNode.Id))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::InvalidNodeId, IdToken.Location,
						FString::Printf(TEXT("'%s' is a keyword and cannot be a node id"), *OutNode.Id));
				}
				Advance();
			}
			else if (Check(ETokenKind::Number))
			{
				// Where `2ndPanel` lands, and a bare `2` with it. Refused rather than accepted,
				// because SanitizeIdentifier WOULD take it -- by prefixing an underscore. The .dui
				// would say 2ndPanel, the generated class would declare _2ndPanel, and every binding
				// written against the name in the file would resolve to nothing.
				Diagnostics.AddError(EDreamUIDiagnosticCode::InvalidNodeId, Current().Location,
					FString::Printf(TEXT("'%s' cannot be a node id: an id does not begin with a digit"),
						*Ellipsize(Current().Text)));
				Advance();
			}
			else
			{
				// Reported against the type, because that is the word the author looks at, and the
				// message offers the other reading: a lone identifier on a line is just as likely to
				// be a property whose '=' went missing.
				Diagnostics.AddError(EDreamUIDiagnosticCode::MissingNodeId, TypeLocation,
					FString::Printf(TEXT("'%s' needs an id, as in '%s MyName' -- or an '=' if it was meant to be a property"),
						*TypeName, *TypeName));
			}

			// The two optional clauses in either order. The canonical spelling puts (was:) first, but
			// accepting the reverse costs a loop and saves an author from a syntax error over a
			// difference that cannot mean anything else.
			bool bHadWasClause = false;
			bool bHadStyleClause = false;
			for (;;)
			{
				if (Check(ETokenKind::OpenParen) && !bHadWasClause)
				{
					ParseWasClause(OutNode);
					bHadWasClause = true;
					continue;
				}
				if (Check(ETokenKind::Colon) && !bHadStyleClause)
				{
					ParseStyleClause(OutNode);
					bHadStyleClause = true;
					continue;
				}
				break;
			}

			if (Check(ETokenKind::OpenBrace))
			{
				const FDreamUISourceLocation OpenLocation = Current().Location;
				Advance();
				ParseNodeBody(OutNode, OpenLocation);
			}
			else if (!Check(ETokenKind::Separator) && !Check(ETokenKind::CloseBrace) && !IsAtEnd())
			{
				RaiseUnexpectedToken(FString::Printf(TEXT("expected '{' or the end of the line after node '%s'"), *OutNode.Id));
				RecoverToStatementBoundary();
			}

			return true;
		}

		void ParseWasClause(FDreamUINode& OutNode)
		{
			const FDreamUISourceLocation OpenLocation = Current().Location;
			Advance(); // '('

			bool bWellFormed = false;
			if (CheckKeyword(TEXT("was")))
			{
				Advance();
				if (Check(ETokenKind::Colon))
				{
					Advance();
					if (Check(ETokenKind::Identifier))
					{
						OutNode.WasId = Current().Text;
						Advance();
						bWellFormed = Check(ETokenKind::CloseParen);
						if (bWellFormed)
						{
							Advance();
						}
					}
				}
			}

			if (!bWellFormed)
			{
				// Cleared, not left half filled. A partial WasId is worse than none: the compiler uses
				// it to move a Blueprint's graph references from one variable to another, and moving
				// them to a name the author did not write is a rename nobody asked for.
				OutNode.WasId.Reset();
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedWasClause, OpenLocation,
					TEXT("a rename clause is written '(was: OldId)'"));
				SkipPastCloseParen();
			}
		}

		void ParseStyleClause(FDreamUINode& OutNode)
		{
			Advance(); // ':'
			if (!Check(ETokenKind::Identifier))
			{
				RaiseUnexpectedToken(TEXT("expected a style name after ':'"));
				return;
			}
			OutNode.StyleName = Current().Text;
			Advance();
		}

		void ParseNodeBody(FDreamUINode& OutNode, const FDreamUISourceLocation& InOpenLocation)
		{
			for (;;)
			{
				SkipSeparators();
				if (Check(ETokenKind::CloseBrace))
				{
					Advance();
					return;
				}
				if (IsAtEnd())
				{
					// Reported at the '{', not at the end of the file. The brace is where the reader
					// has to go, and "unexpected end of file" on line 200 of a 200 line file is the
					// least useful true statement a parser can make.
					Diagnostics.AddError(EDreamUIDiagnosticCode::UnclosedBlock, InOpenLocation,
						TEXT("this '{' never reaches its '}'"));
					return;
				}

				const int32 IndexBefore = Index;
				ParseNodeStatement(OutNode);
				if (Index == IndexBefore)
				{
					Advance();
				}
			}
		}

		void ParseNodeStatement(FDreamUINode& OutNode)
		{
			if (Check(ETokenKind::Plus))
			{
				ParseComponent(OutNode);
				return;
			}
			if (Check(ETokenKind::At))
			{
				ParseSlotProperty(OutNode);
				return;
			}

			// Contextual keywords: `slot`, `for` and `each` only lead a statement when an identifier
			// follows. That is what lets a property actually called Slot keep working -- the words are
			// reserved as node ids, not as property names, and property names come from reflection
			// where this grammar has no say.
			if (CheckKeyword(TEXT("slot")) && Peek(1).Kind == ETokenKind::Identifier)
			{
				ParseNamedSlot(OutNode);
				return;
			}
			if ((CheckKeyword(TEXT("for")) || CheckKeyword(TEXT("each"))) && Peek(1).Kind == ETokenKind::Identifier)
			{
				ParseLoop(OutNode);
				return;
			}
			if (CheckKeyword(TEXT("class")))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedClassDeclaration, Current().Location,
					TEXT("'class' belongs at the top of the file, not inside a node"));
				RecoverToStatementBoundary();
				return;
			}

			if (LooksLikeProperty())
			{
				FDreamUIProperty Property;
				if (ParseProperty(Property, nullptr))
				{
					OutNode.Properties.Add(MoveTemp(Property));
				}
				return;
			}

			FDreamUINode Child;
			if (ParseNode(Child))
			{
				OutNode.Children.Add(MoveTemp(Child));
			}
		}

		/**
		 * Property or child node, decided by one token of lookahead.
		 *
		 * A dotted path settles it on its own: a node type is an identifier or an asset path and
		 * neither can contain a '.', so `AnchorData.SizeDelta` cannot be a node header no matter what
		 * follows it.
		 */
		bool LooksLikeProperty() const
		{
			if (!Check(ETokenKind::Identifier))
			{
				return false;
			}
			const ETokenKind Next = Peek(1).Kind;
			return Next == ETokenKind::Dot || Next == ETokenKind::Equals || Next == ETokenKind::Arrow;
		}

		void ParseNamedSlot(FDreamUINode& OutNode)
		{
			FDreamUINode Slot;
			Slot.Kind = EDreamUINodeKind::NamedSlot;
			Slot.Location = Current().Location;
			Advance(); // 'slot'

			const FToken& NameToken = Current();
			Slot.Id = NameToken.Text;
			if (IsReservedWord(Slot.Id))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::InvalidNodeId, NameToken.Location,
					FString::Printf(TEXT("'%s' is a keyword and cannot name a slot"), *Slot.Id));
			}
			Advance();

			if (Check(ETokenKind::OpenBrace))
			{
				// A named slot declares a hole; what fills it comes from the host, so a block here
				// would describe contents that the declaration cannot own.
				RaiseUnexpectedToken(FString::Printf(TEXT("slot '%s' declares a hole and takes no block"), *Slot.Id));
				SkipBalancedBlock();
			}

			OutNode.Children.Add(MoveTemp(Slot));
		}

		void ParseLoop(FDreamUINode& OutNode)
		{
			FDreamUINode Loop;
			Loop.Kind = CheckKeyword(TEXT("for")) ? EDreamUINodeKind::ForLoop : EDreamUINodeKind::EachLoop;
			Loop.Location = Current().Location;
			Advance(); // 'for' / 'each'

			const FDreamUISourceLocation VariableLocation = Current().Location;
			Loop.LoopVariable = Current().Text;
			Advance();

			if (!CheckKeyword(TEXT("in")))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedLoopHeader, Loop.Location,
					FString::Printf(TEXT("a loop is written '<keyword> %s in SomeFunction()'"), *Loop.LoopVariable));
				RecoverToStatementBoundary();
				return;
			}
			Advance();

			if (!Check(ETokenKind::Identifier))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedLoopHeader, Loop.Location,
					TEXT("a loop draws its items from a no-argument function, written 'SomeFunction()'"));
				RecoverToStatementBoundary();
				return;
			}
			Loop.LoopSourceFunction = Current().Text;
			Advance();

			// The parentheses are required rather than optional decoration: they are the reminder that
			// the right hand side is a call and not a variable, and the same reminder the binding
			// arrow carries.
			if (!Check(ETokenKind::OpenParen))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedLoopHeader, Loop.Location,
					FString::Printf(TEXT("write the source as '%s()' -- the parentheses are part of it"), *Loop.LoopSourceFunction));
				RecoverToStatementBoundary();
				return;
			}
			Advance();
			if (!Check(ETokenKind::CloseParen))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedLoopHeader, Loop.Location,
					FString::Printf(TEXT("'%s' is called with no arguments"), *Loop.LoopSourceFunction));
				SkipPastCloseParen();
				RecoverToStatementBoundary();
				return;
			}
			Advance();

			// A warning, deliberately, and this is the line to change if that stops being right. Today
			// nothing in the grammar can REFERENCE a loop variable -- there is no interpolation, the
			// body just repeats -- so a shadowed one provably cannot change the tree that gets built,
			// and failing a whole file over a name that has no effect is the wrong trade. The day a
			// value can say something about the item being iterated, this becomes an error.
			const bool bShadows = ActiveLoopVariables.ContainsByPredicate([&Loop](const FString& InName)
			{
				return InName.Equals(Loop.LoopVariable, ESearchCase::CaseSensitive);
			});
			if (bShadows)
			{
				Diagnostics.AddWarning(EDreamUIDiagnosticCode::ShadowedLoopVariable, VariableLocation,
					FString::Printf(TEXT("'%s' is already the variable of an enclosing loop"), *Loop.LoopVariable));
			}

			if (!Check(ETokenKind::OpenBrace))
			{
				RaiseUnexpectedToken(TEXT("a loop needs a '{ ... }' body"));
				RecoverToStatementBoundary();
				OutNode.Children.Add(MoveTemp(Loop));
				return;
			}
			const FDreamUISourceLocation OpenLocation = Current().Location;
			Advance();

			ActiveLoopVariables.Add(Loop.LoopVariable);
			ParseNodeBody(Loop, OpenLocation);
			ActiveLoopVariables.RemoveAt(ActiveLoopVariables.Num() - 1);

			OutNode.Children.Add(MoveTemp(Loop));
		}

		void ParseComponent(FDreamUINode& OutNode)
		{
			FDreamUIComponent Component;
			Component.Location = Current().Location;
			Advance(); // '+'

			if (!Check(ETokenKind::Identifier) && !Check(ETokenKind::AssetPath))
			{
				RaiseUnexpectedToken(TEXT("'+' attaches a behaviour and needs its class name"));
				RecoverToStatementBoundary();
				return;
			}
			Component.ClassName = Current().Text;
			Advance();

			// Whether the name resolves to a concrete UDreamUIBehaviour is UnknownBehaviourClass, and
			// it needs the class registry -- builder's half, same rule as the node type above.

			if (Check(ETokenKind::OpenBrace))
			{
				const FDreamUISourceLocation OpenLocation = Current().Location;
				Advance();
				ParsePropertyOnlyBlock(Component.Properties, OpenLocation, TEXT("a behaviour"));
			}
			else if (!Check(ETokenKind::Separator) && !Check(ETokenKind::CloseBrace) && !IsAtEnd())
			{
				RaiseUnexpectedToken(FString::Printf(TEXT("expected '{' or the end of the line after '+ %s'"), *Component.ClassName));
				RecoverToStatementBoundary();
			}

			OutNode.Components.Add(MoveTemp(Component));
		}

		void ParseSlotProperty(FDreamUINode& OutNode)
		{
			const FDreamUISourceLocation AtLocation = Current().Location;
			Advance(); // '@'

			if (!CheckKeyword(TEXT("slot")))
			{
				RaiseUnexpectedToken(TEXT("'@slot' is the only annotation that leads a line"));
				RecoverToStatementBoundary();
				return;
			}
			Advance();

			FDreamUIProperty Property;
			// Stamped with the '@', not the name after it: the statement starts at the '@', and the
			// patcher replacing this line has to know where the line begins.
			if (ParseProperty(Property, &AtLocation))
			{
				OutNode.SlotProperties.Add(MoveTemp(Property));
			}
		}

		void ParsePropertyOnlyBlock(TArray<FDreamUIProperty>& OutProperties, const FDreamUISourceLocation& InOpenLocation, const TCHAR* InWhatItIs)
		{
			for (;;)
			{
				SkipSeparators();
				if (Check(ETokenKind::CloseBrace))
				{
					Advance();
					return;
				}
				if (IsAtEnd())
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::UnclosedBlock, InOpenLocation,
						TEXT("this '{' never reaches its '}'"));
					return;
				}

				const int32 IndexBefore = Index;
				if (LooksLikeProperty())
				{
					FDreamUIProperty Property;
					if (ParseProperty(Property, nullptr))
					{
						OutProperties.Add(MoveTemp(Property));
					}
				}
				else
				{
					RaiseUnexpectedToken(FString::Printf(TEXT("%s holds only 'Name = Value' lines"), InWhatItIs));
					RecoverToStatementBoundary();
				}

				if (Index == IndexBefore)
				{
					Advance();
				}
			}
		}

		// --- properties and values ----------------------------------------------------------------

		bool ParseProperty(FDreamUIProperty& OutProperty, const FDreamUISourceLocation* InLocationOverride)
		{
			if (!Check(ETokenKind::Identifier))
			{
				RaiseUnexpectedToken(TEXT("expected a property name"));
				RecoverToStatementBoundary();
				return false;
			}

			OutProperty.Location = InLocationOverride != nullptr ? *InLocationOverride : Current().Location;

			// Dotted paths are kept as one flat string, dots and all, exactly as the author wrote it.
			// Splitting it into segments here would decide something the builder has to decide again
			// anyway (which segment is a struct and which is a leaf), and the two answers would have
			// to agree forever.
			FString Name = Current().Text;
			Advance();
			while (Check(ETokenKind::Dot))
			{
				Advance();
				if (!Check(ETokenKind::Identifier))
				{
					RaiseUnexpectedToken(FString::Printf(TEXT("expected a field name after '%s.'"), *Name));
					RecoverToStatementBoundary();
					return false;
				}
				Name += TEXT(".");
				Name += Current().Text;
				Advance();
			}
			OutProperty.Name = MoveTemp(Name);

			if (Check(ETokenKind::Equals))
			{
				Advance();
				if (!ParseValue(OutProperty.Value))
				{
					RecoverToStatementBoundary();
					return false;
				}
				return true;
			}
			if (Check(ETokenKind::Arrow))
			{
				Advance();
				if (!ParseBindingFunction(OutProperty))
				{
					RecoverToStatementBoundary();
					return false;
				}
				return true;
			}

			Diagnostics.AddError(EDreamUIDiagnosticCode::MissingPropertyValue, OutProperty.Location,
				FString::Printf(TEXT("'%s' needs '= Value' or '<- Function()'"), *OutProperty.Name));
			RecoverToStatementBoundary();
			return false;
		}

		bool ParseBindingFunction(FDreamUIProperty& OutProperty)
		{
			if (!Check(ETokenKind::Identifier))
			{
				RaiseUnexpectedToken(TEXT("'<-' binds to a function, written 'SomeFunction()'"));
				return false;
			}
			const FString FunctionName = Current().Text;
			Advance();

			if (!Check(ETokenKind::OpenParen))
			{
				RaiseUnexpectedToken(FString::Printf(TEXT("write the binding as '<- %s()' -- the parentheses are part of it"), *FunctionName));
				return false;
			}
			Advance();
			if (!Check(ETokenKind::CloseParen))
			{
				// Arguments are rejected in the grammar, not left for the builder, because the shape
				// is the problem and not the target: FDreamWidgetPropertyBinding holds one function
				// name and nowhere to put an argument. When the open question about expressions on
				// the right of '<-' is settled, this is the one place that loosens.
				RaiseUnexpectedToken(TEXT("a bound function takes no arguments"));
				SkipPastCloseParen();
				return false;
			}
			Advance();

			OutProperty.BindingFunction = FunctionName;
			return true;
		}

		bool ParseValue(FDreamUIValue& OutValue)
		{
			const FToken& ValueToken = Current();
			OutValue.Location = ValueToken.Location;

			switch (ValueToken.Kind)
			{
			case ETokenKind::Number:
				OutValue.Kind = EDreamUIValueKind::Number;
				OutValue.Raw = ValueToken.Text;
				// The shape the lexer left undecided has landed in a value, so it is a number, and
				// this is the position that gets to say what is wrong with it.
				if (ValueToken.bDigitLeadingWord)
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedNumber, ValueToken.Location,
						FString::Printf(TEXT("'%s' is not a number: write it as -12, 0.95 or 1e-45"), *Ellipsize(ValueToken.Text)));
				}
				Advance();
				break;
			case ETokenKind::Identifier:
				OutValue.Kind = EDreamUIValueKind::Identifier;
				OutValue.Raw = ValueToken.Text;
				Advance();
				break;
			case ETokenKind::String:
				OutValue.Kind = EDreamUIValueKind::String;
				OutValue.Raw = ValueToken.Text;
				Advance();
				break;
			case ETokenKind::HexColor:
				OutValue.Kind = EDreamUIValueKind::HexColor;
				OutValue.Raw = ValueToken.Text;
				Advance();
				break;
			case ETokenKind::AssetPath:
				OutValue.Kind = EDreamUIValueKind::AssetPath;
				OutValue.Raw = ValueToken.Text;
				Advance();
				break;
			case ETokenKind::OpenParen:
				if (!ParseTuple(OutValue))
				{
					return false;
				}
				break;
			default:
				Diagnostics.AddError(EDreamUIDiagnosticCode::MissingPropertyValue, ValueToken.Location,
					FString::Printf(TEXT("expected a value, found '%s'"), *DescribeCurrent()));
				return false;
			}

			ParseOptionalKeyOverride(OutValue);
			return true;
		}

		bool ParseTuple(FDreamUIValue& OutValue)
		{
			const FToken OpenToken = Current();
			OutValue.Kind = EDreamUIValueKind::Tuple;
			OutValue.Location = OpenToken.Location;
			Advance();

			// Elements are cut out of the SOURCE between delimiters rather than rebuilt from tokens.
			// That is what makes Raw honest: the patcher leaves untouched values byte identical, and a
			// tuple reprinted from its parts would renormalise (400,240) into (400, 240) and rewrite a
			// line nobody edited -- one diff hunk per file per save, forever.
			int32 Depth = 1;
			int32 ElementStart = OpenToken.End;
			int32 CloseEnd = OpenToken.End;
			bool bClosed = false;

			for (;;)
			{
				const FToken& Token = Current();

				if (Token.Kind == ETokenKind::EndOfFile || Token.Kind == ETokenKind::CloseBrace)
				{
					// A '}' ends the hunt as surely as the end of the file does. Running past it in
					// search of a ')' would consume the rest of the enclosing node and report the
					// damage somewhere it did not happen.
					Diagnostics.AddError(EDreamUIDiagnosticCode::UnclosedTuple, OpenToken.Location,
						TEXT("this '(' never reaches its ')'"));
					CloseEnd = Token.Start;
					break;
				}
				if (Token.Kind == ETokenKind::OpenParen)
				{
					++Depth;
					Advance();
					continue;
				}
				if (Token.Kind == ETokenKind::CloseParen)
				{
					--Depth;
					if (Depth == 0)
					{
						OutValue.Elements.Add(SliceTrimmed(ElementStart, Token.Start));
						CloseEnd = Token.End;
						bClosed = true;
						Advance();
						break;
					}
					Advance();
					continue;
				}
				if (Token.Kind == ETokenKind::Comma && Depth == 1)
				{
					OutValue.Elements.Add(SliceTrimmed(ElementStart, Token.Start));
					ElementStart = Token.End;
					Advance();
					continue;
				}
				// The only place this loop looks at a token's content, because elements are raw slices
				// and nothing else here needs to. Without it a deferred number would reach the builder
				// unreported and fail there as a conversion error with no column of its own.
				if (Token.Kind == ETokenKind::Number && Token.bDigitLeadingWord)
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedNumber, Token.Location,
						FString::Printf(TEXT("'%s' is not a number: write it as -12, 0.95 or 1e-45"), *Ellipsize(Token.Text)));
				}
				// Separators included: a tuple may be broken across lines, and inside the parentheses
				// a newline is whitespace rather than the end of a statement.
				Advance();
			}

			if (OutValue.Elements.Num() == 1 && OutValue.Elements[0].IsEmpty())
			{
				OutValue.Elements.Reset(); // '()'
			}
			else if (OutValue.Elements.Num() >= 2 && OutValue.Elements.Last().IsEmpty())
			{
				// A trailing comma is tolerated for the same reason a stray ';' is: it is a habit from
				// every other language, and letting it through as a phantom element would surface far
				// downstream as a tuple arity complaint that names a count the author never wrote.
				OutValue.Elements.RemoveAt(OutValue.Elements.Num() - 1);
			}

			OutValue.Raw = Slice(OpenToken.Start, CloseEnd);
			return bClosed;
		}

		FString SliceTrimmed(int32 InStart, int32 InEnd) const
		{
			return Slice(InStart, InEnd).TrimStartAndEnd();
		}

		void ParseOptionalKeyOverride(FDreamUIValue& OutValue)
		{
			if (!Check(ETokenKind::At) || Peek(1).Kind != ETokenKind::Identifier)
			{
				return;
			}

			const FDreamUISourceLocation AtLocation = Current().Location;
			if (!Peek(1).Text.Equals(TEXT("key"), ESearchCase::CaseSensitive))
			{
				// Left unconsumed: the statement's own recovery decides what to do with it, and this
				// might well be the start of the next line's '@slot' after a value the author forgot
				// to terminate.
				Diagnostics.AddError(EDreamUIDiagnosticCode::UnexpectedToken, AtLocation,
					FString::Printf(TEXT("'@key(\"...\")' is the only annotation a value takes, found '@%s'"), *Peek(1).Text));
				RecoverToStatementBoundary();
				return;
			}

			Advance(); // '@'
			Advance(); // 'key'

			if (!Check(ETokenKind::OpenParen))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedKeyOverride, AtLocation,
					TEXT("a key override is written '@key(\"Some.Key\")'"));
				RecoverToStatementBoundary();
				return;
			}
			Advance();

			if (!Check(ETokenKind::String))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedKeyOverride, AtLocation,
					TEXT("a key override takes one quoted string"));
				SkipPastCloseParen();
				RecoverToStatementBoundary();
				return;
			}
			const FString Key = Current().Text;
			const FDreamUISourceLocation KeyLocation = Current().Location;
			Advance();

			if (!Check(ETokenKind::CloseParen))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedKeyOverride, AtLocation,
					TEXT("a key override takes one quoted string and nothing else"));
				SkipPastCloseParen();
				RecoverToStatementBoundary();
				return;
			}
			Advance();

			if (OutValue.Kind != EDreamUIValueKind::String)
			{
				// The override renames a localization entry, and only a string literal produces one.
				// On a number it would name an entry that never gets created, which is the kind of
				// setting that looks applied and does nothing.
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedKeyOverride, AtLocation,
					TEXT("'@key' only follows a quoted string, which is the only value that gets localized"));
				return;
			}
			if (Key.IsEmpty())
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedKeyOverride, KeyLocation,
					TEXT("this key override is empty"));
				return;
			}

			OutValue.LocalizationKeyOverride = Key;
		}

		// --- whole-file checks --------------------------------------------------------------------

		/**
		 * The two things that need the finished tree and nothing else.
		 *
		 * Run as a pass rather than while parsing because both are order independent: a style may be
		 * declared after the node that wears it, and reporting a duplicate needs to know where the
		 * first one was. Doing it here also means the answers come from ForEachNode -- the same walk
		 * the builder and the patcher use -- so a node kind that walk forgets is a bug all three
		 * share and one of them will surface, rather than a discrepancy between two traversals.
		 */
		void CheckNamesAcrossTheTree(const FDreamUIAst& InAst)
		{
			// TMap keyed by FString compares case insensitively, which is the intended rule: this id
			// becomes an FName member variable on the generated class, and FName would collide these
			// two anyway. Catching it here names both lines; catching it at class generation names
			// neither.
			TMap<FString, FDreamUISourceLocation> FirstSeen;

			InAst.ForEachNode([this, &InAst, &FirstSeen](const FDreamUINode& InNode)
			{
				if (!InNode.Id.IsEmpty())
				{
					if (const FDreamUISourceLocation* First = FirstSeen.Find(InNode.Id))
					{
						// Never uniquified. The id is the node's identity -- guid, member variable,
						// binding key and localization key at once -- so inventing OkBtn_1 for the
						// author would silently repoint whichever of the two their bindings meant.
						Diagnostics.AddError(EDreamUIDiagnosticCode::DuplicateNodeId, InNode.Location,
							FString::Printf(TEXT("'%s' is already the id of the node on line %d"), *InNode.Id, First->Line));
					}
					else
					{
						FirstSeen.Add(InNode.Id, InNode.Location);
					}
				}

				if (!InNode.StyleName.IsEmpty() && InAst.FindStyle(InNode.StyleName) == nullptr)
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::UnknownStyle, InNode.Location,
						FString::Printf(TEXT("'%s' names a style this file does not declare"), *InNode.StyleName));
				}
			});
		}
	};
}

bool FDreamUISourceFile::Parse(const FString& InText, const FString& InSourceName,
	FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamUIText;

	OutAst = FDreamUIAst();
	OutDiagnostics.SourceName = InSourceName;

	// Counted rather than asked afterwards, because the bag is allowed to arrive with other files'
	// problems already in it: the compiler collects a whole project into one message log, and
	// "did THIS file parse" has to keep meaning that.
	const int32 ErrorsBefore = OutDiagnostics.NumErrors();

	TArray<FToken> Tokens;
	Tokens.Reserve(InText.Len() / 4 + 8);
	FLexer Lexer(InText, OutDiagnostics);
	Lexer.Run(Tokens);

	FParser Parser(InText, Tokens, OutDiagnostics);
	Parser.ParseFile(OutAst);

	return OutDiagnostics.NumErrors() == ErrorsBefore;
}
