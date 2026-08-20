// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIObjectReaderAndWriter.h"
#include "PrefabSystem/WidgetSerializerBase.h"
#include "Serialization/MemoryReader.h"
#include "Engine/Blueprint.h"

namespace DreamUIPrefabSystem
{
	FDreamUIDuplicateObjectWriter::FDreamUIDuplicateObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames)
		: FDreamUIObjectWriter(Bytes, InSerializer, InSkipPropertyNames)
	{
		
	}
	bool FDreamUIDuplicateObjectWriter::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (InProperty->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_DisableEditOnInstance)
			|| InProperty->IsA<FMulticastDelegateProperty>()
			|| InProperty->IsA<FDelegateProperty>()
			)
		{
			return true;
		}
		if (SkipPropertyNames.Contains(InProperty->GetFName())
			&& CurrentIsMemberProperty(*this)//Skip property only support UObject's member property
			)
		{
			return true;
		}

		return false;
	}
	bool FDreamUIDuplicateObjectWriter::SerializeObject(UObject* Object)
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
			bool canSerializeObject = false;
			FGuid guid;
			auto guidPtr = Serializer.MapObjectToGuid.Find(Object);
			if (guidPtr != nullptr)
			{
				canSerializeObject = true;
				guid = *guidPtr;
				//MapObjectToGuid could be passed-in, if that the CollectObjectToSerialize will not execute which will miss some objects. so we still need to collect objects to serialize
				Serializer.CollectObjectToSerialize(Object, guid);
			}
			else
			{
				canSerializeObject = Serializer.CollectObjectToSerialize(Object, guid);
			}

			if (canSerializeObject)//object belongs to this actor hierarchy
			{
				auto type = (uint8)EObjectType::ObjectReference;
				*this << type;
				*this << guid;
				return true;
			}
			else//object not belongs to this actor hierarchy, just copy pointer
			{
				auto type = (uint8)EObjectType::NativeSerializeForDuplicate;
				*this << type;
				ByteOrderSerialize(&Object, sizeof(Object));
				return true;
			}
		}
	}
	FString FDreamUIDuplicateObjectWriter::GetArchiveName() const
	{
		return TEXT("FDreamUIDuplicateObjectReader");
	}



	FDreamUIDuplicateObjectReader::FDreamUIDuplicateObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames)
		: FDreamUIObjectReader(Bytes, InSerializer, InSkipPropertyNames)
	{

	}
	bool FDreamUIDuplicateObjectReader::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (InProperty->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_DisableEditOnInstance)
			|| InProperty->IsA<FMulticastDelegateProperty>()
			|| InProperty->IsA<FDelegateProperty>()
			)
		{
			return true;
		}
		if (SkipPropertyNames.Contains(InProperty->GetFName())
			&& CurrentIsMemberProperty(*this)//Skip property only support UObject's member property
			)
		{
			return true;
		}

		return false;
	}
	bool FDreamUIDuplicateObjectReader::SerializeObject(UObject*& Object, bool CanSerializeClass)
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
		case DreamUIPrefabSystem::EObjectType::NativeSerializeForDuplicate:
		{
			ByteOrderSerialize(&Object, sizeof(Object));
			return true;
		}
		break;
		}
		return false;
	}
	FString FDreamUIDuplicateObjectReader::GetArchiveName() const
	{
		return TEXT("FDreamUIDuplicateObjectReader");
	}
}
