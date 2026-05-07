// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PrefabSystem/ActorSerializerBase.h"
#include "LexUIPrefab.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"

namespace LexUIPrefabSystem
{
	struct FLexUICommonObjectSaveData
	{
	public:
		int32 ObjectClass = -1;
		uint32 ObjectFlags;

		/** The following two array stores default sub objects which belong to this object. Array must match index for specific component. When deserialize, use FName to find FGuid. */
		TArray<FGuid> DefaultSubObjectGuidArray;
		TArray<FName> DefaultSubObjectNameArray;
	};

	struct FLexUIObjectSaveData : FLexUICommonObjectSaveData
	{
	public:
		FName ObjectName;
		FGuid OuterObjectGuid;//outer object

		friend FArchive& operator<<(FArchive& Ar, FLexUIObjectSaveData& ObjectData)
		{
			Ar << ObjectData.ObjectClass;
			Ar << ObjectData.ObjectFlags;

			Ar << ObjectData.DefaultSubObjectGuidArray;
			Ar << ObjectData.DefaultSubObjectNameArray;

			Ar << ObjectData.ObjectName;
			Ar << ObjectData.OuterObjectGuid;
			return Ar;
		}

		friend void operator<<(FStructuredArchive::FSlot Slot, FLexUIObjectSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("ObjectClass"), Data.ObjectClass);
			Record << SA_VALUE(TEXT("ObjectFlags"), Data.ObjectFlags);

			Record << SA_VALUE(TEXT("DefaultSubObjectGuidArray"), Data.DefaultSubObjectGuidArray);
			Record << SA_VALUE(TEXT("DefaultSubObjectNameArray"), Data.DefaultSubObjectNameArray);

