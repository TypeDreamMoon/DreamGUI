// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdEngineData.h"

const FDreamPsdEngineValue* FDreamPsdEngineValue::Find(const FString& InKey) const
{
	if (Type != EDreamPsdEngineType::Dict)
	{
		return nullptr;
	}
	for (const TPair<FString, FDreamPsdEngineValue>& Entry : Entries)
	{
		if (Entry.Key.Equals(InKey, ESearchCase::CaseSensitive))
		{
			return &Entry.Value;
		}
	}
	return nullptr;
}

const FDreamPsdEngineValue* FDreamPsdEngineValue::FindPath(const FString& InPath) const
{
	TArray<FString> Parts;
	InPath.ParseIntoArray(Parts, TEXT("/"));
	const FDreamPsdEngineValue* At = this;
	for (const FString& Part : Parts)
	{
		if (At == nullptr)
		{
			return nullptr;
		}
		if (At->Type == EDreamPsdEngineType::Array)
		{
			if (!Part.IsNumeric())
			{
				return nullptr;
			}
			const int32 Index = FCString::Atoi(*Part);
			At = At->Items.IsValidIndex(Index) ? &At->Items[Index] : nullptr;
		}
		else
		{
			At = At->Find(Part);
		}
	}
	return At;
}

double FDreamPsdEngineValue::GetNumber(const FString& InKey, double InDefault) const
{
	const FDreamPsdEngineValue* V = Find(InKey);
	if (V == nullptr)
	{
		return InDefault;
	}
	return V->Type == EDreamPsdEngineType::Number ? V->Number : (V->Type == EDreamPsdEngineType::Bool ? (V->bBool ? 1.0 : 0.0) : InDefault);
}

bool FDreamPsdEngineValue::GetBool(const FString& InKey, bool bInDefault) const
{
	const FDreamPsdEngineValue* V = Find(InKey);
	if (V == nullptr)
	{
		return bInDefault;
	}
	return V->Type == EDreamPsdEngineType::Bool ? V->bBool : (V->Type == EDreamPsdEngineType::Number ? V->Number != 0.0 : bInDefault);
}

FString FDreamPsdEngineValue::GetString(const FString& InKey, const FString& InDefault) const
{
	const FDreamPsdEngineValue* V = Find(InKey);
	return V != nullptr && (V->Type == EDreamPsdEngineType::String || V->Type == EDreamPsdEngineType::Name) ? V->String : InDefault;
}

FDreamPsdEngineValue FDreamPsdEngineValue::MakeDict()
{
	FDreamPsdEngineValue V;
	V.Type = EDreamPsdEngineType::Dict;
	return V;
}

FDreamPsdEngineValue FDreamPsdEngineValue::MakeArray()
{
	FDreamPsdEngineValue V;
	V.Type = EDreamPsdEngineType::Array;
	return V;
}

FDreamPsdEngineValue FDreamPsdEngineValue::MakeNumber(double InValue)
{
	FDreamPsdEngineValue V;
	V.Type = EDreamPsdEngineType::Number;
	V.Number = InValue;
	return V;
}

FDreamPsdEngineValue FDreamPsdEngineValue::MakeBool(bool bInValue)
{
	FDreamPsdEngineValue V;
	V.Type = EDreamPsdEngineType::Bool;
	V.bBool = bInValue;
	return V;
}

FDreamPsdEngineValue FDreamPsdEngineValue::MakeString(const FString& InValue)
{
	FDreamPsdEngineValue V;
	V.Type = EDreamPsdEngineType::String;
	V.String = InValue;
	return V;
}

FDreamPsdEngineValue FDreamPsdEngineValue::MakeName(const FString& InValue)
{
	FDreamPsdEngineValue V;
	V.Type = EDreamPsdEngineType::Name;
	V.String = InValue;
	return V;
}

FDreamPsdEngineValue& FDreamPsdEngineValue::Add(const FString& InKey, const FDreamPsdEngineValue& InValue)
{
	Entries.Emplace(InKey, InValue);
	return Entries.Last().Value;
}

FDreamPsdEngineValue& FDreamPsdEngineValue::Push(const FDreamPsdEngineValue& InValue)
{
	Items.Add(InValue);
	return Items.Last();
}

namespace DreamPsdEngineDataLocal
{
	struct FParser
	{
		const TArray<uint8>& Bytes;
		int32 Pos = 0;
		FString Error;

		explicit FParser(const TArray<uint8>& InBytes) : Bytes(InBytes) {}

		bool AtEnd() const { return Pos >= Bytes.Num(); }
		uint8 Peek(int32 InAhead = 0) const { return Pos + InAhead < Bytes.Num() ? Bytes[Pos + InAhead] : 0; }

