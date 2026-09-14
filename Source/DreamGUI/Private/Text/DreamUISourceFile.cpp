// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceFile.h"
#include "Text/DreamUIPaths.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
		/**
		 * `/` between two node ids, as `Row/Title` in a timeline track line.
		 *
		 * Emitted only when the '/' ABUTS a preceding identifier, which is a position no asset path
		 * can occupy: a path always begins a value or a node type, so it always follows `=`, `,`,
		 * `{`, a separator, or whitespace. Without the abutment rule `Row/Title` lexed as the
		 * identifier `Row` followed by the asset path `/Title`, and the timeline grammar had no way
		 * to say "the node inside the node" at all.
		 */
		Slash,
		/** `->` -- routes an event to a handler, the way `<-` (Arrow) drives a value from a function. */
		EventArrow,
		/** `<-`, the binding arrow. */
		Arrow,
		/** `<->`, the two-way binding arrow: property and variable mirror each other. */
		TwoWayArrow,
		Plus,
		At,
		/**
		 * Expression operators. They exist for the right side of `<-` and are inert everywhere
		 * else -- a property line that meets one reports UnexpectedToken exactly as it would for
		 * any token it has no rule for. Division is deliberately absent: '/' belongs to comments
		 * and asset paths, and an expression that needs it writes a function.
		 */
		Bang,
		Star,
		Percent,
		Minus,
		Less,
		LessEqual,
		Greater,
		GreaterEqual,
		EqualEqual,
		BangEqual,
		AmpAmp,
		PipePipe,
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
		static const TCHAR* Reserved[] = { TEXT("class"), TEXT("style"), TEXT("resources"), TEXT("slot"), TEXT("for"), TEXT("each"), TEXT("in"), TEXT("was"), TEXT("use"), TEXT("timeline"), TEXT("external"), TEXT("ease") };
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
					// ... and after the comments, the third reading: a '/' that ABUTS the identifier
					// before it separates two node ids (`Row/Title`), which is how a timeline track
					// line names a widget inside a widget. An asset path can never sit here -- it
					// always begins a value or a node type, so something non-identifier always
					// precedes it, and `class /Game/UI/X` has the space that keeps it a path.
					// Without this the parser saw `Row` and `/Title.RenderOpacity` and had no way to
					// know they were one path.
					if (OutTokens.Num() > 0
						&& OutTokens.Last().Kind == ETokenKind::Identifier
						&& OutTokens.Last().End == Offset)
					{
						EmitPunctuation(OutTokens, ETokenKind::Slash, 1);
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
				if (Char == TEXT('-') && PeekChar(1) == TEXT('>'))
				{
					// Before the number branch on purpose: '-' otherwise always starts a number, and
					// `->` would lex as a malformed negative.
					EmitPunctuation(OutTokens, ETokenKind::EventArrow, 2);
					continue;
				}
				if (Char == TEXT('-'))
				{
					// A '-' after an OPERAND is subtraction; anywhere else it is a number's sign --
					// including a malformed one, so a lone minus where a value belongs keeps
					// reporting as the malformed number it always was. `(400, -240)` and `X = -5`
					// lex exactly as before (previous token is a comma or an equals), while
					// `A - 5` and `Count() - Base()` become the operator.
					const bool bPreviousIsOperand = OutTokens.Num() > 0
						&& (OutTokens.Last().Kind == ETokenKind::Identifier
							|| OutTokens.Last().Kind == ETokenKind::Number
							|| OutTokens.Last().Kind == ETokenKind::String
							|| OutTokens.Last().Kind == ETokenKind::HexColor
							|| OutTokens.Last().Kind == ETokenKind::CloseParen);
					// ... and a '-' with a WORD or a '(' after it is NEGATION, which is the third
					// reading this rule had no room for. Without it `<- -Count()` went to LexNumber,
					// whose trailing sweep swallowed the identifier and reported the whole thing as a
					// malformed number -- so ParseUnaryExpression's '-' branch could never be reached
					// by any operand that is not a literal.
					//
					// Narrow on purpose. A digit, a '.', a line ending or anything else still goes to
					// LexNumber exactly as before, so `A = -` keeps reporting MalformedNumber rather
					// than turning into a grammatical complaint about a token the author never
					// thought of as one.
					const TCHAR AfterMinus = PeekChar(1);
					const bool bNegationFollows = IsIdentifierStart(AfterMinus) || AfterMinus == TEXT('(');
					if (bPreviousIsOperand || bNegationFollows)
					{
						EmitPunctuation(OutTokens, ETokenKind::Minus, 1);
					}
					else
					{
						LexNumber(OutTokens);
					}
					continue;
				}
				if (IsDigit(Char))
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
				if (Char == TEXT('<'))
				{
					// '<' immediately followed by '-' is the binding arrow, which means a less-than
					// against a negative number needs the space: `a < -1`. Written `a <-1` it reads
					// as an arrow to this lexer exactly as it does to a squinting human. Three
					// characters make the two-way arrow.
					if (PeekChar(1) == TEXT('-') && PeekChar(2) == TEXT('>'))
					{
						EmitPunctuation(OutTokens, ETokenKind::TwoWayArrow, 3);
					}
					else if (PeekChar(1) == TEXT('-'))
					{
						EmitPunctuation(OutTokens, ETokenKind::Arrow, 2);
					}
					else if (PeekChar(1) == TEXT('='))
					{
						EmitPunctuation(OutTokens, ETokenKind::LessEqual, 2);
					}
					else
					{
						EmitPunctuation(OutTokens, ETokenKind::Less, 1);
					}
					continue;
				}
				if (Char == TEXT('>'))
				{
					EmitPunctuation(OutTokens, PeekChar(1) == TEXT('=') ? ETokenKind::GreaterEqual : ETokenKind::Greater, PeekChar(1) == TEXT('=') ? 2 : 1);
					continue;
				}
				if (Char == TEXT('!'))
				{
					EmitPunctuation(OutTokens, PeekChar(1) == TEXT('=') ? ETokenKind::BangEqual : ETokenKind::Bang, PeekChar(1) == TEXT('=') ? 2 : 1);
					continue;
				}
				if (Char == TEXT('=') && PeekChar(1) == TEXT('='))
				{
					EmitPunctuation(OutTokens, ETokenKind::EqualEqual, 2);
					continue;
				}
				if (Char == TEXT('&') && PeekChar(1) == TEXT('&'))
				{
					EmitPunctuation(OutTokens, ETokenKind::AmpAmp, 2);
					continue;
				}
				if (Char == TEXT('|') && PeekChar(1) == TEXT('|'))
				{
					EmitPunctuation(OutTokens, ETokenKind::PipePipe, 2);
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
				case TEXT('*'): EmitPunctuation(OutTokens, ETokenKind::Star, 1); continue;
				case TEXT('%'): EmitPunctuation(OutTokens, ETokenKind::Percent, 1); continue;
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
			const FDreamUISourceLocation Location = MakeLocation(Start);
			while (Offset < Length && IsIdentifierChar(Chars[Offset]))
			{
				++Offset;
			}

			// The one length rule this grammar has, and it is not taste. Every word a .dui writes
			// down becomes an FName somewhere downstream -- a node id becomes a member variable, a
			// property name a lookup key, a handler a function name -- and FName does not REFUSE a
			// string of NAME_SIZE characters or more, it calls checkf(false) and takes the editor
			// with it (UnrealNames.cpp, FindOrStoreString). Enforced here rather than at each of the
			// dozen FName() conversions downstream, because one rule at the token covers every
			// position a word can appear in.
			//
			// The token is emitted TRUNCATED rather than withheld: the parser has to find a name
			// where a name belongs in order to go on reporting the rest of the file, and the
			// shortened one is safe for anything that reaches an FName. The file already carries an
			// error, so nothing is ever built from the shortened spelling.
			FString Word = Slice(Start, Offset);
			if (Word.Len() >= NAME_SIZE)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::IdentifierTooLong, Location,
					FString::Printf(TEXT("'%s' is %d characters long, and a name here holds at most %d"),
						*Ellipsize(Word), Word.Len(), NAME_SIZE - 1));
				Word.LeftInline(NAME_SIZE - 1);
			}
			Emit(OutTokens, ETokenKind::Identifier, Start, MoveTemp(Word));
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

	bool ParseWithImports(const FString& InText, const FString& InSourceName,
		FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics,
		const TFunction<bool(const FString&, FString&, FString&)>& InImportReader, TSet<FString> InAncestors,
		bool bInAllowRootless = false);

	class FParser
	{
	public:
		FParser(const FString& InText, const TArray<FToken>& InTokens, FDreamUIDiagnosticBag& InDiagnostics)
			: Text(InText)
			, Tokens(InTokens)
			, Diagnostics(InDiagnostics)
		{
		}

		/** Null when the caller offered no way to read files; a `use` then reports ImportFailed. */
		const TFunction<bool(const FString&, FString&, FString&)>* ImportReader = nullptr;
		/** This branch's ancestor chain, for the cycle guard. Normalized, lowercased paths. */
		TSet<FString> ImportAncestors;
		/** True for a file reached through `use`: a style library legitimately has no root node. */
		bool bAllowRootless = false;

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
				else if (CheckKeyword(TEXT("use")))
				{
					ParseUseDeclaration(OutAst);
				}
				else if (CheckKeyword(TEXT("resources")))
				{
					ParseResourcesDeclaration(OutAst);
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
				else if (CheckKeyword(TEXT("timeline")))
				{
					// File scope, level with `style`, and that is the proposal's ruling ⑤: a track
					// line's path already reaches any node in the tree, so nesting a timeline under a
					// node would give one animation two ways to name its target and no rule for which
					// wins.
					ParseTimelineDeclaration(OutAst);
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

			// A file compiled AS A CLASS needs a hierarchy; one pulled in through `use` is allowed
			// to be nothing but styles and resources -- that is what a library IS.
			if (!OutAst.bHasRoot && !bAllowRootless)
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

		/**
		 * How many nested blocks / parenthesised sub-expressions the cursor is inside.
		 *
		 * A recursive-descent parser's answer to a file that nests a thousand deep is a stack
		 * overflow, and that is not a diagnostic -- it is the editor vanishing with the author's
		 * unsaved work and nothing written down about which file did it. One counter rather than one
		 * per production, because the stack is one stack: nodes inside expressions inside nodes all
		 * spend the same budget. See DreamUIAst::MaxNestingDepth for why 256.
		 */
		int32 NestingDepth = 0;

		/** True (and reported once) when the cursor is already as deep as the parser will descend. */
		bool IsTooDeep(const FDreamUISourceLocation& InLocation)
		{
			if (NestingDepth < DreamUIAst::MaxNestingDepth)
			{
				return false;
			}
			if (!bReportedNestingLimit)
			{
				// Once per file. A file that reaches the limit reaches it again on the way out of
				// every level, and several hundred copies of one message is not a report.
				bReportedNestingLimit = true;
				Diagnostics.AddError(EDreamUIDiagnosticCode::NestingTooDeep, InLocation,
					FString::Printf(TEXT("this nests more than %d levels deep; nothing below here was read"),
						DreamUIAst::MaxNestingDepth));
			}
			return true;
		}

		bool bReportedNestingLimit = false;

		/** Raises NestingDepth for the lifetime of one descent. */
		struct FNestingScope
		{
			explicit FNestingScope(int32& InOutDepth) : Depth(InOutDepth) { ++Depth; }
			~FNestingScope() { --Depth; }
			int32& Depth;
		};

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

		/**
		 * Walk to the '}' that closes the block the cursor is already INSIDE, and step over it.
		 *
		 * SkipBalancedBlock's twin, for the one caller that has already consumed the opening brace:
		 * the depth guard refuses a block after ParseNode/ParseComponent stepped past its '{', and
		 * leaving the body unconsumed would hand the enclosing loop a wall of statements it would
		 * report one by one.
		 */
		void SkipBalancedBlockBody()
		{
			int32 Depth = 1;
			while (Depth > 0 && !IsAtEnd())
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

		/**
		 * `use "Styles/Common.dui"` -- pull another file's styles and resources into this one's
		 * lookup, shadowed by local declarations. Paths resolve exactly like SourceFile paths
		 * (DUI-root-relative, Plugin.X:-qualified, or absolute); the imported file's OWN imports
		 * ride along, so a style library can layer. An import that fails parses nothing into this
		 * file and says so once, here, at the line that asked for it -- its internal errors are its
		 * own to show when IT is compiled.
		 */
		void ParseUseDeclaration(FDreamUIAst& OutAst)
		{
			const FDreamUISourceLocation UseLocation = Current().Location;
			Advance(); // 'use'

			if (!Check(ETokenKind::String))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::ImportFailed, UseLocation,
					TEXT("'use' takes a quoted path, as in 'use \"Styles/Common.dui\"'"));
				RecoverToStatementBoundary();
				return;
			}
			const FString Spelling = Current().Text;
			Advance();

			if (ImportReader == nullptr || !(*ImportReader))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::ImportFailed, UseLocation,
					FString::Printf(TEXT("'%s' cannot be imported here: this parse was given no way to read files"), *Spelling));
				return;
			}
			FString Resolved;
			FString ImportedText;
			if (!(*ImportReader)(Spelling, Resolved, ImportedText))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::ImportFailed, UseLocation,
					FString::Printf(TEXT("'%s' resolves to no readable file under any DUI root"), *Spelling));
				return;
			}
			FString Normalized = Resolved;
			FPaths::NormalizeFilename(Normalized);
			Normalized = Normalized.ToLower();
			if (ImportAncestors.Contains(Normalized))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::ImportFailed, UseLocation,
					FString::Printf(TEXT("'%s' is already being imported further up this chain -- the imports form a cycle"), *Spelling));
				return;
			}
			// A file THIS one has already pulled in -- directly, or through something else it uses --
			// contributes nothing a second time, so it is not parsed a second time either. A diamond
			// (A uses B and C, both of which use D) otherwise merged D's styles and resources into A
			// twice and paid a full recursive parse of D for the privilege; deeper graphs multiply,
			// which is how a handful of layered style libraries turn one write-back flush into
			// exponential work. Silent rather than diagnosed, because writing `use` twice for a
			// library you reach two ways is not a mistake -- first declaration wins in FindStyle
			// either way, so the duplicates never changed an answer, only the cost of finding it.
			bool bAlreadyMerged = false;
			for (const FString& Already : OutAst.Imports)
			{
				FString AlreadyNormalized = Already;
				FPaths::NormalizeFilename(AlreadyNormalized);
				if (AlreadyNormalized.ToLower() == Normalized)
				{
					bAlreadyMerged = true;
					break;
				}
			}
			if (bAlreadyMerged)
			{
				return;
			}

			FDreamUIAst Imported;
			FDreamUIDiagnosticBag ImportedDiagnostics;
			TSet<FString> BranchAncestors = ImportAncestors;
			BranchAncestors.Add(Normalized);
			if (!ParseWithImports(ImportedText, Resolved, Imported, ImportedDiagnostics, *ImportReader, MoveTemp(BranchAncestors), /*bInAllowRootless*/true))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::ImportFailed, UseLocation,
					FString::Printf(TEXT("'%s' failed to parse (%d error(s)); compile it directly to see them"),
						*Spelling, ImportedDiagnostics.NumErrors()));
				return;
			}

			// Whether a FILE has already put its declarations into this AST, asked of the resolved
			// path each declaration was read from (FDreamUIStyle::SourceName). This is what closes
			// the diamond properly: skipping the second `use` of the same spelling (above) only
			// catches the shallow case, while A-uses-B-and-C with both using D arrives here as C's
			// TRANSITIVE list carrying D's styles a second time. Duplicates never changed an answer
			// -- FindStyle takes the first -- but they grow both arrays, and every lookup through
			// them, with the shape of the import graph rather than with the number of styles.
			auto AlreadyMerged = [&OutAst](const FString& InPath) -> bool
			{
				if (InPath.IsEmpty())
				{
					return false;
				}
				FString Key = InPath;
				FPaths::NormalizeFilename(Key);
				Key.ToLowerInline();
				for (const FString& Existing : OutAst.Imports)
				{
					FString ExistingKey = Existing;
					FPaths::NormalizeFilename(ExistingKey);
					ExistingKey.ToLowerInline();
					if (ExistingKey == Key)
					{
						return true;
					}
				}
				return false;
			};

			// The imported file's OWN declarations go in whole: this is the first time it has been
			// merged, or the early-out above would have fired.
			OutAst.ImportedStyles.Append(MoveTemp(Imported.Styles));
			OutAst.ImportedResources.Append(MoveTemp(Imported.Resources));
			for (FDreamUIStyle& Style : Imported.ImportedStyles)
			{
				if (!AlreadyMerged(Style.SourceName))
				{
					OutAst.ImportedStyles.Add(MoveTemp(Style));
				}
			}
			for (FDreamUIResource& Resource : Imported.ImportedResources)
			{
				if (!AlreadyMerged(Resource.SourceName))
				{
					OutAst.ImportedResources.Add(MoveTemp(Resource));
				}
			}
			OutAst.Imports.Add(Resolved);
			for (FString& Import : Imported.Imports)
			{
				// Read against the list as it grows, so a duplicate inside this one list is caught
				// too. The watcher's dependency table eats this array; naming a file twice there
				// would recompile the importer once per path the graph reaches it by.
				if (!AlreadyMerged(Import))
				{
					OutAst.Imports.Add(MoveTemp(Import));
				}
			}
		}

		/**
		 * `resources { Color Accent = #FF6600 ... }` -- named constants, referenced as `@Accent`.
		 *
		 * Entries accumulate across blocks (a second `resources` block is fine; a second ACCENT is
		 * not), and first declaration wins on a duplicate for the same reason FindStyle keeps the
		 * first style: everybody downstream must agree about what a name means.
		 */
		void ParseResourcesDeclaration(FDreamUIAst& OutAst)
		{
			Advance(); // 'resources'

			if (!Check(ETokenKind::OpenBrace))
			{
				RaiseUnexpectedToken(TEXT("'resources' needs a '{ ... }' block"));
				RecoverToStatementBoundary();
				return;
			}
			Advance();

			while (!IsAtEnd() && !Check(ETokenKind::CloseBrace))
			{
				// A stray ';' is tolerated here the way it is everywhere else in the grammar.
				if (Check(ETokenKind::Separator))
				{
					Advance();
					continue;
				}
				if (!Check(ETokenKind::Identifier))
				{
					RaiseUnexpectedToken(TEXT("expected 'Type Name = Value' in a resources block"));
					RecoverToStatementBoundary();
					continue;
				}

				FDreamUIResource Resource;
				Resource.Location = Current().Location;
				// The file this entry is written in, which a `use` will carry into somebody else's
				// AST -- see FDreamUIStyle::SourceName. Taken from the bag rather than passed in
				// because the bag is already stamped with this parse's file name.
				Resource.SourceName = Diagnostics.SourceName;
				Resource.TypeName = Current().Text;
				Advance();

				if (!Check(ETokenKind::Identifier))
				{
					RaiseUnexpectedToken(FString::Printf(
						TEXT("expected a resource name after '%s'"), *Resource.TypeName));
					RecoverToStatementBoundary();
					continue;
				}
				Resource.Name = Current().Text;
				Advance();

				if (!Check(ETokenKind::Equals))
				{
					RaiseUnexpectedToken(FString::Printf(
						TEXT("resource '%s' needs '= Value'"), *Resource.Name));
					RecoverToStatementBoundary();
					continue;
				}
				Advance();

				if (!ParseValue(Resource.Value))
				{
					RecoverToStatementBoundary();
					continue;
				}

				// Own entries only, exactly like the style declaration below it, and for the same
				// reason: FindResource CHAINS into ImportedResources, so asking it here made a local
				// entry that shadows an imported one report as a duplicate -- and only when the `use`
				// line happened to come first in the file, because that is when the import had
				// already been merged. Shadowing an imported name is the language working (own
				// declarations win, see FDreamUIAst::FindResource), not a mistake to refuse.
				const bool bAlreadyDeclaredLocally = OutAst.Resources.ContainsByPredicate(
					[&Resource](const FDreamUIResource& InExisting) { return InExisting.Name == Resource.Name; });
				if (bAlreadyDeclaredLocally)
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::DuplicateResource, Resource.Location,
						FString::Printf(TEXT("resource '%s' is declared twice"), *Resource.Name));
					continue;
				}
				OutAst.Resources.Add(MoveTemp(Resource));
			}

			if (Check(ETokenKind::CloseBrace))
			{
				Advance();
			}
			else
			{
				RaiseUnexpectedToken(TEXT("a resources block is missing its '}'"));
			}
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
			Style.SourceName = Diagnostics.SourceName;
			Style.Name = Current().Text;
			Advance();

			if (Check(ETokenKind::Colon))
			{
				// `style Danger : Button` -- the same ':' a node uses to wear a style, because it is
				// the same relationship: properties from over there, then mine on top.
				Advance();
				if (!Check(ETokenKind::Identifier))
				{
					RaiseUnexpectedToken(FString::Printf(TEXT("expected a base style name after 'style %s :'"), *Style.Name));
					RecoverToStatementBoundary();
					return;
				}
				Style.BaseName = Current().Text;
				Advance();
			}

			if (!Check(ETokenKind::OpenBrace))
			{
				RaiseUnexpectedToken(FString::Printf(TEXT("style '%s' needs a '{ ... }' block"), *Style.Name));
				RecoverToStatementBoundary();
				return;
			}
			const FDreamUISourceLocation OpenLocation = Current().Location;
			Advance();
			ParsePropertyOnlyBlock(Style.Properties, OpenLocation, TEXT("a style"));

			// Own styles only, not the import chain: a local name matching an imported one is the
			// SHADOWING rule working, not a duplicate -- FindStyle looks locally first, so declaring
			// it here is exactly how a file overrides a library.
			const bool bAlreadyDeclaredLocally = OutAst.Styles.ContainsByPredicate(
				[&Style](const FDreamUIStyle& InExisting) { return InExisting.Name == Style.Name; });
			if (bAlreadyDeclaredLocally)
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

		// --- timelines ----------------------------------------------------------------------------

		/** A time in seconds: `0.3`, or `-0` nonsense refused. Shared by key lines and `@` lines. */
		bool ParseTimelineTime(double& OutTime, const TCHAR* InWhatItIs)
		{
			if (!Check(ETokenKind::Number))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
					FString::Printf(TEXT("%s takes a time in seconds, found '%s'"), InWhatItIs, *DescribeCurrent()));
				return false;
			}
			OutTime = FCString::Atod(*Current().Text);
			if (OutTime < 0.0)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
					FString::Printf(TEXT("a timeline starts at 0, so '%s' is not a time on it"), *Current().Text));
				Advance();
				return false;
			}
			Advance();
			return true;
		}

		/**
		 * `@0.3 -> Landed` -- one key on the block's event track.
		 *
		 * The proposal's ruling ③, and shaped like the event BINDING it borrows its arrow from. What
		 * travels is a NAME, not a function reference, because that is what the runtime track already
		 * broadcasts (UDreamUIAnimEventSection holds an FMovieSceneStringChannel and fires it through
		 * the component's OnAnimationEvent): anything may listen, including a widget the file has
		 * never heard of, which is exactly why the track is unbound.
		 */
		bool ParseTimelineEventLine(FDreamUITimeline& OutTimeline)
		{
			const FDreamUISourceLocation AtLocation = Current().Location;
			Advance(); // '@'

			double Time = 0.0;
			if (!ParseTimelineTime(Time, TEXT("'@' on a timeline line")))
			{
				RecoverToStatementBoundary();
				return false;
			}
			if (!Check(ETokenKind::EventArrow))
			{
				RaiseUnexpectedToken(TEXT("a timeline event key is written '@<time> -> EventName'"));
				RecoverToStatementBoundary();
				return false;
			}
			Advance();
			if (!Check(ETokenKind::Identifier))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
					FString::Printf(TEXT("'->' on a timeline line needs an event name, found '%s'"), *DescribeCurrent()));
				RecoverToStatementBoundary();
				return false;
			}

			// One event TRACK per block, however many `@` lines there are: the runtime track is a
			// single unbound row of named keys, so a track per line would be N rows each holding one
			// key and firing in an order nothing pins down.
			FDreamUITimelineTrack* EventTrack = OutTimeline.Tracks.FindByPredicate(
				[](const FDreamUITimelineTrack& InTrack) { return InTrack.bIsEvent; });
			if (EventTrack == nullptr)
			{
				EventTrack = &OutTimeline.Tracks.AddDefaulted_GetRef();
				EventTrack->bIsEvent = true;
				EventTrack->Location = AtLocation;
			}

			FDreamUITimelineKey& Key = EventTrack->Keys.AddDefaulted_GetRef();
			Key.Time = Time;
			Key.EventName = Current().Text;
			Key.Location = AtLocation;
			Advance();
			return true;
		}

		/** `Row/Title.RenderOpacity : 0.0 = 0, 0.2 = 1 ease OutCubic` */
		bool ParseTimelineTrackLine(FDreamUITimeline& OutTimeline)
		{
			FDreamUITimelineTrack Track;
			Track.Location = Current().Location;

			// The dotted-and-slashed left side, read as one run. A node id and a property name are
			// the same shape of word, so the split is positional and stated in two rules: a '/' only
			// ever extends the node path, and the first '.' ends it -- everything after is the
			// property. A run with no '.' at all is the property, on the animation's own host.
			TArray<FString> PathSegments;
			TArray<FString> PropertySegments;
			bool bInProperty = false;
			for (;;)
			{
				if (!Check(ETokenKind::Identifier))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
						FString::Printf(TEXT("a timeline track line starts with '<node path>.<Property>', found '%s'"), *DescribeCurrent()));
					RecoverToStatementBoundary();
					return false;
				}
				(bInProperty ? PropertySegments : PathSegments).Add(Current().Text);
				Advance();

				if (Check(ETokenKind::Slash) && !bInProperty)
				{
					Advance();
					continue;
				}
				if (Check(ETokenKind::Dot))
				{
					Advance();
					bInProperty = true;
					continue;
				}
				break;
			}
			if (PropertySegments.Num() == 0)
			{
				if (PathSegments.Num() == 1)
				{
					// ONE BARE WORD IS THE PROPERTY, on the widget that hosts the animation -- the
					// same empty path FDreamWidgetAnimationObjectReference records for the context
					// widget, and what `RenderScale : 0 = (1, 1, 1)` has to mean for the commonest
					// timeline there is (one that animates the thing it is written on).
					//
					// Nothing lexical tells a node id from a property name, so the rule is positional
					// and it is this way round because only one of the two readings is ever useful: a
					// path with no property is not a track at all, while a property with no path is
					// most of them.
					PropertySegments = MoveTemp(PathSegments);
					PathSegments.Reset();
				}
				else
				{
					// `Row/Icon : …`. A '/' can only have been written to reach INTO the tree, so
					// this one really is a line that stopped before its property -- reported here
					// rather than left to the builder, because the file alone is enough to see it.
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Track.Location,
						FString::Printf(TEXT("'%s' names a node and no property -- a track line is '<node path>.<Property> : …'"),
							*FString::Join(PathSegments, TEXT("/"))));
					RecoverToStatementBoundary();
					return false;
				}
			}
			// The node path may be empty, and legitimately -- see above. When it is not, the split is
			// the one genuinely ambiguous spelling in this grammar and it reads the way the rest of
			// the language reads dots: the FIRST run is the node path (only a '/' extends it) and
			// everything after the first '.' is the property, so `Icon.Brush.TintColor` drives Icon's
			// `Brush.TintColor` rather than `Icon/Brush`'s `TintColor`.
			Track.NodePath = FString::Join(PathSegments, TEXT("/"));
			Track.PropertyName = FString::Join(PropertySegments, TEXT("."));

			if (!Check(ETokenKind::Colon))
			{
				// The target quoted back the way the author wrote it: a bare property has no path to
				// print, and a leading '.' in the example would be a spelling the grammar refuses.
				const FString Target = Track.NodePath.IsEmpty()
					? Track.PropertyName
					: Track.NodePath + TEXT(".") + Track.PropertyName;
				RaiseUnexpectedToken(FString::Printf(
					TEXT("a timeline track line separates its target from its keys with ':', as in '%s : 0 = …'"),
					*Target));
				RecoverToStatementBoundary();
				return false;
			}
			Advance();

			for (;;)
			{
				FDreamUITimelineKey Key;
				Key.Location = Current().Location;
				if (!ParseTimelineTime(Key.Time, TEXT("a timeline key")))
				{
					RecoverToStatementBoundary();
					return false;
				}
				if (!Check(ETokenKind::Equals))
				{
					RaiseUnexpectedToken(TEXT("a timeline key is written '<time> = <value>'"));
					RecoverToStatementBoundary();
					return false;
				}
				Advance();
				if (!ParseValue(Key.Value))
				{
					RecoverToStatementBoundary();
					return false;
				}
				if (CheckKeyword(TEXT("ease")))
				{
					Advance();
					if (!Check(ETokenKind::Identifier))
					{
						Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
							FString::Printf(TEXT("'ease' needs a curve name, found '%s'"), *DescribeCurrent()));
						RecoverToStatementBoundary();
						return false;
					}
					// Checked for MEMBERSHIP by the builder, not here: the word list is the tween
					// library's, which is reflection this stage deliberately does not touch.
					Key.EaseName = Current().Text;
					Advance();
				}
				Track.Keys.Add(MoveTemp(Key));

				if (!Check(ETokenKind::Comma))
				{
					break;
				}
				Advance();
				// A comma at the end of a line continues the track onto the next, which is how a
				// long track stays readable. SkipSeparators, so the newline after it is not a key.
				SkipSeparators();
			}

			OutTimeline.Tracks.Add(MoveTemp(Track));
			return true;
		}

		void ParseTimelineBody(FDreamUITimeline& OutTimeline, const FDreamUISourceLocation& InOpenLocation)
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
						FString::Printf(TEXT("timeline '%s' was opened here and never closed"), *OutTimeline.Name));
					return;
				}

				const int32 IndexBefore = Index;

				if (Check(ETokenKind::At))
				{
					ParseTimelineEventLine(OutTimeline);
				}
				else if (CheckKeyword(TEXT("duration")) && Peek(1).Kind == ETokenKind::Equals)
				{
					Advance();
					Advance();
					ParseTimelineTime(OutTimeline.Duration, TEXT("'duration'"));
				}
				else if (CheckKeyword(TEXT("loop")) && Peek(1).Kind == ETokenKind::Equals)
				{
					Advance();
					Advance();
					if (!Check(ETokenKind::Identifier))
					{
						Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
							FString::Printf(TEXT("'loop' takes Once, Loop or PingPong, found '%s'"), *DescribeCurrent()));
						RecoverToStatementBoundary();
					}
					else
					{
						// The three spellings are checked HERE and not in the builder, because unlike
						// an ease name this set is the language's own and needs no reflection to know.
						const FString Mode = Current().Text;
						if (!Mode.Equals(TEXT("Once")) && !Mode.Equals(TEXT("Loop")) && !Mode.Equals(TEXT("PingPong")))
						{
							Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
								FString::Printf(TEXT("'%s' is not a loop mode; write Once, Loop or PingPong"), *Mode));
						}
						else
						{
							OutTimeline.LoopMode = Mode;
						}
						Advance();
					}
				}
				else
				{
					ParseTimelineTrackLine(OutTimeline);
				}

				if (Index == IndexBefore)
				{
					Advance();
				}
			}
		}

		void ParseTimelineDeclaration(FDreamUIAst& OutAst)
		{
			const FDreamUISourceLocation KeywordLocation = Current().Location;
			Advance(); // 'timeline'

			if (!Check(ETokenKind::Identifier))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
					FString::Printf(TEXT("'timeline' takes a name, found '%s'"), *DescribeCurrent()));
				RecoverToStatementBoundary();
				return;
			}

			FDreamUITimeline Timeline;
			Timeline.Location = KeywordLocation;
			Timeline.SourceName = Diagnostics.SourceName;
			Timeline.Name = Current().Text;
			Advance();

			if (CheckKeyword(TEXT("external")))
			{
				// Layer two: the animation lives in the asset, Sequencer owns it, and the language
				// records only that it exists -- which is what makes "what animations does this class
				// have" answerable from the file. A block after it would be the file claiming to
				// describe contents it has explicitly disclaimed.
				Timeline.bExternal = true;
				Advance();
				if (Check(ETokenKind::OpenBrace))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
						FString::Printf(TEXT("timeline '%s' is external, so its contents live in the asset and it takes no block"), *Timeline.Name));
					SkipBalancedBlock();
				}
			}
			else
			{
				if (!Check(ETokenKind::OpenBrace))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedTimeline, Current().Location,
						FString::Printf(TEXT("timeline '%s' needs a '{ ... }' block, or the word 'external'"), *Timeline.Name));
					RecoverToStatementBoundary();
					return;
				}
				const FDreamUISourceLocation OpenLocation = Current().Location;
				Advance();
				ParseTimelineBody(Timeline, OpenLocation);
			}

			const bool bAlreadyDeclared = OutAst.Timelines.ContainsByPredicate(
				[&Timeline](const FDreamUITimeline& InExisting) { return InExisting.Name == Timeline.Name; });
			if (bAlreadyDeclared)
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::DuplicateTimeline, Timeline.Location,
					FString::Printf(TEXT("timeline '%s' is declared twice"), *Timeline.Name));
				return;
			}
			OutAst.Timelines.Add(MoveTemp(Timeline));
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
			FString TypeName = TypeToken.Text;
			const FDreamUISourceLocation TypeLocation = TypeToken.Location;
			Advance();

			// `Native.Toggle` -- a scoped tag, resolved through the widget registry. The lexer hands
			// it over as three tokens (identifier, dot, identifier) because a dot elsewhere separates
			// property path segments; the tag position is the one place they mean a single name, so
			// they are joined here rather than taught to the lexer. LooksLikeProperty has already
			// ruled out the property reading before ParseNode is entered. One dot only: a second
			// segment has no meaning the registry knows.
			if (Check(ETokenKind::Dot) && Peek(1).Kind == ETokenKind::Identifier)
			{
				Advance();
				TypeName = FString::Printf(TEXT("%s.%s"), *TypeName, *Current().Text);
				OutNode.TypeName = TypeName;
				Advance();
			}

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
			if (IsTooDeep(InOpenLocation))
			{
				SkipBalancedBlockBody();
				return;
			}
			const FNestingScope Scope(NestingDepth);
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
			if (Next == ETokenKind::Dot)
			{
				// A dot no longer settles it: `AnchorData.SizeDelta = ...` is a property, and
				// `Native.Toggle Mute {` is a node whose tag has a scope. Walk the dotted run and let
				// what FOLLOWS it decide -- and only an id or an open brace reads as a node, so that a
				// property missing its '=' (`AnchorData.SizeDelta` alone on a line) still fails as
				// the property it was meant to be, with the diagnostic that says so.
				int32 Ahead = 1;
				while (Peek(Ahead).Kind == ETokenKind::Dot && Peek(Ahead + 1).Kind == ETokenKind::Identifier)
				{
					Ahead += 2;
				}
				const ETokenKind After = Peek(Ahead).Kind;
				return After != ETokenKind::Identifier && After != ETokenKind::OpenBrace;
			}
			return Next == ETokenKind::Equals || Next == ETokenKind::Arrow
				|| Next == ETokenKind::EventArrow || Next == ETokenKind::TwoWayArrow;
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

			// `(was: OldId)`, the same clause a widget node takes, and for a reason that is not
			// symmetry: a slot's id becomes a class member variable exactly like a widget's (it is
			// why two slots colliding is DuplicateNodeId and not a code of its own), so a renamed
			// slot orphaned every graph reference, binding and animation track that named it with no
			// way to say what it used to be called. Migration is the whole point of the clause, and a
			// slot is the one declaration that could not use it.
			//
			// No `: Style` here, and that is not an oversight either: a style is a bag of PROPERTIES
			// and a slot declaration has no properties to give them to -- the grammar refuses it a
			// block for the same reason. Accepting the clause would parse a promise nothing could
			// keep.
			if (Check(ETokenKind::OpenParen))
			{
				ParseWasClause(Slot);
			}

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

			// The parentheses now DISTINGUISH rather than merely remind: `in GetItems()` calls a
			// function, `in Items` reads a variable -- the two source shapes the ruling admits. The
			// variable spelling is what lets a FieldNotify array drive the list's refresh.
			if (Check(ETokenKind::OpenParen))
			{
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
				Loop.bLoopSourceIsFunction = true;
			}
			else
			{
				Loop.bLoopSourceIsFunction = false;
			}

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
			if (IsTooDeep(InOpenLocation))
			{
				SkipBalancedBlockBody();
				return;
			}
			const FNestingScope Scope(NestingDepth);
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
			if (Check(ETokenKind::EventArrow))
			{
				// `OnClicked -> Confirm`. A bare handler name, no parens: `<-` writes `Func()` because
				// the file is DESCRIBING a call it will make; `->` names a function something else
				// will call, and dressing it as a call would promise arguments the author cannot pass.
				Advance();
				if (!Check(ETokenKind::Identifier))
				{
					RaiseUnexpectedToken(FString::Printf(TEXT("expected a handler name after '%s ->'"), *OutProperty.Name));
					RecoverToStatementBoundary();
					return false;
				}
				OutProperty.EventHandler = Current().Text;
				Advance();
				return true;
			}
			if (Check(ETokenKind::TwoWayArrow))
			{
				// `Value <-> Volume`. A bare VARIABLE name: the two sides mirror each other, and a
				// call or an expression has no left-hand side to write back into.
				Advance();
				if (!Check(ETokenKind::Identifier))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
						FString::Printf(TEXT("'%s <->' expects the name of a variable on this class"), *OutProperty.Name));
					RecoverToStatementBoundary();
					return false;
				}
				OutProperty.TwoWayProperty = Current().Text;
				Advance();
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
			// The open question closed: the right of `<-` is an EXPRESSION. A bare `Func()` keeps
			// travelling as the one name FDreamWidgetPropertyBinding holds -- byte-for-byte the old
			// behaviour -- and anything richer is carried as a tree for the compiler to lower into
			// a generated pure function before the builder runs.
			FDreamUIExpression Expression;
			if (!ParseBindingExpression(Expression, /*InMinPrecedence*/1))
			{
				return false;
			}
			if (!Check(ETokenKind::Separator) && !Check(ETokenKind::CloseBrace) && !Check(ETokenKind::EndOfFile))
			{
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
					FString::Printf(TEXT("unexpected '%s' after the binding expression"), *Current().Text));
				RecoverToStatementBoundary();
				return false;
			}
			if (Expression.IsBareCall())
			{
				OutProperty.BindingFunction = Expression.Symbol;
			}
			else
			{
				OutProperty.BindingExpression = MoveTemp(Expression);
			}
			return true;
		}

		/** 0 means "not a binary operator". Higher binds tighter; all binaries are left-associative. */
		static int32 GetBinaryPrecedence(ETokenKind InKind)
		{
			switch (InKind)
			{
			case ETokenKind::PipePipe: return 1;
			case ETokenKind::AmpAmp: return 2;
			case ETokenKind::EqualEqual:
			case ETokenKind::BangEqual: return 3;
			case ETokenKind::Less:
			case ETokenKind::LessEqual:
			case ETokenKind::Greater:
			case ETokenKind::GreaterEqual: return 4;
			case ETokenKind::Plus:
			case ETokenKind::Minus: return 5;
			case ETokenKind::Star:
			case ETokenKind::Percent: return 6;
			default: return 0;
			}
		}

		static const TCHAR* GetOperatorSpelling(ETokenKind InKind)
		{
			switch (InKind)
			{
			case ETokenKind::PipePipe: return TEXT("||");
			case ETokenKind::AmpAmp: return TEXT("&&");
			case ETokenKind::EqualEqual: return TEXT("==");
			case ETokenKind::BangEqual: return TEXT("!=");
			case ETokenKind::Less: return TEXT("<");
			case ETokenKind::LessEqual: return TEXT("<=");
			case ETokenKind::Greater: return TEXT(">");
			case ETokenKind::GreaterEqual: return TEXT(">=");
			case ETokenKind::Plus: return TEXT("+");
			case ETokenKind::Minus: return TEXT("-");
			case ETokenKind::Star: return TEXT("*");
			case ETokenKind::Percent: return TEXT("%");
			case ETokenKind::Bang: return TEXT("!");
			default: return TEXT("?");
			}
		}

		bool ParseBindingExpression(FDreamUIExpression& OutExpression, int32 InMinPrecedence)
		{
			if (IsTooDeep(Current().Location))
			{
				RecoverToStatementBoundary();
				return false;
			}
			const FNestingScope Scope(NestingDepth);
			if (!ParseUnaryExpression(OutExpression))
			{
				return false;
			}
			while (true)
			{
				const int32 Precedence = GetBinaryPrecedence(Current().Kind);
				if (Precedence == 0 || Precedence < InMinPrecedence)
				{
					return true;
				}
				const ETokenKind OperatorKind = Current().Kind;
				const FDreamUISourceLocation OperatorLocation = Current().Location;
				Advance();

				FDreamUIExpression Right;
				// Precedence + 1 makes every level left-associative: `a - b - c` is `(a-b)-c`.
				if (!ParseBindingExpression(Right, Precedence + 1))
				{
					return false;
				}
				FDreamUIExpression Combined;
				Combined.Kind = FDreamUIExpression::EKind::Binary;
				Combined.Symbol = GetOperatorSpelling(OperatorKind);
				Combined.Location = OperatorLocation;
				Combined.Operands.Add(MoveTemp(OutExpression));
				Combined.Operands.Add(MoveTemp(Right));
				OutExpression = MoveTemp(Combined);
			}
		}

		bool ParseUnaryExpression(FDreamUIExpression& OutExpression)
		{
			if (IsTooDeep(Current().Location))
			{
				RecoverToStatementBoundary();
				return false;
			}
			const FNestingScope Scope(NestingDepth);
			if (Check(ETokenKind::Bang) || Check(ETokenKind::Minus))
			{
				FDreamUIExpression Unary;
				Unary.Kind = FDreamUIExpression::EKind::Unary;
				Unary.Symbol = GetOperatorSpelling(Current().Kind);
				Unary.Location = Current().Location;
				Advance();
				FDreamUIExpression& Inner = Unary.Operands.AddDefaulted_GetRef();
				if (!ParseUnaryExpression(Inner))
				{
					return false;
				}
				OutExpression = MoveTemp(Unary);
				return true;
			}
			return ParsePrimaryExpression(OutExpression);
		}

		bool ParsePrimaryExpression(FDreamUIExpression& OutExpression)
		{
			OutExpression.Location = Current().Location;
			switch (Current().Kind)
			{
			case ETokenKind::OpenParen:
			{
				Advance();
				if (!ParseBindingExpression(OutExpression, 1))
				{
					return false;
				}
				if (!Check(ETokenKind::CloseParen))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
						TEXT("this '(' was never closed"));
					RecoverToStatementBoundary();
					return false;
				}
				Advance();
				return true;
			}
			case ETokenKind::Number:
			{
				// The shape the lexer refused to judge on its own has landed where a NUMBER belongs,
				// so it is a number and this is the position that gets to say what is wrong with it
				// -- the same ruling ParseValue and ParseTuple already make. Without it `<- 24px`
				// travelled as a literal, the thunk generator fed "24px" to a Real pin, and
				// FCString::Atof read 24 out of it: a binding that compiled green and drove a value
				// the author never wrote.
				if (Current().bDigitLeadingWord)
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedNumber, Current().Location,
						FString::Printf(TEXT("'%s' is not a number: write it as -12, 0.95 or 1e-45"),
							*Ellipsize(Current().Text)));
				}
				OutExpression.Kind = FDreamUIExpression::EKind::Literal;
				OutExpression.LiteralKind = EDreamUIValueKind::Number;
				OutExpression.LiteralRaw = Current().Text;
				Advance();
				return true;
			}
			case ETokenKind::String:
			{
				OutExpression.Kind = FDreamUIExpression::EKind::Literal;
				OutExpression.LiteralKind = EDreamUIValueKind::String;
				OutExpression.LiteralRaw = Current().Text;
				Advance();
				return true;
			}
			case ETokenKind::At:
			{
				// `@Accent` inside an expression, which is the same word the same `resources` block
				// already defines for an assignment -- no new spelling, just the one that existed
				// being readable in the one place it was not. A named constant that can be assigned
				// but not multiplied is a constant with a rule nobody can remember.
				//
				// Recorded as a LITERAL of kind ResourceRef and resolved where the expression is
				// lowered, never by rewriting the AST: the patcher owns this tree's byte offsets, and
				// substituting a longer literal under it would splice later edits into wrong columns.
				Advance();
				if (!Check(ETokenKind::Identifier))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
						TEXT("expected a resource name after '@'"));
					RecoverToStatementBoundary();
					return false;
				}
				OutExpression.Kind = FDreamUIExpression::EKind::Literal;
				OutExpression.LiteralKind = EDreamUIValueKind::ResourceRef;
				OutExpression.LiteralRaw = Current().Text;
				Advance();
				return true;
			}
			case ETokenKind::Identifier:
			{
				const FString Name = Current().Text;
				Advance();
				if (Name == TEXT("true") || Name == TEXT("false"))
				{
					OutExpression.Kind = FDreamUIExpression::EKind::Literal;
					OutExpression.LiteralKind = EDreamUIValueKind::Identifier;
					OutExpression.LiteralRaw = Name;
					return true;
				}
				if (Check(ETokenKind::OpenParen))
				{
					Advance();
					OutExpression.Kind = FDreamUIExpression::EKind::Call;
					OutExpression.Symbol = Name;
					if (!Check(ETokenKind::CloseParen))
					{
						while (true)
						{
							FDreamUIExpression& Argument = OutExpression.Operands.AddDefaulted_GetRef();
							if (!ParseBindingExpression(Argument, 1))
							{
								return false;
							}
							if (Check(ETokenKind::Comma))
							{
								Advance();
								continue;
							}
							break;
						}
					}
					if (!Check(ETokenKind::CloseParen))
					{
						Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
							FString::Printf(TEXT("the call to '%s' was never closed"), *Name));
						RecoverToStatementBoundary();
						return false;
					}
					Advance();
					return true;
				}
				// A bare identifier is a variable on the user widget -- the day `<-` learned
				// expressions is the day a property could be a source too. Dots extend it into a
				// path: outside a loop body that is an error the thunk generator raises (a graph
				// cannot get a sub-property by name), but inside an `each` it is how a binding says
				// something about the item -- `Entry.Name`.
				OutExpression.Kind = FDreamUIExpression::EKind::VariableRef;
				OutExpression.Symbol = Name;
				while (Check(ETokenKind::Dot))
				{
					Advance();
					if (!Check(ETokenKind::Identifier))
					{
						Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
							FString::Printf(TEXT("expected a member name after '%s.'"), *OutExpression.Symbol));
						RecoverToStatementBoundary();
						return false;
					}
					OutExpression.Symbol += TEXT(".") + Current().Text;
					Advance();
				}
				return true;
			}
			default:
				Diagnostics.AddError(EDreamUIDiagnosticCode::MalformedBindingExpression, Current().Location,
					TEXT("'<-' expects an expression: a function call, a variable, a literal, or an operator combination of them"));
				RecoverToStatementBoundary();
				return false;
			}
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
			case ETokenKind::At:
			{
				// `@Accent` -- a reference into the resources block. The location is the '@', which
				// is where the patcher's MeasureValue starts when a designer edit replaces the whole
				// reference with a literal.
				Advance();
				if (!Check(ETokenKind::Identifier))
				{
					Diagnostics.AddError(EDreamUIDiagnosticCode::MissingPropertyValue, ValueToken.Location,
						TEXT("expected a resource name after '@'"));
					return false;
				}
				OutValue.Kind = EDreamUIValueKind::ResourceRef;
				OutValue.Raw = Current().Text;
				Advance();
				break;
			}
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

