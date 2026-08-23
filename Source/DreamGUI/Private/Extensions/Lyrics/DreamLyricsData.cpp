// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Lyrics/DreamLyricsData.h"

FString FDreamLyricLine::GetText() const
{
	FString Result;
	for (const FDreamLyricWord& Word : Words)
	{
		Result += Word.Text;
	}
	return Result;
}

int32 FDreamLyrics::FindLineAtTime(float Time) const
{
	int32 Result = INDEX_NONE;
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		if (Lines[i].StartTime <= Time)
		{
			Result = i;
		}
		else
		{
			break;
		}
	}
	return Result;
}

namespace DreamLyricsLocal
{
	/*
	 * A small XML reader that keeps text nodes in document order between elements. The engine's
	 * FXmlFile folds all of a node's text into one string, which loses whether a space sat between
	 * two word spans -- and that space is the difference between "Hello world" and "Helloworld".
	 */
	struct FXmlElement
	{
		FString Tag;
		TMap<FString, FString> Attributes;
		/** Children in order; a child with an empty Tag is a text node (its text in Text). */
		TArray<TSharedPtr<FXmlElement>> Children;
		FString Text;

		bool IsText() const { return Tag.IsEmpty(); }
		FString Attr(const TCHAR* Name) const
		{
			const FString* Found = Attributes.Find(Name);
			return Found ? *Found : FString();
		}
		/** All text below this element, in order. */
		FString InnerText() const
		{
			FString Result;
			for (const auto& Child : Children)
			{
				Result += Child->IsText() ? Child->Text : Child->InnerText();
			}
			return Result;
		}
	};