		static bool IsSpace(uint8 C) { return C == ' ' || C == '\t' || C == '\r' || C == '\n' || C == 0; }
		static bool IsDelimiter(uint8 C) { return IsSpace(C) || C == '/' || C == '[' || C == ']' || C == '<' || C == '>' || C == '(' || C == ')'; }

		void SkipSpace()
		{
			while (!AtEnd() && IsSpace(Peek()))
			{
				++Pos;
			}
		}

		bool Fail(const FString& InWhat)
		{
			if (Error.IsEmpty())
			{
				Error = FString::Printf(TEXT("%s at byte %d"), *InWhat, Pos);
			}
			return false;
		}

		FString ReadWord()
		{
			FString Out;
			while (!AtEnd() && !IsDelimiter(Peek()))
			{
				Out.AppendChar((TCHAR)Peek());
				++Pos;
			}
			return Out;
		}

		bool ReadString(FString& Out)
		{
			// At '('. Bytes up to the matching unescaped ')', escapes resolved; nested parentheses balance.
			++Pos;
			TArray<uint8> Raw;
			int32 Depth = 0;
			// In UTF-16 text a backslash only ever escapes the next byte ('(', ')' or itself); a letter after it is the
			// other half of a code unit, not an escape.
			const bool bWide = Peek() == 0xFE && Peek(1) == 0xFF;
			while (true)
			{
				if (AtEnd())
				{
					return Fail(TEXT("a string that never closes"));
				}
				const uint8 C = Bytes[Pos++];
				if (C == '\\')
				{
					if (AtEnd())
					{
						return Fail(TEXT("a string ending in a backslash"));
					}
					const uint8 E = Bytes[Pos++];
					switch (bWide ? 0 : E)
					{
					case 'n': Raw.Add('\n'); break;
					case 'r': Raw.Add('\r'); break;
					case 't': Raw.Add('\t'); break;
					case 'b': Raw.Add('\b'); break;
					case 'f': Raw.Add('\f'); break;
					default: Raw.Add(E); break;
					}
					continue;
				}
				if (bWide)
				{
					// Parentheses in the text are always escaped; an unescaped ')' ends the string.
					if (C == ')')
					{
						break;
					}
					Raw.Add(C);
					continue;
				}
				if (C == '(')
				{
					++Depth;
				}
				else if (C == ')')
				{
					if (Depth == 0)
					{
						break;
					}
					--Depth;
				}
				Raw.Add(C);
			}
			if (Raw.Num() >= 2 && Raw[0] == 0xFE && Raw[1] == 0xFF)
			{
				TArray<UTF16CHAR> Units;
				for (int32 i = 2; i + 1 < Raw.Num(); i += 2)
				{
					Units.Add((UTF16CHAR)((uint16(Raw[i]) << 8) | uint16(Raw[i + 1])));
				}
				Units.Add(0);
				Out = FString(StringCast<TCHAR>(Units.GetData()).Get());
			}
			else
			{
				Out.Reset();
				for (uint8 B : Raw)
				{
					Out.AppendChar((TCHAR)B);
				}
			}
			return true;
		}

		bool ReadValue(FDreamPsdEngineValue& Out, int32 InDepth)
		{
			if (InDepth > 128)
			{
				return Fail(TEXT("nesting deeper than 128"));
			}
			SkipSpace();
			if (AtEnd())
			{
				return Fail(TEXT("a value expected and the data ends"));
			}
			const uint8 C = Peek();
			if (C == '<' && Peek(1) == '<')
			{
				Pos += 2;
				Out = FDreamPsdEngineValue::MakeDict();
				while (true)
				{
					SkipSpace();
					if (AtEnd())
					{
						return Fail(TEXT("a dictionary that never closes"));
					}
					if (Peek() == '>' && Peek(1) == '>')
					{
						Pos += 2;
						return true;
					}
					if (Peek() != '/')
					{
						return Fail(TEXT("a dictionary key that is not a /Name"));
					}
					++Pos;
					const FString Key = ReadWord();
					FDreamPsdEngineValue Value;
					if (!ReadValue(Value, InDepth + 1))
					{
						return false;
					}
					Out.Entries.Emplace(Key, MoveTemp(Value));
				}
			}
			if (C == '[')
			{
				++Pos;
				Out = FDreamPsdEngineValue::MakeArray();
				while (true)
				{
					SkipSpace();
					if (AtEnd())
					{
						return Fail(TEXT("an array that never closes"));
					}
					if (Peek() == ']')
					{
						++Pos;
						return true;
					}
					FDreamPsdEngineValue Item;
					if (!ReadValue(Item, InDepth + 1))
					{
						return false;
					}
					Out.Items.Add(MoveTemp(Item));
				}
			}
			if (C == '(')
			{
				Out.Type = EDreamPsdEngineType::String;
				return ReadString(Out.String);
			}
			if (C == '/')
			{
				++Pos;
				Out = FDreamPsdEngineValue::MakeName(ReadWord());
				return true;
			}
			const FString Word = ReadWord();
			if (Word.IsEmpty())
			{
				++Pos;
				return Fail(FString::Printf(TEXT("an unexpected character '%c'"), (TCHAR)C));
			}
			if (Word == TEXT("true") || Word == TEXT("false"))
			{
				Out = FDreamPsdEngineValue::MakeBool(Word == TEXT("true"));
				return true;
			}
			// ".5", "-0.25", "12" -- the engine writes numbers without a leading zero.
			bool bNumber = true;
			for (TCHAR Ch : Word)
			{
				bNumber &= FChar::IsDigit(Ch) || Ch == TEXT('.') || Ch == TEXT('-') || Ch == TEXT('+') || Ch == TEXT('e') || Ch == TEXT('E');
			}
			if (bNumber)
			{
				Out = FDreamPsdEngineValue::MakeNumber(FCString::Atod(*Word));
				return true;
			}
			// A bare word the engine does not normally write; kept as a name rather than refused.
			Out = FDreamPsdEngineValue::MakeName(Word);
			return true;
		}
	};

