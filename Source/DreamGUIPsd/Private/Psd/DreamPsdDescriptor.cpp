// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdDescriptor.h"
#include "Psd/DreamPsdColor.h"

// ---------------------------------------------------------------------------------------------------------------------
// The stream
// ---------------------------------------------------------------------------------------------------------------------

void FDreamPsdStream::Skip(int64 InCount)
{
	if (InCount < 0 || Pos + InCount > Size)
	{
		bOverrun = true;
		Pos = Size;
		return;
	}
	Pos += InCount;
}

uint8 FDreamPsdStream::U8()
{
	if (!Has(1))
	{
		bOverrun = true;
		Pos = Size;
		return 0;
	}
	return Data[Pos++];
}

uint16 FDreamPsdStream::U16()
{
	if (!Has(2))
	{
		bOverrun = true;
		Pos = Size;
		return 0;
	}
	const uint16 V = (uint16(Data[Pos]) << 8) | uint16(Data[Pos + 1]);
	Pos += 2;
	return V;
}

uint32 FDreamPsdStream::U32()
{
	if (!Has(4))
	{
		bOverrun = true;
		Pos = Size;
		return 0;
	}
	const uint32 V = (uint32(Data[Pos]) << 24) | (uint32(Data[Pos + 1]) << 16) | (uint32(Data[Pos + 2]) << 8) | uint32(Data[Pos + 3]);
	Pos += 4;
	return V;
}

uint64 FDreamPsdStream::U64()
{
	const uint64 High = U32();
	const uint64 Low = U32();
	return (High << 32) | Low;
}

float FDreamPsdStream::F32()
{
	const uint32 Bits = U32();
	float V;
	FMemory::Memcpy(&V, &Bits, sizeof(V));
	return V;
}

double FDreamPsdStream::F64()
{
	const uint64 Bits = U64();
	double V;
	FMemory::Memcpy(&V, &Bits, sizeof(V));
	return V;
}

FString FDreamPsdStream::Key4()
{
	ANSICHAR Chars[5] = {};
	for (int32 i = 0; i < 4; ++i)
	{
		Chars[i] = (ANSICHAR)U8();
	}
	return FString(ANSI_TO_TCHAR(Chars));
}

FString FDreamPsdStream::Pascal(int32 InPadding)
{
	const int32 Length = U8();
	FString Out;
	Out.Reserve(Length);
	for (int32 i = 0; i < Length; ++i)
	{
		// Latin-1: the format says "system code page", and a byte read as its own code point never fails.
		Out.AppendChar((TCHAR)U8());
	}
	const int32 Total = Length + 1;
	if (InPadding > 1 && Total % InPadding != 0)
	{
		Skip(InPadding - Total % InPadding);
	}
	return Out;
}

FString FDreamPsdStream::Unicode()
{
	const uint32 Count = U32();
	if (!Has(int64(Count) * 2))
	{
		bOverrun = true;
		Pos = Size;
		return FString();
	}
	TArray<UTF16CHAR> Units;
	Units.Reserve(Count + 1);
	for (uint32 i = 0; i < Count; ++i)
	{
		Units.Add((UTF16CHAR)U16());
	}
	while (Units.Num() > 0 && Units.Last() == 0)
	{
		Units.Pop();
	}
	Units.Add(0);
	return FString(StringCast<TCHAR>(Units.GetData()).Get());
}

FString FDreamPsdStream::IdString()
{
	const uint32 Length = U32();
	if (Length == 0)
	{
		return Key4();
	}
	if (!Has(Length))
	{
		bOverrun = true;
		Pos = Size;
		return FString();
	}
	FString Out;
	Out.Reserve(Length);
	for (uint32 i = 0; i < Length; ++i)
	{
		Out.AppendChar((TCHAR)U8());
	}
	return Out;
}

void FDreamPsdStream::Bytes(TArray<uint8>& Out, int64 InCount)
{
	Out.Reset();
	if (InCount <= 0)
	{
		return;
	}
	if (!Has(InCount))
	{
		bOverrun = true;
		InCount = Remaining();
	}
	Out.Append(Data + Pos, (int32)InCount);
	Pos += InCount;
}

