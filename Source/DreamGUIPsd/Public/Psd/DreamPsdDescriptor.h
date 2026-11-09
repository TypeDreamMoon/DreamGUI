// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The action descriptor: the typed key/value tree the layered image format writes its newer blocks in -- effects,
 * text, fills, vector origination, most adjustments, smart objects. Read whole and kept whole, so a mapping that
 * needs a key the typed model below does not lift can still find it.
 *
 * Plain structs with no UObject, like the rest of the reader.
 */
enum class EDreamPsdValueType : uint8
{
	None,
	/** 'Objc' and 'GlbO': a nested descriptor. */
	Descriptor,
	/** 'VlLs': a list of values. */
	List,
	/** 'doub'. */
	Double,
	/** 'UntF': a double with a unit ('#Ang', '#Pxl', '#Prc', '#Rlt', '#Nne', '#Rsl', '#Pnt', '#Mlm'). */
	UnitFloat,
	/** 'UnFl': several doubles with one unit. Numbers holds them. */
	UnitFloats,
	/** 'TEXT'. */
	String,
	/** 'enum': EnumType and String (the value). */
	Enum,
	/** 'long'. */
	Integer,
	/** 'comp'. */
	LargeInteger,
	/** 'bool'. */
	Boolean,
	/** 'type' and 'GlbC': String is the class id. */
	Class,
	/** 'alis', 'tdta' and 'Pth ': the bytes. */
	RawData,
	/** 'obj ': a reference, its items in List (each a Descriptor-less value whose String names what it points at). */
	Reference,
	/** 'ObAr': an object array, read like a descriptor. */
	ObjectArray,
};

struct FDreamPsdDescriptor;

struct DREAMGUIPSD_API FDreamPsdValue
{
	EDreamPsdValueType Type = EDreamPsdValueType::None;
	/** Double, UnitFloat, Integer, LargeInteger and Boolean (0 or 1). */
	double Number = 0.0;
	/** UnitFloat and UnitFloats: the four characters of the unit. */
	FString Unit;
	/** String; the enum value of Enum; the class id of Class; the kind of a Reference item. */
	FString String;
	/** Enum only: the enum's type id. */
	FString EnumType;
	TArray<double> Numbers;
	TArray<uint8> Raw;
	TSharedPtr<FDreamPsdDescriptor> Descriptor;
	TArray<FDreamPsdValue> List;

	bool IsNumber() const;
	double AsNumber(double InDefault = 0.0) const { return IsNumber() ? Number : InDefault; }
};

struct DREAMGUIPSD_API FDreamPsdDescriptor
{
	/** The descriptor's display name, often empty. */
	FString Name;
	/** Its class id: 'null', 'Clr ', 'RGBC', 'Grdn', 'TxLr'... */
	FString ClassId;
	/** The items in the order they were written. Keys are ids: four characters, or a longer name ("masterFXSwitch"). */
	TArray<TPair<FString, FDreamPsdValue>> Items;

	const FDreamPsdValue* Find(const FString& InKey) const;
	bool Has(const FString& InKey) const { return Find(InKey) != nullptr; }

	double GetNumber(const FString& InKey, double InDefault = 0.0) const;
	/** A unit float's value, with OutUnit its unit when given. */
	double GetUnitFloat(const FString& InKey, double InDefault = 0.0, FString* OutUnit = nullptr) const;
	bool GetBool(const FString& InKey, bool bInDefault = false) const;
	FString GetString(const FString& InKey, const FString& InDefault = FString()) const;
	/** An enum's value (its second id), or InDefault. */
	FString GetEnum(const FString& InKey, const FString& InDefault = FString()) const;
	const FDreamPsdDescriptor* GetDescriptor(const FString& InKey) const;
	const TArray<FDreamPsdValue>* GetList(const FString& InKey) const;

	/**
	 * A colour descriptor under InKey ('Clr ' usually), in sRGB 0..1: RGBC (Rd, Grn, Bl, or redFloat...), HSBC (H, Strt,
	 * Brgh), Grsc (Gry), CMYC (Cyn, Mgnt, Ylw, Blck) and LbCl (Lmnc, A, B) are converted. False when there is none.
	 */
	bool GetColor(const FString& InKey, FLinearColor& OutColor) const;
	/** The colour this descriptor itself is, for a list of colour descriptors. */
	bool ReadAsColor(FLinearColor& OutColor) const;