			Record << SA_VALUE(TEXT("ObjectName"), Data.ObjectName);
			Record << SA_VALUE(TEXT("OuterObjectGuid"), Data.OuterObjectGuid);
		}
	};

	struct FLexUIPrefabOverrideParameterSaveData
	{
	public:
		TArray<uint8> OverrideParameterData;
		TArray<FName> OverrideParameterNames;
		friend FArchive& operator<<(FArchive& Ar, FLexUIPrefabOverrideParameterSaveData& Data)
		{
			Ar << Data.OverrideParameterData;
			Ar << Data.OverrideParameterNames;
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FLexUIPrefabOverrideParameterSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("OverrideObjectReferenceParameterData"), Data.OverrideParameterData);
			Record << SA_VALUE(TEXT("OverrideParameterNameSet"), Data.OverrideParameterNames);
		}
	};

	struct FLexUISubPrefabObjectUniqueIdSaveData
	{
	public:
		FGuid RootActorGuidInParentPrefab;
		FGuid ObjectGuidInOriginPrefab;

		bool operator==(const FLexUISubPrefabObjectUniqueIdSaveData& other)const
		{
			return this->RootActorGuidInParentPrefab == other.RootActorGuidInParentPrefab && this->ObjectGuidInOriginPrefab == other.ObjectGuidInOriginPrefab;
		}
		friend FORCEINLINE uint32 GetTypeHash(const FLexUISubPrefabObjectUniqueIdSaveData& other)
		{
			return HashCombine(GetTypeHash(other.RootActorGuidInParentPrefab), GetTypeHash(other.ObjectGuidInOriginPrefab));
		}

		friend FArchive& operator<<(FArchive& Ar, FLexUISubPrefabObjectUniqueIdSaveData& Data)
		{
			Ar << Data.RootActorGuidInParentPrefab;
			Ar << Data.ObjectGuidInOriginPrefab;
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FLexUISubPrefabObjectUniqueIdSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("RootActorGuidInParentPrefab"), Data.RootActorGuidInParentPrefab);
			Record << SA_VALUE(TEXT("ObjectGuidInOriginPrefab"), Data.ObjectGuidInOriginPrefab);
		}
	};

	//Actor serialize and save data
	struct FLexUIActorSaveData : FLexUICommonObjectSaveData
	{
	public:
		bool bIsPrefab = false;
		int32 PrefabAssetIndex;
		TMap<FGuid, FLexUIPrefabOverrideParameterSaveData> MapObjectGuidToSubPrefabOverrideParameter;//override sub prefab's parameter
		TMap<FLexUISubPrefabObjectUniqueIdSaveData, FGuid> MapObjectIdToNewlyCreatedId;
		TMap<FGuid, FGuid> MapObjectGuidFromParentPrefabToSubPrefab;//sub prefab's object use a different guid in parent prefab. So multiple same sub prefab can exist in same parent prefab.

		FGuid ActorGuid;

		friend FArchive& operator<<(FArchive& Ar, FLexUIActorSaveData& ActorData)
		{
			Ar << ActorData.bIsPrefab;
			if (ActorData.bIsPrefab)
			{
				Ar << ActorData.PrefabAssetIndex;
				Ar << ActorData.ActorGuid;//sub prefab's root actor's guid
				Ar << ActorData.MapObjectGuidToSubPrefabOverrideParameter;
				Ar << ActorData.MapObjectIdToNewlyCreatedId;
				Ar << ActorData.MapObjectGuidFromParentPrefabToSubPrefab;
			}
			else
			{
				Ar << ActorData.ActorGuid;
				Ar << ActorData.ObjectClass;
				Ar << ActorData.ObjectFlags;

				Ar << ActorData.DefaultSubObjectGuidArray;
				Ar << ActorData.DefaultSubObjectNameArray;
			}
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FLexUIActorSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("bIsPrefab"), Data.bIsPrefab);
			if (Data.bIsPrefab)
			{
				Record << SA_VALUE(TEXT("PrefabAssetIndex"), Data.PrefabAssetIndex);
				Record << SA_VALUE(TEXT("ActorGuid"), Data.ActorGuid);
				Record << SA_VALUE(TEXT("SubPrefabOverrideParameterArray"), Data.MapObjectGuidToSubPrefabOverrideParameter);
				Record << SA_VALUE(TEXT("MapObjectIdToNewlyCreatedId"), Data.MapObjectIdToNewlyCreatedId);
				Record << SA_VALUE(TEXT("MapObjectGuidFromParentPrefabToSubPrefab"), Data.MapObjectGuidFromParentPrefabToSubPrefab);
			}
			else
			{
				Record << SA_VALUE(TEXT("ActorGuid"), Data.ActorGuid);
				Record << SA_VALUE(TEXT("ObjectClass"), Data.ObjectClass);
				Record << SA_VALUE(TEXT("ObjectFlags"), Data.ObjectFlags);

				Record << SA_VALUE(TEXT("DefaultSubObjectGuidArray"), Data.DefaultSubObjectGuidArray);
				Record << SA_VALUE(TEXT("DefaultSubObjectNameArray"), Data.DefaultSubObjectNameArray);
			}
		}
	};

	struct FLexUIPrefabSaveData
	{
	public:
		TArray<FLexUIActorSaveData> SavedActors;
		TMap<FGuid, FLexUIObjectSaveData> SavedObjects;
		/** Key as child, value as parent. */
		TMap<FGuid, FGuid> MapSceneComponentToParent;
		/** Map guid to parameter data */
		TMap<FGuid, TArray<uint8>> SavedObjectData;

		friend FArchive& operator<<(FArchive& Ar, FLexUIPrefabSaveData& GameData)
		{
			Ar << GameData.SavedActors;
			Ar << GameData.SavedObjects;
			Ar << GameData.MapSceneComponentToParent;
			Ar << GameData.SavedObjectData;
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FLexUIPrefabSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("SavedActor"), Data.SavedActors);
			Record << SA_VALUE(TEXT("SavedObjects"), Data.SavedObjects);
			Record << SA_VALUE(TEXT("MapSceneComponentToParent"), Data.MapSceneComponentToParent);
			Record << SA_VALUE(TEXT("SavedObjectReferences"), Data.SavedObjectData);
		}
	};

	struct FDuplicateActorDataContainer;

	/*
	 * serialize/deserialize actor with hierarchy.
	 */
	class LGUI_API ActorSerializer : public LexUIPrefabSystem::ActorSerializerBase
	{
	public:
		/**
		 * @param CallbackBeforeAwake	This callback function will execute before Awake event, parameter "Actor" is the loaded root actor.
		 */
		static ULexWidget* LoadPrefab(UWorld* InWorld, ULexUIPrefab* InPrefab, ULexWidget* Parent, bool SetRelativeTransformToIdentity = true, TFunction<void(ULexWidget*)> CallbackBeforeAwake = nullptr);
		/**
		 * @param CallbackBeforeAwake	This callback function will execute before Awake event, parameter "Actor" is the loaded root actor.
		 */
		static ULexWidget* LoadPrefab(UWorld* InWorld, ULexUIPrefab* InPrefab, ULexWidget* Parent, FVector RelativeLocation, FQuat RelativeRotation, FVector RelativeScale, TFunction<void(ULexWidget*)> CallbackBeforeAwake = nullptr);
		/**
		 * LoadPrefab and keep reference of objects.
		 */
		static ULexWidget* LoadPrefabWithExistingObjects(UWorld* InWorld, ULexUIPrefab* InPrefab, ULexWidget* Parent
			, TMap<FGuid, TObjectPtr<UObject>>& InOutMapGuidToObjects, TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& OutSubPrefabMap
		);

		/** Save prefab data for editor use.
		 * @return If save prefab success
		 */
		static bool SavePrefab(ULexWidget* RootActor, ULexUIPrefab* InPrefab
			, TMap<UObject*, FGuid>& OutMapObjectToGuid, TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& InSubPrefabMap
			, bool InForEditorOrRuntimeUse
		);
		
		/**
		 * Duplicate actor with hierarchy
		 */
		static ULexWidget* DuplicateActor(UObject* InOwnerObject, ULexWidget* OriginRootActor, ULexWidget* Parent);
		/** Prepare one data and duplicate multiple times */
		static bool PrepareDataForDuplicate(ULexWidget* RootActor, FDuplicateActorDataContainer& OutData);
		static ULexWidget* DuplicateActorWithPreparedData(FDuplicateActorDataContainer& InData, ULexWidget* InParent);
		/**
		 * Editor version, duplicate actor with hierarchy, will also concern sub prefab.
		 */
		static ULexWidget* DuplicateActorForEditor(ULexWidget* OriginRootWidget, ULexWidget* Parent
			, const TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& InSubPrefabMap
			, const TMap<UObject*, FGuid>& InMapObjectToGuid
			, TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& OutDuplicatedSubPrefabMap
			, TMap<FGuid, TObjectPtr<UObject>>& OutMapGuidToObject
		);

		static ULexWidget* LoadSubPrefab(
			UObject* InOwnerObject, ULexUIPrefab* InPrefab, ULexWidget* Parent
			, const FGuid& InParentDeserializationSessionId
			, TMap<FGuid, TObjectPtr<UObject>>& InMapGuidToObject
			, const TFunction<void(ULexWidget*, const TMap<FGuid, TObjectPtr<UObject>>&, const TMap<TObjectPtr<UObject>, FGuid>&, const TArray<ULexWidget*>&, const TArray<UActorComponent*>&)>& InOnSubPrefabFinishDeserializeFunction
		);

	private:
		struct FComponentDataStruct
		{
			UActorComponent* Component = nullptr;
			FGuid SceneComponentParentGuid;
		};
		TArray<FComponentDataStruct> ComponentsInThisPrefab;
		//include components in sub-prefab and sub-prefab's sub-prefab...
		TArray<UActorComponent*> AllComponents;
		//collection for all actors, include sub-prefab
		TArray<ULexWidget*> AllWidgets;

		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubPrefabMap;
		TArray<ULexWidget*> SubPrefabActorArray;
		TArray<FComponentDataStruct> SubPrefabRootComponents;
		//this collection will collect all actors of this prefab, and root actor of sub prefab
		TArray<ULexWidget*> TrySerializeActorArray;
		//origin guid mean the object guid in it's origin prefab, not sub prefab
		TMap<TObjectPtr<UObject>, FGuid> MapObjectToOriginGuid;

		void CollectWidgetRecursive(ULexWidget* Widget);

		struct FSubPrefabObjectOverrideParameterData
		{
			UObject* Object = nullptr;
			TArray<uint8> ParameterDatas;
			TArray<FName> ParameterNames;
		};
		TArray<FSubPrefabObjectOverrideParameterData> SubPrefabOverrideParameters;

		//serialize actor
		bool SerializeActor(ULexWidget* RootActor, ULexUIPrefab* InPrefab);
		void SerializeActorArray(TMap<FGuid, FGuid>& MapWidgetToParent, TArray<FLexUIActorSaveData>& SavedActors, TMap<FGuid, TArray<uint8>>& SavedObjectData);
		void SerializeObjectArray(TMap<FGuid, FLexUIObjectSaveData>& ObjectSaveDataArray, TMap<FGuid, TArray<uint8>>& SavedObjectData, TMap<FGuid, FGuid>& MapSceneComponentToParent);
		void SerializeActorToData(ULexWidget* RootActor, FLexUIPrefabSaveData& OutData);
		//deserialize actor
		ULexWidget* DeserializeActor(ULexWidget* Parent, ULexUIPrefab* InPrefab, const TFunction<void()>& InCallbackBeforeDeserialize, bool ReplaceTransform = false, FVector InLocation = FVector::ZeroVector, FQuat InRotation = FQuat::Identity, FVector InScale = FVector::OneVector);
		ULexWidget* DeserializeActorFromData(FLexUIPrefabSaveData& SaveData, ULexWidget* Parent, bool ReplaceTransform, FVector InLocation, FQuat InRotation, FVector InScale);
		ULexWidget* GenerateActorArray(TArray<FLexUIActorSaveData>& SavedActors, TMap<FGuid, FLexUIObjectSaveData>& InSavedObjects, TMap<FGuid, FGuid>& MapSceneComponentToParent, FGuid ParentGuid);
		void GenerateObjectArray(TMap<FGuid, FLexUIObjectSaveData>& SavedObjects, TMap<FGuid, FGuid>& MapSceneComponentToParent);

		/** Mark of this deserialization session. If nested prefab, this is still the root prefab's value. */
		FGuid DeserializationSessionId = FGuid();
		bool bIsSubPrefab = false;
		/** A temporary string for log if loading or saving prefab (not duplicate). */
		FString PrefabAssetPath;
		
		struct FSubPrefabObjectOverideData
		{
			UObject* Object;
			TArray<uint8> Data;
			TArray<FName> Names;
		};
		/** Store sub-prefab's override data(object reference), after all object is generated then restore it. */
		TArray<FSubPrefabObjectOverideData> SubPrefabObjectOverrideData;

		TFunction<void(ULexWidget*)> CallbackBeforeAwake = nullptr;

		/**
		 * @param	ULexWidget*		SubPrefab's root actor
		 * @param	const TMap<FGuid, UObject*>&	SubPrefab's map guid to all object
		 * @param	const TArray<ULexWidget*>&		SubPrefab's all created actor
		 */
		TFunction<void(ULexWidget*, const TMap<FGuid, TObjectPtr<UObject>>&, const TMap<TObjectPtr<UObject>, FGuid>&, const TArray<ULexWidget*>&, const TArray<UActorComponent*>&)> OnSubPrefabFinishDeserializeFunction = nullptr;

		/**
		 * Writer and Reader for serialize or deserialize
		 * @param	UObject*	Object to serialize/deserialize
		 * @param	TArray<uint8>&	Data buffer
		 * @param	bool	is SceneComponent
		 */
		TFunction<void(UObject*, TArray<uint8>&, bool)> WriterOrReaderFunction = nullptr;
		/**
		 * Writer and Reader for serialize or deserialize
		 * @param	UObject*	Object to serialize/deserialize
		 * @param	TArray<uint8>&	Data buffer
		 * @param	TArray<FName>&	Member properties to filter
		 */
		TFunction<void(UObject*, TArray<uint8>&, const TArray<FName>&)> WriterOrReaderFunctionForSubPrefabOverride = nullptr;
	};

	struct FDuplicateActorDataContainer
	{
		FLexUIPrefabSaveData ActorData;
		ActorSerializer Serializer;
	};
}