namespace DreamUIText
{
	bool ParseWithImports(const FString& InText, const FString& InSourceName,
		FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics,
		const TFunction<bool(const FString&, FString&, FString&)>& InImportReader, TSet<FString> InAncestors,
		bool bInAllowRootless)
	{
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
		Parser.ImportReader = &InImportReader;
		// By value on purpose: the set is this branch's ANCESTOR CHAIN, not a global visited set. A
		// diamond -- two imports both pulling one base file -- parses the base twice and merges twice
		// (first-wins lookup makes the duplicates inert); only a genuine cycle trips the guard.
		Parser.ImportAncestors = MoveTemp(InAncestors);
		Parser.bAllowRootless = bInAllowRootless;
		Parser.ParseFile(OutAst);

		return OutDiagnostics.NumErrors() == ErrorsBefore;
	}
}

bool FDreamUISourceFile::Parse(const FString& InText, const FString& InSourceName,
	FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
{
	return Parse(InText, InSourceName, OutAst, OutDiagnostics, nullptr);
}

bool FDreamUISourceFile::Parse(const FString& InText, const FString& InSourceName,
	FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics,
	TFunction<bool(const FString&, FString&, FString&)> InImportReader)
{
	using namespace DreamUIText;
	TSet<FString> Ancestors;
	FString NormalizedSelf = InSourceName;
	FPaths::NormalizeFilename(NormalizedSelf);
	Ancestors.Add(NormalizedSelf.ToLower());
	return ParseWithImports(InText, InSourceName, OutAst, OutDiagnostics, InImportReader, MoveTemp(Ancestors));
}

TFunction<bool(const FString&, FString&, FString&)> FDreamUISourceFile::MakeFileImportReader()
{
	return [](const FString& InSpelling, FString& OutResolvedPath, FString& OutText)
	{
		OutResolvedPath = DreamUIPaths::Resolve(InSpelling);
		return !OutResolvedPath.IsEmpty() && FFileHelper::LoadFileToString(OutText, *OutResolvedPath);
	};
}
