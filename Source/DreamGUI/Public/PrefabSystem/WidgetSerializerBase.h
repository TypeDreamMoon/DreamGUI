// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamUIPrefab.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "UObject/ObjectVersion.h"

class UDreamWidget;
class UDreamUIPrefabWorldSubsystem;

namespace DreamUIPrefabSystem
{
	/*
	 * serialize/deserialize actor with hierarchy
	 */
	class DREAMGUI_API WidgetSerializerBase
	{
		friend class FDreamUIObjectReader;
		friend class FDreamUIObjectWriter;
		friend class FDreamUIDuplicateObjectReader;
		friend class FDreamUIDuplicateObjectWriter;
		friend class FDreamUIOverrideParameterObjectWriter;
		friend class FDreamUIOverrideParameterObjectReader;
		friend class FDreamUIDuplicateOverrideParameterObjectWriter;
		friend class FDreamUIDuplicateOverrideParameterObjectReader;

	public:
		virtual ~WidgetSerializerBase() {}

		TMap<UObject*, TArray<uint8>> SaveOverrideParameterToData(TArray<FDreamUIPrefabOverrideParameterData> InData);
		void RestoreOverrideParameterFromData(TMap<UObject*, TArray<uint8>>& InData, TArray<FDreamUIPrefabOverrideParameterData> InNameSetData);

		virtual void SetupArchive(FArchive& InArchive);

		//Actor that belongs to this prefab. All UObjects which get outer of these actor can be serialized
		TArray<UDreamWidget*> WillSerializeWidgetArray;
		//Common UObjects that need to serialize. Outer object should stay at lower index then sub object, so when deserialize the outer object will created ealier, then the sub object can use the correct outer.
		TArray<UObject*> WillSerializeObjectArray;
		bool ObjectBelongsToThisPrefab(UObject* InObject);

		bool CollectObjectToSerialize(UObject* Object, FGuid& OutGuid);
		//Check object and it's up outer to tell if it is trash
		bool ObjectIsTrash(UObject* InObject);
		//find id from list, if not then create
		int32 FindOrAddAssetIdFromList(UObject* AssetObject);
		int32 FindOrAddClassFromList(UClass* Class);
		int32 FindOrAddNameFromList(const FName& Name);
		int32 FindOrAddTextFromList(const FText& Text);
		//find object by id
		UObject* FindAssetFromListByIndex(int32 Id);
		UClass* FindClassFromListByIndex(int32 Id);
		FName FindNameFromListByIndex(int32 Id);
		FText FindTextFromListByIndex(int32 Id);
		TArray<UObject*> ReferenceAssetList;
		TArray<UClass*> ReferenceClassList;
		TArray<FName> ReferenceNameList;
		TArray<FText> ReferenceTextList;

		bool bOverrideVersions = false;
		uint16 PrefabVersion = 0;
		FPackageFileVersion ArchiveVersion;
		int32 ArchiveLicenseeVer = 0;
		FEngineVersionBase ArEngineVer;
		uint32 ArEngineNetVer = 0;
		uint32 ArGameNetVer = 0;
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
		TMap<UObject*, FGuid> MapObjectToGuid;

	protected:
		UObject* OwnerObject = nullptr;//Outer object that hold our widget
		UWorld* World = nullptr;
		bool bIsEditorOrRuntime = true;
		static bool CanUseUnversionedPropertySerialization();
	};
}