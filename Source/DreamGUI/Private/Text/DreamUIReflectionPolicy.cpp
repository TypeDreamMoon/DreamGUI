// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIReflectionPolicy.h"

#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUIValueFormat.h"

#include "UObject/UnrealType.h"

namespace DreamUIReflection
{
	namespace Local
	{
		/** Owner-name + property-name pairs excluded by code rather than by meta. */
		TSet<TPair<FName, FName>>& GetExclusions()
		{
			static TSet<TPair<FName, FName>> Exclusions;
			return Exclusions;
		}

		bool IsContainer(const FProperty* InProperty)
		{
			return InProperty->IsA<FArrayProperty>() || InProperty->IsA<FMapProperty>()
				|| InProperty->IsA<FSetProperty>();
		}

		/** A field inside a struct: no Edit-or-setter gate, because plain structs are plain data. */
		bool IsSweepField(const FProperty* InProperty)
		{
			FString Unused;
			return !IsHidden(InProperty)
				&& !IsContainer(InProperty)
				&& !InProperty->HasAnyPropertyFlags(CPF_EditConst)
				&& FDreamUITextBuilder::IsWritableFromText(InProperty, Unused);
		}

		void AppendLeafPaths(const FProperty* InProperty, const FString& InPrefix, TArray<FString>& OutPaths)
		{
			const FString Path = InPrefix.IsEmpty()
				? InProperty->GetName()
				: FString::Printf(TEXT("%s.%s"), *InPrefix, *InProperty->GetName());

			const FStructProperty* AsStruct = CastField<FStructProperty>(InProperty);
			if (AsStruct == nullptr || DreamUIValueFormat::HasShortForm(InProperty))
			{
				// A leaf: a scalar, a string, an enum, an object reference, or a struct whose whole
				// value has one spelling. Whether it can actually be PRINTED is the printer's call --
				// enumerating a leaf nothing can spell costs a comparison that quietly declines.
				OutPaths.Add(Path);
				return;
			}
			for (TFieldIterator<FProperty> It(AsStruct->Struct); It; ++It)
			{
				if (IsSweepField(*It))
				{
					AppendLeafPaths(*It, Path, OutPaths);
				}
			}
		}
	}

	bool IsHidden(const FProperty* InProperty)
	{
		if (InProperty == nullptr)
		{
			return true;
		}
#if WITH_EDITORONLY_DATA
		if (InProperty->HasMetaData(MetaHidden))
		{
			return true;
		}
#endif
		const UStruct* Owner = InProperty->GetOwnerStruct();
		return Owner != nullptr
			&& Local::GetExclusions().Contains(TPair<FName, FName>(Owner->GetFName(), InProperty->GetFName()));
	}

	void AddExclusion(const FName InOwnerName, const FName InPropertyName)
	{
		Local::GetExclusions().Add(TPair<FName, FName>(InOwnerName, InPropertyName));
	}

	bool IsSweepRoot(const FProperty* InProperty)
	{
		if (InProperty == nullptr)
		{
			return false;
		}
		// Edit-or-setter -- see the header for why neither alone is right. EditConst is a row the
		// panel itself refuses to edit, so a sweep that wrote it would author what no one can touch.
		if (!InProperty->HasAnyPropertyFlags(CPF_Edit) && !InProperty->HasSetter())
		{
			return false;
		}
		if (InProperty->HasAnyPropertyFlags(CPF_EditConst) || Local::IsContainer(InProperty))
		{
			return false;
		}
		FString Unused;
		return !IsHidden(InProperty) && FDreamUITextBuilder::IsWritableFromText(InProperty, Unused);
	}

	const TArray<FString>& GetWritableLeafPaths(const UStruct* InScope)
	{
		// Weak keys, not raw pointers: a Blueprint behaviour class is a real possibility here, and a
		// reinstanced or collected class can hand its address to a NEW class -- a raw-pointer cache
		// would then serve the old class's paths for the new one, silently. A weak key carries the
		// serial number, so a stale entry can never match a fresh object; it merely lingers, bounded
		// by how many classes a session compiles.
		static TMap<TWeakObjectPtr<const UStruct>, TArray<FString>> Cache;
		if (InScope == nullptr)
		{
			static const TArray<FString> Empty;
			return Empty;
		}
		if (const TArray<FString>* Found = Cache.Find(InScope))
		{
			return *Found;
		}

		TArray<FString> Paths;
		for (TFieldIterator<FProperty> It(InScope); It; ++It)
		{
			if (IsSweepRoot(*It))
			{
				Local::AppendLeafPaths(*It, FString(), Paths);
			}
		}
		return Cache.Add(InScope, MoveTemp(Paths));
	}
}