	/** The whole tree as indented text, for a report or a failing test. */
	FString Dump(int32 InIndent = 0) const;
};

/** Big-endian reading over a byte range, never past it: a read that would run off the end reads zeros and sets bOverrun. */
struct DREAMGUIPSD_API FDreamPsdStream
{
	FDreamPsdStream(const uint8* InData, int64 InSize) : Data(InData), Size(InSize) {}

	const uint8* Data = nullptr;
	int64 Size = 0;
	int64 Pos = 0;
	bool bOverrun = false;

	int64 Remaining() const { return Size > Pos ? Size - Pos : 0; }
	bool AtEnd() const { return Pos >= Size; }
	void Seek(int64 InPos) { Pos = FMath::Clamp<int64>(InPos, 0, Size); }
	void Skip(int64 InCount);
	bool Has(int64 InCount) const { return InCount >= 0 && Pos + InCount <= Size; }

	uint8 U8();
	int8 I8() { return (int8)U8(); }
	uint16 U16();
	int16 I16() { return (int16)U16(); }
	uint32 U32();
	int32 I32() { return (int32)U32(); }
	uint64 U64();
	int64 I64() { return (int64)U64(); }
	float F32();
	double F64();
	/** A 4-byte length in a PSD, an 8-byte one in a PSB. */
	uint64 Length(bool bInWide) { return bInWide ? U64() : (uint64)U32(); }
	/** Four characters, as written. */
	FString Key4();
	/** A Pascal string (a length byte, then that many bytes in the system code page, read as Latin-1), padded so the whole is a multiple of InPadding. */
	FString Pascal(int32 InPadding);
	/** A count of UTF-16 code units, then the units; a trailing NUL is dropped. */
	FString Unicode();
	/** A descriptor key or class id: a length, or 0 then four characters. */
	FString IdString();
	void Bytes(TArray<uint8>& Out, int64 InCount);
	/** A view of the next InCount bytes (clamped), consumed. */
	FDreamPsdStream Sub(int64 InCount);
};

namespace DreamPsdDescriptor
{
	/** Read a descriptor: name, class id, item count, items. Errors go to OutError and stop the read. */
	DREAMGUIPSD_API bool Read(FDreamPsdStream& InStream, FDreamPsdDescriptor& OutDescriptor, FString& OutError);

	/** Read a 4-byte descriptor version (16), then a descriptor. */
	DREAMGUIPSD_API bool ReadVersioned(FDreamPsdStream& InStream, FDreamPsdDescriptor& OutDescriptor, FString& OutError);

	/** Write a descriptor the way Read reads it. The writer of the test documents uses this. */
	DREAMGUIPSD_API void Write(TArray<uint8>& Out, const FDreamPsdDescriptor& InDescriptor);

	/** A value of each kind, for building descriptors to write. */
	DREAMGUIPSD_API FDreamPsdValue MakeNumber(double InValue);
	DREAMGUIPSD_API FDreamPsdValue MakeUnitFloat(const FString& InUnit, double InValue);
	DREAMGUIPSD_API FDreamPsdValue MakeInteger(int32 InValue);
	DREAMGUIPSD_API FDreamPsdValue MakeBool(bool bInValue);
	DREAMGUIPSD_API FDreamPsdValue MakeString(const FString& InValue);
	DREAMGUIPSD_API FDreamPsdValue MakeEnum(const FString& InType, const FString& InValue);
	DREAMGUIPSD_API FDreamPsdValue MakeDescriptor(const FDreamPsdDescriptor& InValue);
	DREAMGUIPSD_API FDreamPsdValue MakeList(const TArray<FDreamPsdValue>& InItems);
	DREAMGUIPSD_API FDreamPsdValue MakeRaw(const TArray<uint8>& InBytes);
	/** An 'RGBC' colour descriptor from an sRGB colour (Rd, Grn, Bl in 0..255). */
	DREAMGUIPSD_API FDreamPsdDescriptor MakeColor(const FLinearColor& InColor);
}