	static FString DecodeEntities(const FString& In)
	{
		if (!In.Contains(TEXT("&")))return In;
		FString Out;
		Out.Reserve(In.Len());
		for (int32 i = 0; i < In.Len(); i++)
		{
			if (In[i] != TEXT('&'))
			{
				Out.AppendChar(In[i]);
				continue;
			}
			const int32 End = In.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i + 1);
			if (End == INDEX_NONE || End - i > 10)
			{
				Out.AppendChar(In[i]);
				continue;
			}
			const FString Entity = In.Mid(i + 1, End - i - 1);
			if (Entity == TEXT("amp"))Out.AppendChar(TEXT('&'));
			else if (Entity == TEXT("lt"))Out.AppendChar(TEXT('<'));
			else if (Entity == TEXT("gt"))Out.AppendChar(TEXT('>'));
			else if (Entity == TEXT("quot"))Out.AppendChar(TEXT('"'));
			else if (Entity == TEXT("apos"))Out.AppendChar(TEXT('\''));
			else if (Entity.StartsWith(TEXT("#x")))Out.AppendChar((TCHAR)FParse::HexNumber(*Entity.Mid(2)));
			else if (Entity.StartsWith(TEXT("#")))Out.AppendChar((TCHAR)FCString::Atoi(*Entity.Mid(1)));
			else
			{
				Out.AppendChar(TEXT('&'));
				continue;
			}
			i = End;
		}
		return Out;
	}

	struct FXmlReader
	{
		const FString& Source;
		int32 Pos = 0;
		FString Error;

		explicit FXmlReader(const FString& InSource) : Source(InSource) {}

		bool AtEnd() const { return Pos >= Source.Len(); }
		bool StartsWith(const TCHAR* Token) const
		{
			const int32 Len = FCString::Strlen(Token);
			return Pos + Len <= Source.Len() && FCString::Strncmp(*Source + Pos, Token, Len) == 0;
		}
		void SkipWhitespace()
		{
			while (!AtEnd() && FChar::IsWhitespace(Source[Pos]))Pos++;
		}
		/** Skip prolog, comments, doctype and processing instructions before or between elements. */
		bool SkipMisc()
		{
			for (;;)
			{
				SkipWhitespace();
				if (StartsWith(TEXT("<!--")))
				{
					const int32 End = Source.Find(TEXT("-->"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
					if (End == INDEX_NONE) { Error = TEXT("unterminated comment"); return false; }
					Pos = End + 3;
				}
				else if (StartsWith(TEXT("<?")))
				{
					const int32 End = Source.Find(TEXT("?>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
					if (End == INDEX_NONE) { Error = TEXT("unterminated processing instruction"); return false; }
					Pos = End + 2;
				}
				else if (StartsWith(TEXT("<!")))
				{
					const int32 End = Source.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
					if (End == INDEX_NONE) { Error = TEXT("unterminated declaration"); return false; }
					Pos = End + 1;
				}
				else
				{
					return true;
				}
			}
		}
		static bool IsNameChar(TCHAR C)
		{
			return FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-') || C == TEXT(':') || C == TEXT('.');
		}
		FString ReadName()
		{
			const int32 Start = Pos;
			while (!AtEnd() && IsNameChar(Source[Pos]))Pos++;
			return Source.Mid(Start, Pos - Start);
		}
		/** Parse one element starting at '<'. */
		TSharedPtr<FXmlElement> ReadElement()
		{
			if (AtEnd() || Source[Pos] != TEXT('<')) { Error = TEXT("expected '<'"); return nullptr; }
			Pos++;
			TSharedPtr<FXmlElement> Element = MakeShared<FXmlElement>();
			Element->Tag = ReadName();
			if (Element->Tag.IsEmpty()) { Error = TEXT("missing tag name"); return nullptr; }
			// Attributes.
			for (;;)
			{
				SkipWhitespace();
				if (AtEnd()) { Error = TEXT("unterminated start tag"); return nullptr; }
				if (StartsWith(TEXT("/>")))
				{
					Pos += 2;
					return Element;
				}
				if (Source[Pos] == TEXT('>'))
				{
					Pos++;
					break;
				}
				const FString Name = ReadName();
				if (Name.IsEmpty()) { Error = FString::Printf(TEXT("bad attribute in <%s>"), *Element->Tag); return nullptr; }
				SkipWhitespace();
				FString Value;
				if (!AtEnd() && Source[Pos] == TEXT('='))
				{
					Pos++;
					SkipWhitespace();
					if (AtEnd()) { Error = TEXT("unterminated attribute"); return nullptr; }
					const TCHAR Quote = Source[Pos];
					if (Quote == TEXT('"') || Quote == TEXT('\''))
					{
						Pos++;
						const int32 End = Source.Find(FString::Chr(Quote), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
						if (End == INDEX_NONE) { Error = TEXT("unterminated attribute value"); return nullptr; }
						Value = DecodeEntities(Source.Mid(Pos, End - Pos));
						Pos = End + 1;
					}
					else
					{
						const int32 Start = Pos;
						while (!AtEnd() && !FChar::IsWhitespace(Source[Pos]) && Source[Pos] != TEXT('>') && Source[Pos] != TEXT('/'))Pos++;
						Value = DecodeEntities(Source.Mid(Start, Pos - Start));
					}
				}
				Element->Attributes.Add(Name, Value);
			}
			// Content: text and child elements, in order, up to the matching end tag.
			for (;;)
			{
				if (AtEnd()) { Error = FString::Printf(TEXT("missing </%s>"), *Element->Tag); return nullptr; }
				if (Source[Pos] == TEXT('<'))
				{
					if (StartsWith(TEXT("</")))
					{
						Pos += 2;
						const FString EndName = ReadName();
						SkipWhitespace();
						if (AtEnd() || Source[Pos] != TEXT('>')) { Error = TEXT("bad end tag"); return nullptr; }
						Pos++;
						if (EndName != Element->Tag) { Error = FString::Printf(TEXT("</%s> closes <%s>"), *EndName, *Element->Tag); return nullptr; }
						return Element;
					}
					if (StartsWith(TEXT("<![CDATA[")))
					{
						const int32 End = Source.Find(TEXT("]]>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
						if (End == INDEX_NONE) { Error = TEXT("unterminated CDATA"); return nullptr; }
						TSharedPtr<FXmlElement> TextNode = MakeShared<FXmlElement>();
						TextNode->Text = Source.Mid(Pos + 9, End - Pos - 9);
						Element->Children.Add(TextNode);
						Pos = End + 3;
						continue;
					}
					if (StartsWith(TEXT("<!--")) || StartsWith(TEXT("<?")) || StartsWith(TEXT("<!")))
					{
						if (!SkipMisc())return nullptr;
						continue;
					}
					TSharedPtr<FXmlElement> Child = ReadElement();
					if (!Child.IsValid())return nullptr;
					Element->Children.Add(Child);
				}
				else
				{
					const int32 Start = Pos;
					while (!AtEnd() && Source[Pos] != TEXT('<'))Pos++;
					TSharedPtr<FXmlElement> TextNode = MakeShared<FXmlElement>();
					TextNode->Text = DecodeEntities(Source.Mid(Start, Pos - Start));
					Element->Children.Add(TextNode);
				}
			}
		}
		TSharedPtr<FXmlElement> ReadDocument()
		{
			// Tolerate a UTF-8 BOM.
			if (!AtEnd() && Source[Pos] == 0xFEFF)Pos++;
			if (!SkipMisc())return nullptr;
			if (AtEnd()) { Error = TEXT("empty document"); return nullptr; }
			return ReadElement();
		}
	};

	/** Strip an XML namespace prefix: "ttm:role" -> "role" is NOT wanted; but "tt:p" -> "p" is. */
	static FString LocalName(const FString& Tag)
	{
		int32 Colon = INDEX_NONE;
		return Tag.FindLastChar(TEXT(':'), Colon) ? Tag.Mid(Colon + 1) : Tag;
	}

	static const FXmlElement* FindDescendant(const FXmlElement& Element, const TCHAR* Local)
	{
		for (const auto& Child : Element.Children)
		{
			if (Child->IsText())continue;
			if (LocalName(Child->Tag) == Local)return Child.Get();
			if (const FXmlElement* Found = FindDescendant(*Child, Local))return Found;
		}
		return nullptr;
	}

	static void CollectDescendants(const FXmlElement& Element, const TCHAR* Local, TArray<const FXmlElement*>& Out)
	{
		for (const auto& Child : Element.Children)
		{
			if (Child->IsText())continue;
			if (LocalName(Child->Tag) == Local)
			{
				Out.Add(Child.Get());
			}
			else
			{
				CollectDescendants(*Child, Local, Out);
			}
		}
	}

	static bool ReadTimeAttr(const FXmlElement& Element, const TCHAR* Name, float& Out)
	{
		const FString Value = Element.Attr(Name);
		return !Value.IsEmpty() && UDreamLyricsLibrary::ParseTime(Value, Out);
	}

	/** Words of a line or a background-vocal span: its timed spans in order, text between them kept as spacing. */
	static void ReadWords(const FXmlElement& Container, float LineStart, float LineEnd, FDreamLyricLine& OutLine, FString* OutTranslation, FString* OutRoman, TArray<const FXmlElement*>* OutBackground)
	{
		FString PlainText;
		bool bHasTimedSpans = false;
		for (const auto& Child : Container.Children)
		{
			if (Child->IsText())
			{
				// Whitespace between word spans belongs to the word before it.
				if (OutLine.Words.Num() > 0 && bHasTimedSpans)
				{
					OutLine.Words.Last().Text += Child->Text;
				}
				else
				{
					PlainText += Child->Text;
				}
				continue;
			}
			if (LocalName(Child->Tag) != TEXT("span"))
			{
				PlainText += Child->InnerText();
				continue;
			}
			const FString Role = Child->Attr(TEXT("ttm:role"));
			if (Role == TEXT("x-translation"))
			{
				if (OutTranslation)*OutTranslation = Child->InnerText();
				continue;
			}
			if (Role == TEXT("x-roman"))
			{
				if (OutRoman)*OutRoman = Child->InnerText();
				continue;
			}
			if (Role == TEXT("x-bg"))
			{
				if (OutBackground)OutBackground->Add(Child.Get());
				continue;
			}
			FDreamLyricWord Word;
			Word.Text = Child->InnerText();
			if (ReadTimeAttr(*Child, TEXT("begin"), Word.StartTime) && ReadTimeAttr(*Child, TEXT("end"), Word.EndTime))
			{
				bHasTimedSpans = true;
			}
			else
			{
				Word.StartTime = LineStart;
				Word.EndTime = LineEnd;
			}
			OutLine.Words.Add(Word);
		}
		if (!bHasTimedSpans && OutLine.Words.Num() == 0)
		{
			FDreamLyricWord Word;
			Word.Text = PlainText.TrimStartAndEnd();
			Word.StartTime = LineStart;
			Word.EndTime = LineEnd;
			if (!Word.Text.IsEmpty())
			{
				OutLine.Words.Add(Word);
			}
		}
		// Background-vocal text is often wrapped in parentheses in the source; keep it as written.
		if (OutLine.Words.Num() > 0)
		{
			// A trailing space on the last word is never meaningful.
			OutLine.Words.Last().Text.TrimEndInline();
		}
	}
}

bool UDreamLyricsLibrary::ParseTime(const FString& InText, float& OutSeconds)
{
	FString Text = InText.TrimStartAndEnd();
	if (Text.IsEmpty())return false;
	if (Text.EndsWith(TEXT("ms")))
	{
		OutSeconds = FCString::Atof(*Text.LeftChop(2)) / 1000.0f;
		return true;
	}
	if (Text.EndsWith(TEXT("s")) && !Text.Contains(TEXT(":")))
	{
		OutSeconds = FCString::Atof(*Text.LeftChop(1));
		return true;
	}
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(":"), false);
	if (Parts.Num() < 1 || Parts.Num() > 3)return false;
	double Seconds = 0.0;
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || !FChar::IsDigit(Part[0]))return false;
		Seconds = Seconds * 60.0 + FCString::Atod(*Part);
	}
	OutSeconds = (float)Seconds;
	return true;
}

bool UDreamLyricsLibrary::ParseTTML(const FString& Xml, FDreamLyrics& OutLyrics, FString& OutError)
{
	using namespace DreamLyricsLocal;
	OutLyrics.Lines.Reset();
	OutError.Reset();

	FXmlReader Reader(Xml);
	TSharedPtr<FXmlElement> Root = Reader.ReadDocument();
	if (!Root.IsValid())
	{
		OutError = Reader.Error;
		return false;
	}
	const FXmlElement* Body = FindDescendant(*Root, TEXT("body"));
	if (Body == nullptr)
	{
		OutError = TEXT("no <body>");
		return false;
	}
	// The first agent declared is the lead voice; any other agent on a line marks the second voice.
	FString LeadAgent;
	if (const FXmlElement* Head = FindDescendant(*Root, TEXT("head")))
	{
		TArray<const FXmlElement*> Agents;
		CollectDescendants(*Head, TEXT("agent"), Agents);
		if (Agents.Num() > 0)
		{
			LeadAgent = Agents[0]->Attr(TEXT("xml:id"));
		}
	}

	TArray<const FXmlElement*> Paragraphs;
	CollectDescendants(*Body, TEXT("p"), Paragraphs);
	for (const FXmlElement* P : Paragraphs)
	{
		FDreamLyricLine Line;
		ReadTimeAttr(*P, TEXT("begin"), Line.StartTime);
		ReadTimeAttr(*P, TEXT("end"), Line.EndTime);
		const FString Agent = P->Attr(TEXT("ttm:agent"));
		Line.bSecondary = !Agent.IsEmpty() && !LeadAgent.IsEmpty() && Agent != LeadAgent;

		TArray<const FXmlElement*> Backgrounds;
		ReadWords(*P, Line.StartTime, Line.EndTime, Line, &Line.Translation, &Line.Roman, &Backgrounds);
		if (Line.Words.Num() > 0)
		{
			OutLyrics.Lines.Add(Line);
		}
		// Backing vocals become their own lines, flagged, right after the line they belong to.
		for (const FXmlElement* Bg : Backgrounds)
		{
			FDreamLyricLine BgLine;
			BgLine.bBackground = true;
			BgLine.bSecondary = Line.bSecondary;
			if (!ReadTimeAttr(*Bg, TEXT("begin"), BgLine.StartTime))BgLine.StartTime = Line.StartTime;
			if (!ReadTimeAttr(*Bg, TEXT("end"), BgLine.EndTime))BgLine.EndTime = Line.EndTime;
			ReadWords(*Bg, BgLine.StartTime, BgLine.EndTime, BgLine, &BgLine.Translation, &BgLine.Roman, nullptr);
			if (BgLine.Words.Num() > 0)
			{
				// The span's own times may be missing while its words carry them.
				if (Bg->Attr(TEXT("begin")).IsEmpty())BgLine.StartTime = BgLine.Words[0].StartTime;
				if (Bg->Attr(TEXT("end")).IsEmpty())BgLine.EndTime = BgLine.Words.Last().EndTime;
				OutLyrics.Lines.Add(BgLine);
			}
		}
	}
	OutLyrics.Lines.StableSort([](const FDreamLyricLine& A, const FDreamLyricLine& B) { return A.StartTime < B.StartTime; });
	return OutLyrics.Lines.Num() > 0;
}

bool UDreamLyricsLibrary::ParseLRC(const FString& Text, FDreamLyrics& OutLyrics)
{
	OutLyrics.Lines.Reset();
	TArray<FString> SourceLines;
	Text.ParseIntoArrayLines(SourceLines, false);
	for (const FString& Raw : SourceLines)
	{
		FString Source = Raw.TrimStartAndEnd();
		// One text may carry several time tags: "[00:12.00][00:40.00]chorus".
		TArray<float> Starts;
		while (Source.StartsWith(TEXT("[")))
		{
			const int32 Close = Source.Find(TEXT("]"));
			if (Close == INDEX_NONE)break;
			float Seconds = 0.0f;
			const FString Tag = Source.Mid(1, Close - 1);
			Source = Source.Mid(Close + 1);
			if (ParseTime(Tag, Seconds))
			{
				Starts.Add(Seconds);
			}
			// Metadata tags like [ar:...] are simply skipped.
		}
		if (Starts.Num() == 0)continue;
		// Enhanced LRC: "<mm:ss.xx>word <mm:ss.xx>word".
		TArray<FDreamLyricWord> Words;
		{
			int32 Cursor = 0;
			float PendingStart = -1.0f;
			FString Pending;
			auto FlushWord = [&](float EndTime)
			{
				if (PendingStart >= 0.0f)
				{
					FDreamLyricWord Word;
					Word.Text = Pending;
					Word.StartTime = PendingStart;
					Word.EndTime = EndTime;
					Words.Add(Word);
				}
				Pending.Reset();
			};
			while (Cursor < Source.Len())
			{
				if (Source[Cursor] == TEXT('<'))
				{
					const int32 Close = Source.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
					float Seconds = 0.0f;
					if (Close != INDEX_NONE && ParseTime(Source.Mid(Cursor + 1, Close - Cursor - 1), Seconds))
					{
						FlushWord(Seconds);
						PendingStart = Seconds;
						Cursor = Close + 1;
						continue;
					}
				}
				Pending.AppendChar(Source[Cursor]);
				Cursor++;
			}
			if (PendingStart >= 0.0f)
			{
				FlushWord(-1.0f);//end fixed up below
			}
			else if (!Pending.TrimStartAndEnd().IsEmpty())
			{
				FDreamLyricWord Word;
				Word.Text = Pending.TrimStartAndEnd();
				Words.Add(Word);
			}
		}
		if (Words.Num() == 0)continue;
		for (float Start : Starts)
		{
			FDreamLyricLine Line;
			Line.StartTime = Start;
			Line.Words = Words;
			if (Line.Words.Num() == 1 && Line.Words[0].EndTime == 0.0f && Line.Words[0].StartTime == 0.0f)
			{
				Line.Words[0].StartTime = Start;
			}
			OutLyrics.Lines.Add(Line);
		}
	}
	OutLyrics.Lines.StableSort([](const FDreamLyricLine& A, const FDreamLyricLine& B) { return A.StartTime < B.StartTime; });
	// Ends: the next line's start, and the last word of a line ends with the line.
	for (int32 i = 0; i < OutLyrics.Lines.Num(); i++)
	{
		FDreamLyricLine& Line = OutLyrics.Lines[i];
		Line.EndTime = (i + 1 < OutLyrics.Lines.Num()) ? OutLyrics.Lines[i + 1].StartTime : Line.StartTime + 5.0f;
		for (int32 w = 0; w < Line.Words.Num(); w++)
		{
			FDreamLyricWord& Word = Line.Words[w];
			if (Word.EndTime <= Word.StartTime)
			{
				Word.EndTime = (w + 1 < Line.Words.Num()) ? Line.Words[w + 1].StartTime : Line.EndTime;
			}
		}
	}
	return OutLyrics.Lines.Num() > 0;
}
