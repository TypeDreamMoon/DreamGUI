// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * EngineData: the text engine's own serialisation inside a type layer's descriptor. A PostScript-like syntax --
 * `<< /Key value >>` dictionaries, `[ ]` arrays, `/Name`s, numbers, `true`/`false` and `( )` strings in UTF-16 with a
 * byte-order mark and backslash escapes -- held here as a tree, in the order it was written.
 */
enum class EDreamPsdEngineType : uint8
{
	None,
	Dict,
	Array,
	/** `/Name` outside a key position. */
	Name,
	Number,
	Bool,
	String,
};

struct DREAMGUIPSD_API FDreamPsdEngineValue
{
	EDreamPsdEngineType Type = EDreamPsdEngineType::None;
	double Number = 0.0;
	bool bBool = false;
	/** String and Name. */
	FString String;
	/** Dict entries, ordered. */
	TArray<TPair<FString, FDreamPsdEngineValue>> Entries;
	/** Array items. */
	TArray<FDreamPsdEngineValue> Items;

	/** A dict entry, or null. */
	const FDreamPsdEngineValue* Find(const FString& InKey) const;
	/** A path of dict keys and array indices ("EngineDict/StyleRun/RunArray/0/StyleSheet"), or null. */
	const FDreamPsdEngineValue* FindPath(const FString& InPath) const;
	double GetNumber(const FString& InKey, double InDefault = 0.0) const;
	bool GetBool(const FString& InKey, bool bInDefault = false) const;
	FString GetString(const FString& InKey, const FString& InDefault = FString()) const;

	static FDreamPsdEngineValue MakeDict();
	static FDreamPsdEngineValue MakeArray();
	static FDreamPsdEngineValue MakeNumber(double InValue);
	static FDreamPsdEngineValue MakeBool(bool bInValue);
	static FDreamPsdEngineValue MakeString(const FString& InValue);
	static FDreamPsdEngineValue MakeName(const FString& InValue);
	FDreamPsdEngineValue& Add(const FString& InKey, const FDreamPsdEngineValue& InValue);
	FDreamPsdEngineValue& Push(const FDreamPsdEngineValue& InValue);
};

namespace DreamPsdEngineData
{
	/** Parse the raw bytes. False, with OutError naming the byte offset, when they do not read. */
	DREAMGUIPSD_API bool Parse(const TArray<uint8>& InBytes, FDreamPsdEngineValue& OutRoot, FString& OutError);

	/** Write a tree the way Parse reads it (strings as UTF-16 with a byte-order mark). */
	DREAMGUIPSD_API void Write(const FDreamPsdEngineValue& InRoot, TArray<uint8>& OutBytes);
}
