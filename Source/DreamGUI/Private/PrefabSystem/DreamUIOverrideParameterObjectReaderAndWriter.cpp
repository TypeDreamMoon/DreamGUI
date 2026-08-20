// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIObjectReaderAndWriter.h"
#include "PrefabSystem/WidgetSerializerBase.h"
#include "Serialization/MemoryReader.h"
#include "Engine/Blueprint.h"

namespace DreamUIPrefabSystem
{
	FDreamUIOverrideParameterObjectWriter::FDreamUIOverrideParameterObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames)
		: FDreamUIObjectWriter(Bytes, InSerializer, {})
		, OverridePropertyNames(InOverridePropertyNames)
	{
		
	}
	bool FDreamUIOverrideParameterObjectWriter::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (DreamUIPrefab_ShouldSkipProperty(InProperty))
		{
			return true;
		}

		if (CurrentIsMemberProperty(*this))
		{
			if (OverridePropertyNames.Contains(InProperty->GetFName()))
			{
				return false;
			}
			else
			{
				return true;
			}
		}

		return false;
	}
	bool FDreamUIOverrideParameterObjectWriter::SerializeObject(UObject* Object)
	{
		if (Object->IsAsset())
		{
			auto id = Serializer.FindOrAddAssetIdFromList(Object);
			auto type = (uint8)EObjectType::Asset;
			*this << type;
			*this << id;
			return true;
		}
		else
		{
			auto guidPtr = Serializer.MapObjectToGuid.Find(Object);
			if (guidPtr != nullptr)
			{
				auto type = (uint8)EObjectType::ObjectReference;
				*this << type;
				*this << *guidPtr;
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	FString FDreamUIOverrideParameterObjectWriter::GetArchiveName() const
	{
		return TEXT("FDreamUIOverrideParameterObjectWriter");
	}


	FDreamUIOverrideParameterObjectReader::FDreamUIOverrideParameterObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames)
		: FDreamUIObjectReader(Bytes, InSerializer, {})
		, OverridePropertyNames(InOverridePropertyNames)
	{
		
	}
	bool FDreamUIOverrideParameterObjectReader::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (DreamUIPrefab_ShouldSkipProperty(InProperty))
		{
			return true;
		}

		if (CurrentIsMemberProperty(*this))
		{
			if (OverridePropertyNames.Contains(InProperty->GetFName()))
			{
				return false;
			}
			else
			{
				return true;
			}
		}

		return false;
	}
	bool FDreamUIOverrideParameterObjectReader::SerializeObject(UObject*& Object, bool CanSerializeClass)
	{
		uint8 typeUint8 = 0;
		*this << typeUint8;
		auto type = (EObjectType)typeUint8;
		switch (type)
		{
		case DreamUIPrefabSystem::EObjectType::None:
			Object = nullptr;
			return true;
		case DreamUIPrefabSystem::EObjectType::Class:
		{
			check(CanSerializeClass);
			int32 id = -1;
			*this << id;
			auto asset = Serializer.FindClassFromListByIndex(id);
			if (IsValid(asset))
			{
				Object = asset;
				return true;
			}
			return false;
		}
		break;
		case DreamUIPrefabSystem::EObjectType::Asset:
		{
			int32 id = -1;
			*this << id;
			auto asset = Serializer.FindAssetFromListByIndex(id);
			if (IsValid(asset))
			{
				Object = asset;
				return true;
			}
			return false;
		}
		break;
		case DreamUIPrefabSystem::EObjectType::ObjectReference:
		{
			FGuid guid;
			*this << guid;
			if (auto ObjectPtr = Serializer.MapGuidToObject.Find(guid); ObjectPtr && IsValid(*ObjectPtr))
			{
				Object = *ObjectPtr;
				return true;
			}
		}
		break;
		}
		return false;
	}
	FString FDreamUIOverrideParameterObjectReader::GetArchiveName() const
	{
		return TEXT("FDreamUIOverrideParameterObjectReader");
	}





	FDreamUIImmediateOverrideParameterObjectWriter::FDreamUIImmediateOverrideParameterObjectWriter(UObject* Object, TArray< uint8 >& Bytes, WidgetSerializerBase& Serializer, const TArray<FName>& InOverridePropertyNames)
		: FObjectWriter(Bytes)
		, OverridePropertyNames(InOverridePropertyNames)
	{
		SetIsLoading(false);
		SetIsSaving(true);

		Serializer.SetupArchive(*this);

		Object->Serialize(*this);
	}
	bool FDreamUIImmediateOverrideParameterObjectWriter::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (DreamUIPrefab_ShouldSkipProperty(InProperty))
		{
			return true;
		}

		if (CurrentIsMemberProperty(*this))
		{
			if (OverridePropertyNames.Contains(InProperty->GetFName()))
			{
				return false;
			}
			else
			{
				return true;
			}
		}

		return false;
	}
	FString FDreamUIImmediateOverrideParameterObjectWriter::GetArchiveName() const
	{
		return TEXT("FDreamUIImmediateOverrideParameterObjectWriter");
	}


	FDreamUIImmediateOverrideParameterObjectReader::FDreamUIImmediateOverrideParameterObjectReader(UObject* Object, TArray< uint8 >& Bytes, WidgetSerializerBase& Serializer, const TArray<FName>& InOverridePropertyNames)
		: FObjectReader(Bytes)
		, OverridePropertyNames(InOverridePropertyNames)
	{
		SetIsLoading(true);
		SetIsSaving(false);

		Serializer.SetupArchive(*this);

		Object->Serialize(*this);
	}
	bool FDreamUIImmediateOverrideParameterObjectReader::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (DreamUIPrefab_ShouldSkipProperty(InProperty))
		{
			return true;
		}

		if (CurrentIsMemberProperty(*this))
		{
			if (OverridePropertyNames.Contains(InProperty->GetFName()))
			{
				return false;
			}
			else
			{
				return true;
			}
		}

		return false;
	}
	FString FDreamUIImmediateOverrideParameterObjectReader::GetArchiveName() const
	{
		return TEXT("FDreamUIImmediateOverrideParameterObjectReader");
	}
}
