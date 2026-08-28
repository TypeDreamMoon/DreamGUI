// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"

namespace DreamUIPrefabSystem
{
	class WidgetSerializerBase;

	enum class EObjectType :uint8
	{
		None,
		/** Asset resource */
		Asset,
		/** UClass */
		Class,
		/** UObject reference(Not asset), include actor/ component/ uobject */
		ObjectReference,
		/** Only for duplicate, use native ObjectWriter/ObjectReader serialization method */
		NativeSerializeForDuplicate,
	};

	/** 
	 * If not have valid property chain, then it is member property.
	 * Why use a template instead of FArchiveState? Because FArchiveState's construcion is prirvate, I can't convert FDreamGUIObjectWriterXXX to FArchiveState.
	 */
	template<class T>
	bool CurrentIsMemberProperty(const T& t)
	{
		auto PropertyChain = t.GetSerializedPropertyChain();
		if (PropertyChain == nullptr || PropertyChain->GetNumProperties() == 0)
		{
			return true;
		}
		return false;
	}
	/**
	 * UDreamWidget::Children: the hierarchy itself, which every prefab path carries separately (as
	 * FDreamUIPrefabSaveData::MapWidgetToParent) and replays through SetParentBeforeRegister.
	 *
	 * It used to be excluded for free by being Transient. It stopped being Transient when it became the
	 * persistent, Instanced record of the hierarchy for the class model -- so every archive that walks a
	 * widget's properties has to exclude it deliberately now, or load attaches every child twice: once
	 * from the restored array, once from the attach pass. Shared rather than repeated because the
	 * duplicate and override archives each roll their own filter.
	 */
	bool DreamUIPrefab_IsHierarchyProperty(const FProperty* InProperty);
	bool DreamUIPrefab_ShouldSkipProperty(const FProperty* InProperty);

	class DREAMGUI_API FDreamUIObjectWriter : public FObjectWriter
	{
	public:
		FDreamUIObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames);
		virtual void DoSerialize(UObject* Object);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FArchive& operator<<(FName& N) override;
		virtual FArchive& operator<<(FText& Value) override;
		virtual FArchive& operator<<(UObject*& Res) override;
		virtual FArchive& operator<<(FObjectPtr& Value) override;
		virtual FArchive& operator<<(FWeakObjectPtr& Value) override;
		virtual FArchive& operator<<(FLazyObjectPtr& Value) override;
		virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
		virtual FArchive& operator<<(FSoftObjectPath& Value) override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject* Object);
	protected:
		WidgetSerializerBase& Serializer;
		TSet<FName> SkipPropertyNames;
	};
	class DREAMGUI_API FDreamUIObjectReader : public FObjectReader
	{
	public:
		FDreamUIObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames);
		virtual void DoSerialize(UObject* Object);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FArchive& operator<<(FName& N) override;
		virtual FArchive& operator<<(FText& Value) override;
		virtual FArchive& operator<<(UObject*& Res) override;
		virtual FArchive& operator<<(FObjectPtr& Value) override;
		virtual FArchive& operator<<(FWeakObjectPtr& Value) override;
		virtual FArchive& operator<<(FLazyObjectPtr& Value) override;
		virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
		virtual FArchive& operator<<(FSoftObjectPath& Value) override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject*& Object, bool CanSerializeClass);
	protected:
		WidgetSerializerBase& Serializer;
		TSet<FName> SkipPropertyNames;
	};

	class DREAMGUI_API FDreamUIDuplicateObjectWriter : public FDreamUIObjectWriter
	{
	public:
		FDreamUIDuplicateObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject* Object)override;
	};
	class DREAMGUI_API FDreamUIDuplicateObjectReader : public FDreamUIObjectReader
	{
	public:
		FDreamUIDuplicateObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, TSet<FName> InSkipPropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject*& Object, bool CanSerializeClass)override;
	};



	class DREAMGUI_API FDreamUIOverrideParameterObjectWriter : public FDreamUIObjectWriter
	{
	public:
		FDreamUIOverrideParameterObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject* Object);
	protected:
		mutable TSet<FName> OverridePropertyNames;
	};
	class DREAMGUI_API FDreamUIOverrideParameterObjectReader : public FDreamUIObjectReader
	{
	public:
		FDreamUIOverrideParameterObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject*& Object, bool CanSerializeClass);
	protected:
		mutable TSet<FName> OverridePropertyNames;
	};


	class DREAMGUI_API FDreamUIDuplicateOverrideParameterObjectWriter : public FDreamUIOverrideParameterObjectWriter
	{
	public:
		FDreamUIDuplicateOverrideParameterObjectWriter(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject* Object)override;
	};
	class DREAMGUI_API FDreamUIDuplicateOverrideParameterObjectReader : public FDreamUIOverrideParameterObjectReader
	{
	public:
		FDreamUIDuplicateOverrideParameterObjectReader(TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
		virtual bool SerializeObject(UObject*& Object, bool CanSerializeClass)override;
	};


	class DREAMGUI_API FDreamUIImmediateOverrideParameterObjectWriter : public FObjectWriter
	{
	public:
		FDreamUIImmediateOverrideParameterObjectWriter(UObject* Object, TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
	private:
		mutable TSet<FName> OverridePropertyNames;
	};
	class DREAMGUI_API FDreamUIImmediateOverrideParameterObjectReader : public FObjectReader
	{
	public:
		FDreamUIImmediateOverrideParameterObjectReader(UObject* Object, TArray< uint8 >& Bytes, WidgetSerializerBase& InSerializer, const TArray<FName>& InOverridePropertyNames);

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
		virtual FString GetArchiveName() const override;
	private:
		mutable TSet<FName> OverridePropertyNames;
	};
}