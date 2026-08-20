// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIObjectReaderAndWriter.h"
#include "PrefabSystem/WidgetSerializerBase.h"
#include "Core/Components/DreamWidget.h"
#include "Serialization/MemoryReader.h"
#include "Engine/Blueprint.h"

namespace DreamUIPrefabSystem
{
	bool DreamUIPrefab_ShouldSkipProperty(const FProperty* InProperty)
	{
		return
			InProperty->HasAnyPropertyFlags(CPF_Transient | CPF_NonPIEDuplicateTransient | CPF_DisableEditOnInstance)
			|| InProperty->IsA<FMulticastDelegateProperty>()
			|| InProperty->IsA<FDelegateProperty>()
			;
	}

	FDreamUIObjectWriter::FDreamUIObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames)
		: FObjectWriter(Bytes)
		, Serializer(InSerializer)
		, SkipPropertyNames(InSkipPropertyNames)
	{
		SetIsLoading(false);
		SetIsSaving(true);

		Serializer.SetupArchive(*this);
	}
	void FDreamUIObjectWriter::DoSerialize(UObject* Object)
	{
		Object->Serialize(*this);
	}
	bool FDreamUIObjectWriter::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (DreamUIPrefab_ShouldSkipProperty(InProperty))
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
	FArchive& FDreamUIObjectWriter::operator<<(FName& N)
	{
		auto id = Serializer.FindOrAddNameFromList(N);
		*this << id;

		return *this;
	}

	FArchive& FDreamUIObjectWriter::operator<<(FText& Value)
	{
#if WITH_EDITOR
		if (Serializer.PrefabVersion < (uint16)EDreamUIPrefabVersion::FTextAsReference)
		{
			return FArchive::operator<<(Value);
		}
		else
#endif
		{
			auto id = Serializer.FindOrAddTextFromList(Value);
			*this << id;
			return *this;
		}
	}

	bool FDreamUIObjectWriter::SerializeObject(UObject* Object)
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
			if (Object->IsA<UDreamWidget>())
			{
				if (Serializer.WillSerializeWidgetArray.Contains(CastChecked<UDreamWidget>(Object)))
				{
					if (const FGuid* GuidPtr = Serializer.MapObjectToGuid.Find(Object))
					{
						guid = *GuidPtr;
						canSerializeObject = true;
					}
				}
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
			else
			{
				return false;
			}
		}
	}
	FArchive& FDreamUIObjectWriter::operator<<(UObject*& Res)
	{
		if (Res != nullptr)
		{
			auto Property = this->GetSerializedProperty();
			if (CastField<FClassProperty>(Property) != nullptr)//class property
			{
				auto id = Serializer.FindOrAddClassFromList((UClass*)Res);
				auto type = (uint8)EObjectType::Class;
				*this << type;
				*this << id;
				return *this;
			}
			else
			{
				if (SerializeObject(Res))
				{
					return *this;
				}
			}
		}

		auto noneType = (uint8)EObjectType::None;
		*this << noneType;

		return *this;
	}
	FArchive& FDreamUIObjectWriter::operator<<(FObjectPtr& Value)
	{
		auto Res = Value.Get();
		if (Res != nullptr)
		{
			auto Property = this->GetSerializedProperty();
			if (CastField<FClassProperty>(Property) != nullptr)//class property
			{
				auto id = Serializer.FindOrAddClassFromList((UClass*)Res);
				auto type = (uint8)EObjectType::Class;
				*this << type;
				*this << id;
				return *this;
			}
			else
			{
				if (SerializeObject(Res))
				{
					return *this;
				}
			}
		}

		auto noneType = (uint8)EObjectType::None;
		*this << noneType;

		return *this;
	}
	FArchive& FDreamUIObjectWriter::operator<<(FWeakObjectPtr& Value)
	{
		if (Value.IsValid())
		{
			//no need to concern UClass, because UClass cannot use weakptr
			if (SerializeObject(Value.Get()))
			{
				return *this;
			}
		}
		auto noneType = (uint8)EObjectType::None;
		*this << noneType;

		return *this;
	}
	FArchive& FDreamUIObjectWriter::operator<<(FLazyObjectPtr& Value)
	{
		return FObjectWriter::operator<<(Value);
	}
	FArchive& FDreamUIObjectWriter::operator<<(FSoftObjectPtr& Value)
	{
		return FObjectWriter::operator<<(Value);
	}
	FArchive& FDreamUIObjectWriter::operator<<(FSoftObjectPath& Value)
	{
		return FObjectWriter::operator<<(Value);
	}
	FString FDreamUIObjectWriter::GetArchiveName() const
	{
		return TEXT("FDreamUIObjectWriter");
	}


	FDreamUIObjectReader::FDreamUIObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames)
		: FObjectReader(Bytes)
		, Serializer(InSerializer)
		, SkipPropertyNames(InSkipPropertyNames)
	{
		SetIsLoading(true);
		SetIsSaving(false);

		Serializer.SetupArchive(*this);
	}
	void FDreamUIObjectReader::DoSerialize(UObject* Object)
	{
		Object->Serialize(*this);
	}
	bool FDreamUIObjectReader::ShouldSkipProperty(const FProperty* InProperty) const
	{
		if (DreamUIPrefab_ShouldSkipProperty(InProperty))
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
	FArchive& FDreamUIObjectReader::operator<<(FName& N)
	{
		int32 id = -1;
		*this << id;
		N = Serializer.FindNameFromListByIndex(id);

		return *this;
	}

	FArchive& FDreamUIObjectReader::operator<<(FText& Value)
	{
#if WITH_EDITOR
		if (Serializer.PrefabVersion < (uint16)EDreamUIPrefabVersion::FTextAsReference)
		{
			return FArchive::operator<<(Value);
		}
		else
#endif
		{
			int32 id = -1;
			*this << id;
			Value = Serializer.FindTextFromListByIndex(id);
			
			return *this;
		}
	}

	bool FDreamUIObjectReader::SerializeObject(UObject*& Object, bool CanSerializeClass)
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
	FArchive& FDreamUIObjectReader::operator<<(UObject*& Value)
	{
		UObject* Res = nullptr;
		if (SerializeObject(Res, true))
		{
			Value = Res;
		}
		return *this;
	}
	FArchive& FDreamUIObjectReader::operator<<(FObjectPtr& Value)
	{
		UObject* Res = nullptr;
		if (SerializeObject(Res, true))
		{
			Value = Res;
		}
		return *this;
	}
	FArchive& FDreamUIObjectReader::operator<<(FWeakObjectPtr& Value)
	{
		UObject* Res = nullptr;
		if (SerializeObject(Res, false))
		{
			Value = Res;
		}
		return *this;
	}
	FArchive& FDreamUIObjectReader::operator<<(FLazyObjectPtr& Value)
	{
		return FObjectReader::operator<<(Value);
	}
	FArchive& FDreamUIObjectReader::operator<<(FSoftObjectPtr& Value)
	{
		return FObjectReader::operator<<(Value);
	}
	FArchive& FDreamUIObjectReader::operator<<(FSoftObjectPath& Value)
	{
		return FObjectReader::operator<<(Value);
	}
	FString FDreamUIObjectReader::GetArchiveName() const
	{
		return TEXT("FDreamUIObjectReader");
	}
}