FDreamPsdStream FDreamPsdStream::Sub(int64 InCount)
{
	if (InCount < 0 || !Has(InCount))
	{
		bOverrun = true;
		InCount = Remaining();
	}
	FDreamPsdStream Out(Data + Pos, InCount);
	Pos += InCount;
	return Out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------------------------------------------------

bool FDreamPsdValue::IsNumber() const
{
	return Type == EDreamPsdValueType::Double || Type == EDreamPsdValueType::UnitFloat || Type == EDreamPsdValueType::Integer
		|| Type == EDreamPsdValueType::LargeInteger || Type == EDreamPsdValueType::Boolean;
}

const FDreamPsdValue* FDreamPsdDescriptor::Find(const FString& InKey) const
{
	for (const TPair<FString, FDreamPsdValue>& Item : Items)
	{
		if (Item.Key.Equals(InKey, ESearchCase::CaseSensitive))
		{
			return &Item.Value;
		}
	}
	// Keys are written with their trailing spaces ("Clr ", "Nm  "); a caller writing them without still finds them.
	for (const TPair<FString, FDreamPsdValue>& Item : Items)
	{
		if (Item.Key.TrimEnd().Equals(InKey.TrimEnd(), ESearchCase::CaseSensitive))
		{
			return &Item.Value;
		}
	}
	return nullptr;
}

double FDreamPsdDescriptor::GetNumber(const FString& InKey, double InDefault) const
{
	const FDreamPsdValue* V = Find(InKey);
	return V != nullptr ? V->AsNumber(InDefault) : InDefault;
}

double FDreamPsdDescriptor::GetUnitFloat(const FString& InKey, double InDefault, FString* OutUnit) const
{
	const FDreamPsdValue* V = Find(InKey);
	if (V == nullptr || !V->IsNumber())
	{
		return InDefault;
	}
	if (OutUnit != nullptr)
	{
		*OutUnit = V->Unit;
	}
	return V->Number;
}

bool FDreamPsdDescriptor::GetBool(const FString& InKey, bool bInDefault) const
{
	const FDreamPsdValue* V = Find(InKey);
	return V != nullptr && V->IsNumber() ? V->Number != 0.0 : bInDefault;
}

FString FDreamPsdDescriptor::GetString(const FString& InKey, const FString& InDefault) const
{
	const FDreamPsdValue* V = Find(InKey);
	return V != nullptr && (V->Type == EDreamPsdValueType::String || V->Type == EDreamPsdValueType::Enum || V->Type == EDreamPsdValueType::Class)
		? V->String : InDefault;
}

FString FDreamPsdDescriptor::GetEnum(const FString& InKey, const FString& InDefault) const
{
	const FDreamPsdValue* V = Find(InKey);
	return V != nullptr && V->Type == EDreamPsdValueType::Enum ? V->String : InDefault;
}

const FDreamPsdDescriptor* FDreamPsdDescriptor::GetDescriptor(const FString& InKey) const
{
	const FDreamPsdValue* V = Find(InKey);
	return V != nullptr && V->Descriptor.IsValid() ? V->Descriptor.Get() : nullptr;
}

const TArray<FDreamPsdValue>* FDreamPsdDescriptor::GetList(const FString& InKey) const
{
	const FDreamPsdValue* V = Find(InKey);
	return V != nullptr && V->Type == EDreamPsdValueType::List ? &V->List : nullptr;
}

bool FDreamPsdDescriptor::GetColor(const FString& InKey, FLinearColor& OutColor) const
{
	const FDreamPsdDescriptor* Colour = GetDescriptor(InKey);
	return Colour != nullptr && Colour->ReadAsColor(OutColor);
}

bool FDreamPsdDescriptor::ReadAsColor(FLinearColor& OutColor) const
{
	const FString Class = ClassId.TrimEnd();
	if (Class == TEXT("RGBC") || Has(TEXT("Rd  ")) || Has(TEXT("redFloat")))
	{
		if (Has(TEXT("redFloat")))
		{
			OutColor = FLinearColor((float)GetNumber(TEXT("redFloat")), (float)GetNumber(TEXT("greenFloat")), (float)GetNumber(TEXT("blueFloat")), 1.0f);
		}
		else
		{
			OutColor = FLinearColor((float)GetNumber(TEXT("Rd  ")) / 255.0f, (float)GetNumber(TEXT("Grn ")) / 255.0f, (float)GetNumber(TEXT("Bl  ")) / 255.0f, 1.0f);
		}
		return true;
	}
	if (Class == TEXT("HSBC"))
	{
		// Hue in degrees, saturation and brightness in percent.
		const float H = (float)GetUnitFloat(TEXT("H   "));
		const float S = (float)GetNumber(TEXT("Strt")) / 100.0f;
		const float B = (float)GetNumber(TEXT("Brgh")) / 100.0f;
		OutColor = DreamPsdColor::HsbToRgb(H, S, B);
		return true;
	}
	if (Class == TEXT("Grsc"))
	{
		// Gray is the amount of ink, in percent: 0 is white.
		const float G = 1.0f - (float)GetNumber(TEXT("Gry ")) / 100.0f;
		OutColor = FLinearColor(G, G, G, 1.0f);
		return true;
	}
	if (Class == TEXT("CMYC"))
	{
		OutColor = DreamPsdColor::CmykToRgb((float)GetNumber(TEXT("Cyn ")) / 100.0f, (float)GetNumber(TEXT("Mgnt")) / 100.0f,
			(float)GetNumber(TEXT("Ylw ")) / 100.0f, (float)GetNumber(TEXT("Blck")) / 100.0f);
		return true;
	}
	if (Class == TEXT("LbCl"))
	{
		OutColor = DreamPsdColor::LabToRgb((float)GetNumber(TEXT("Lmnc")), (float)GetNumber(TEXT("A   ")), (float)GetNumber(TEXT("B   ")));
		return true;
	}
	return false;
}

namespace DreamPsdDescriptorLocal
{
	FString DumpValue(const FDreamPsdValue& V, int32 InIndent)
	{
		switch (V.Type)
		{
		case EDreamPsdValueType::Descriptor:
		case EDreamPsdValueType::ObjectArray:
			return V.Descriptor.IsValid() ? V.Descriptor->Dump(InIndent) : TEXT("{}");
		case EDreamPsdValueType::List:
		case EDreamPsdValueType::Reference:
		{
			FString Out = TEXT("[\n");
			for (const FDreamPsdValue& Item : V.List)
			{
				Out += FString::ChrN(InIndent + 2, TEXT(' ')) + DumpValue(Item, InIndent + 2) + TEXT("\n");
			}
			return Out + FString::ChrN(InIndent, TEXT(' ')) + TEXT("]");
		}
		case EDreamPsdValueType::Double: return FString::SanitizeFloat(V.Number);
		case EDreamPsdValueType::UnitFloat: return FString::Printf(TEXT("%s %s"), *FString::SanitizeFloat(V.Number), *V.Unit);
		case EDreamPsdValueType::UnitFloats:
		{
			TArray<FString> Parts;
			for (double D : V.Numbers)
			{
				Parts.Add(FString::SanitizeFloat(D));
			}
			return FString::Printf(TEXT("%s [%s]"), *V.Unit, *FString::Join(Parts, TEXT(", ")));
		}
		case EDreamPsdValueType::String: return FString::Printf(TEXT("\"%s\""), *V.String);
		case EDreamPsdValueType::Enum: return FString::Printf(TEXT("%s.%s"), *V.EnumType, *V.String);
		case EDreamPsdValueType::Integer:
		case EDreamPsdValueType::LargeInteger: return FString::Printf(TEXT("%lld"), (long long)V.Number);
		case EDreamPsdValueType::Boolean: return V.Number != 0.0 ? TEXT("true") : TEXT("false");
		case EDreamPsdValueType::Class: return FString::Printf(TEXT("class %s"), *V.String);
		case EDreamPsdValueType::RawData: return FString::Printf(TEXT("<%d bytes>"), V.Raw.Num());
		default: return TEXT("?");
		}
	}

	bool ReadValue(FDreamPsdStream& S, const FString& InType, FDreamPsdValue& Out, FString& OutError, int32 InDepth);

	bool ReadBody(FDreamPsdStream& S, FDreamPsdDescriptor& Out, FString& OutError, int32 InDepth)
	{
		if (InDepth > 64)
		{
			OutError = TEXT("descriptors nest deeper than 64");
			return false;
		}
		Out.Name = S.Unicode();
		Out.ClassId = S.IdString();
		const uint32 Count = S.U32();
		if (S.bOverrun || Count > 100000)
		{
			OutError = TEXT("a descriptor's header runs past its block");
			return false;
		}
		Out.Items.Reserve(Count);
		for (uint32 i = 0; i < Count; ++i)
		{
			const FString Key = S.IdString();
			const FString Type = S.Key4();
			if (S.bOverrun)
			{
				OutError = FString::Printf(TEXT("a descriptor item (%s) runs past its block"), *Key);
				return false;
			}
			FDreamPsdValue Value;
			if (!ReadValue(S, Type, Value, OutError, InDepth))
			{
				OutError = FString::Printf(TEXT("%s.%s: %s"), *Out.ClassId, *Key, *OutError);
				return false;
			}
			Out.Items.Emplace(Key, MoveTemp(Value));
		}
		return true;
	}

	bool ReadValue(FDreamPsdStream& S, const FString& InType, FDreamPsdValue& Out, FString& OutError, int32 InDepth)
	{
		if (InType == TEXT("Objc") || InType == TEXT("GlbO"))
		{
			Out.Type = EDreamPsdValueType::Descriptor;
			Out.Descriptor = MakeShared<FDreamPsdDescriptor>();
			return ReadBody(S, *Out.Descriptor, OutError, InDepth + 1);
		}
		if (InType == TEXT("VlLs"))
		{
			Out.Type = EDreamPsdValueType::List;
			const uint32 Count = S.U32();
			if (Count > 1000000)
			{
				OutError = TEXT("a list claims more items than a block can hold");
				return false;
			}
			Out.List.Reserve(Count);
			for (uint32 i = 0; i < Count && !S.bOverrun; ++i)
			{
				const FString Type = S.Key4();
				FDreamPsdValue Item;
				if (!ReadValue(S, Type, Item, OutError, InDepth + 1))
				{
					return false;
				}
				Out.List.Add(MoveTemp(Item));
			}
			return !S.bOverrun;
		}
		if (InType == TEXT("doub"))
		{
			Out.Type = EDreamPsdValueType::Double;
			Out.Number = S.F64();
			return !S.bOverrun;
		}
		if (InType == TEXT("UntF"))
		{
			Out.Type = EDreamPsdValueType::UnitFloat;
			Out.Unit = S.Key4();
			Out.Number = S.F64();
			return !S.bOverrun;
		}
		if (InType == TEXT("UnFl"))
		{
			Out.Type = EDreamPsdValueType::UnitFloats;
			Out.Unit = S.Key4();
			const uint32 Count = S.U32();
			if (!S.Has(int64(Count) * 8))
			{
				OutError = TEXT("unit floats run past their block");
				return false;
			}
			for (uint32 i = 0; i < Count; ++i)
			{
				Out.Numbers.Add(S.F64());
			}
			Out.Number = Out.Numbers.Num() > 0 ? Out.Numbers[0] : 0.0;
			return true;
		}
		if (InType == TEXT("TEXT"))
		{
			Out.Type = EDreamPsdValueType::String;
			Out.String = S.Unicode();
			return !S.bOverrun;
		}
		if (InType == TEXT("enum"))
		{
			Out.Type = EDreamPsdValueType::Enum;
			Out.EnumType = S.IdString();
			Out.String = S.IdString();
			return !S.bOverrun;
		}
		if (InType == TEXT("long"))
		{
			Out.Type = EDreamPsdValueType::Integer;
			Out.Number = (double)S.I32();
			return !S.bOverrun;
		}
		if (InType == TEXT("comp"))
		{
			Out.Type = EDreamPsdValueType::LargeInteger;
			Out.Number = (double)S.I64();
			return !S.bOverrun;
		}
		if (InType == TEXT("bool"))
		{
			Out.Type = EDreamPsdValueType::Boolean;
			Out.Number = S.U8() != 0 ? 1.0 : 0.0;
			return !S.bOverrun;
		}
		if (InType == TEXT("type") || InType == TEXT("GlbC"))
		{
			Out.Type = EDreamPsdValueType::Class;
			Out.Unit = S.Unicode();
			Out.String = S.IdString();
			return !S.bOverrun;
		}
		if (InType == TEXT("alis") || InType == TEXT("tdta") || InType == TEXT("Pth "))
		{
			Out.Type = EDreamPsdValueType::RawData;
			const uint32 Length = S.U32();
			S.Bytes(Out.Raw, Length);
			return !S.bOverrun;
		}
		if (InType == TEXT("obj "))
		{
			Out.Type = EDreamPsdValueType::Reference;
			const uint32 Count = S.U32();
			for (uint32 i = 0; i < Count && !S.bOverrun; ++i)
			{
				const FString Kind = S.Key4();
				FDreamPsdValue Item;
				Item.Type = EDreamPsdValueType::String;
				Item.EnumType = Kind;
				if (Kind == TEXT("prop"))
				{
					S.Unicode();
					S.IdString();
					Item.String = S.IdString();
				}
				else if (Kind == TEXT("Clss"))
				{
					S.Unicode();
					Item.String = S.IdString();
				}
				else if (Kind == TEXT("Enmr"))
				{
					S.Unicode();
					S.IdString();
					S.IdString();
					Item.String = S.IdString();
				}
				else if (Kind == TEXT("rele"))
				{
					S.Unicode();
					S.IdString();
					Item.Number = S.I32();
				}
				else if (Kind == TEXT("Idnt") || Kind == TEXT("indx"))
				{
					Item.Number = S.I32();
				}
				else if (Kind == TEXT("name"))
				{
					S.Unicode();
					S.IdString();
					Item.String = S.Unicode();
				}
				else
				{
					OutError = FString::Printf(TEXT("a reference item of kind '%s' this reader does not know"), *Kind);
					return false;
				}
				Out.List.Add(MoveTemp(Item));
			}
			return !S.bOverrun;
		}
		if (InType == TEXT("ObAr"))
		{
			Out.Type = EDreamPsdValueType::ObjectArray;
			S.U32(); // the count of objects the array stands for
			Out.Descriptor = MakeShared<FDreamPsdDescriptor>();
			return ReadBody(S, *Out.Descriptor, OutError, InDepth + 1);
		}
		OutError = FString::Printf(TEXT("a value of type '%s' this reader does not know"), *InType);
		return false;
	}

	// --- writing ---

	void W8(TArray<uint8>& Out, uint8 V) { Out.Add(V); }
	void W32(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(uint8(V >> 24)); Out.Add(uint8(V >> 16)); Out.Add(uint8(V >> 8)); Out.Add(uint8(V));
	}
	void W64(TArray<uint8>& Out, uint64 V) { W32(Out, uint32(V >> 32)); W32(Out, uint32(V)); }
	void WF64(TArray<uint8>& Out, double V)
	{
		uint64 Bits;
		FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		W64(Out, Bits);
	}
	void WKey4(TArray<uint8>& Out, const FString& InKey)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			Out.Add(i < InKey.Len() ? uint8(InKey[i]) : uint8(' '));
		}
	}
	void WId(TArray<uint8>& Out, const FString& InId)
	{
		if (InId.Len() == 4)
		{
			W32(Out, 0);
			WKey4(Out, InId);
			return;
		}
		W32(Out, InId.Len());
		for (TCHAR C : InId)
		{
			Out.Add(uint8(C));
		}
	}
	void WUnicode(TArray<uint8>& Out, const FString& InText)
	{
		const FTCHARToUTF16 Units(*InText);
		W32(Out, Units.Length() + 1);
		for (int32 i = 0; i < Units.Length(); ++i)
		{
			const uint16 U = (uint16)Units.Get()[i];
			Out.Add(uint8(U >> 8));
			Out.Add(uint8(U));
		}
		Out.Add(0);
		Out.Add(0);
	}

	void WriteBody(TArray<uint8>& Out, const FDreamPsdDescriptor& D);

	void WriteValue(TArray<uint8>& Out, const FDreamPsdValue& V)
	{
		switch (V.Type)
		{
		case EDreamPsdValueType::Descriptor:
			WKey4(Out, TEXT("Objc"));
			WriteBody(Out, V.Descriptor.IsValid() ? *V.Descriptor : FDreamPsdDescriptor());
			break;
		case EDreamPsdValueType::List:
			WKey4(Out, TEXT("VlLs"));
			W32(Out, V.List.Num());
			for (const FDreamPsdValue& Item : V.List)
			{
				WriteValue(Out, Item);
			}
			break;
		case EDreamPsdValueType::Double:
			WKey4(Out, TEXT("doub"));
			WF64(Out, V.Number);
			break;
		case EDreamPsdValueType::UnitFloat:
			WKey4(Out, TEXT("UntF"));
			WKey4(Out, V.Unit);
			WF64(Out, V.Number);
			break;
		case EDreamPsdValueType::String:
			WKey4(Out, TEXT("TEXT"));
			WUnicode(Out, V.String);
			break;
		case EDreamPsdValueType::Enum:
			WKey4(Out, TEXT("enum"));
			WId(Out, V.EnumType);
			WId(Out, V.String);
			break;
		case EDreamPsdValueType::Integer:
			WKey4(Out, TEXT("long"));
			W32(Out, (uint32)(int32)V.Number);
			break;
		case EDreamPsdValueType::Boolean:
			WKey4(Out, TEXT("bool"));
			W8(Out, V.Number != 0.0 ? 1 : 0);
			break;
		case EDreamPsdValueType::RawData:
			WKey4(Out, TEXT("tdta"));
			W32(Out, V.Raw.Num());
			Out.Append(V.Raw);
			break;
		default:
			// Only the kinds a test document needs are written.
			WKey4(Out, TEXT("long"));
			W32(Out, 0);
			break;
		}
	}

	void WriteBody(TArray<uint8>& Out, const FDreamPsdDescriptor& D)
	{
		WUnicode(Out, D.Name);
		WId(Out, D.ClassId.IsEmpty() ? FString(TEXT("null")) : D.ClassId);
		W32(Out, D.Items.Num());
		for (const TPair<FString, FDreamPsdValue>& Item : D.Items)
		{
			WId(Out, Item.Key);
			WriteValue(Out, Item.Value);
		}
	}
}