	void WriteText(TArray<uint8>& Out, const char* InText)
	{
		while (*InText)
		{
			Out.Add((uint8)*InText++);
		}
	}

	void WriteValue(TArray<uint8>& Out, const FDreamPsdEngineValue& V, int32 InIndent)
	{
		auto Indent = [&Out](int32 N) { for (int32 i = 0; i < N; ++i) { Out.Add('\t'); } };
		switch (V.Type)
		{
		case EDreamPsdEngineType::Dict:
			WriteText(Out, "<<\n");
			for (const TPair<FString, FDreamPsdEngineValue>& Entry : V.Entries)
			{
				Indent(InIndent + 1);
				Out.Add('/');
				WriteText(Out, TCHAR_TO_ANSI(*Entry.Key));
				Out.Add(' ');
				WriteValue(Out, Entry.Value, InIndent + 1);
				Out.Add('\n');
			}
			Indent(InIndent);
			WriteText(Out, ">>");
			break;
		case EDreamPsdEngineType::Array:
			Out.Add('[');
			for (const FDreamPsdEngineValue& Item : V.Items)
			{
				Out.Add(' ');
				WriteValue(Out, Item, InIndent + 1);
			}
			WriteText(Out, " ]");
			break;
		case EDreamPsdEngineType::Name:
			Out.Add('/');
			WriteText(Out, TCHAR_TO_ANSI(*V.String));
			break;
		case EDreamPsdEngineType::Number:
		{
			const double R = FMath::RoundToDouble(V.Number);
			const FString Text = FMath::Abs(V.Number - R) < 1e-9 ? FString::Printf(TEXT("%lld"), (long long)R) : FString::Printf(TEXT("%.5f"), V.Number);
			WriteText(Out, TCHAR_TO_ANSI(*Text));
			break;
		}
		case EDreamPsdEngineType::Bool:
			WriteText(Out, V.bBool ? "true" : "false");
			break;
		case EDreamPsdEngineType::String:
		{
			Out.Add('(');
			Out.Add(0xFE);
			Out.Add(0xFF);
			const FTCHARToUTF16 Units(*V.String);
			for (int32 i = 0; i < Units.Length(); ++i)
			{
				const uint16 U = (uint16)Units.Get()[i];
				for (const uint8 B : { uint8(U >> 8), uint8(U) })
				{
					if (B == '(' || B == ')' || B == '\\')
					{
						Out.Add('\\');
					}
					Out.Add(B);
				}
			}
			Out.Add(')');
			break;
		}
		default:
			break;
		}
	}
}

bool DreamPsdEngineData::Parse(const TArray<uint8>& InBytes, FDreamPsdEngineValue& OutRoot, FString& OutError)
{
	DreamPsdEngineDataLocal::FParser Parser(InBytes);
	Parser.SkipSpace();
	if (!Parser.ReadValue(OutRoot, 0))
	{
		OutError = Parser.Error;
		return false;
	}
	return true;
}

void DreamPsdEngineData::Write(const FDreamPsdEngineValue& InRoot, TArray<uint8>& OutBytes)
{
	OutBytes.Reset();
	OutBytes.Add('\n');
	OutBytes.Add('\n');
	DreamPsdEngineDataLocal::WriteValue(OutBytes, InRoot, 0);
	OutBytes.Add('\n');
}