FString FDreamPsdDescriptor::Dump(int32 InIndent) const
{
	FString Out = FString::Printf(TEXT("%s {\n"), *ClassId);
	for (const TPair<FString, FDreamPsdValue>& Item : Items)
	{
		Out += FString::ChrN(InIndent + 2, TEXT(' ')) + Item.Key + TEXT(": ") + DreamPsdDescriptorLocal::DumpValue(Item.Value, InIndent + 2) + TEXT("\n");
	}
	return Out + FString::ChrN(InIndent, TEXT(' ')) + TEXT("}");
}

bool DreamPsdDescriptor::Read(FDreamPsdStream& InStream, FDreamPsdDescriptor& OutDescriptor, FString& OutError)
{
	return DreamPsdDescriptorLocal::ReadBody(InStream, OutDescriptor, OutError, 0);
}

bool DreamPsdDescriptor::ReadVersioned(FDreamPsdStream& InStream, FDreamPsdDescriptor& OutDescriptor, FString& OutError)
{
	const uint32 Version = InStream.U32();
	if (Version != 16)
	{
		OutError = FString::Printf(TEXT("descriptor version %u, where 16 is the only one written"), Version);
		return false;
	}
	return Read(InStream, OutDescriptor, OutError);
}

void DreamPsdDescriptor::Write(TArray<uint8>& Out, const FDreamPsdDescriptor& InDescriptor)
{
	DreamPsdDescriptorLocal::WriteBody(Out, InDescriptor);
}

FDreamPsdValue DreamPsdDescriptor::MakeNumber(double InValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::Double;
	V.Number = InValue;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeUnitFloat(const FString& InUnit, double InValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::UnitFloat;
	V.Unit = InUnit;
	V.Number = InValue;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeInteger(int32 InValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::Integer;
	V.Number = InValue;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeBool(bool bInValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::Boolean;
	V.Number = bInValue ? 1.0 : 0.0;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeString(const FString& InValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::String;
	V.String = InValue;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeEnum(const FString& InType, const FString& InValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::Enum;
	V.EnumType = InType;
	V.String = InValue;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeDescriptor(const FDreamPsdDescriptor& InValue)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::Descriptor;
	V.Descriptor = MakeShared<FDreamPsdDescriptor>(InValue);
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeList(const TArray<FDreamPsdValue>& InItems)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::List;
	V.List = InItems;
	return V;
}

FDreamPsdValue DreamPsdDescriptor::MakeRaw(const TArray<uint8>& InBytes)
{
	FDreamPsdValue V;
	V.Type = EDreamPsdValueType::RawData;
	V.Raw = InBytes;
	return V;
}

FDreamPsdDescriptor DreamPsdDescriptor::MakeColor(const FLinearColor& InColor)
{
	FDreamPsdDescriptor D;
	D.ClassId = TEXT("RGBC");
	D.Items.Emplace(TEXT("Rd  "), MakeNumber(InColor.R * 255.0));
	D.Items.Emplace(TEXT("Grn "), MakeNumber(InColor.G * 255.0));
	D.Items.Emplace(TEXT("Bl  "), MakeNumber(InColor.B * 255.0));
	return D;
}
