// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "PrefabSystem/WidgetSerializerBase.h"
#include "DreamUIPrefab.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"

namespace DreamUIPrefabSystem
{
	/**
	 * A nested prefab root gets its panel slot from the parent prefab, so the
	 * object has no source GUID in the nested prefab asset. Use a reserved
	 * origin GUID to persist that parent-owned attachment object.
	 */
	inline const FGuid& GetSubPrefabRootPanelSlotOriginGuid()
	{
		static const FGuid Guid(0x4C455850, 0x414E454C, 0x534C4F54, 0x00000001);
		return Guid;
	}

	struct FDreamUICommonObjectSaveData
	{
	public:
		int32 ObjectClass = -1;
		uint32 ObjectFlags;

		/** The following two array stores default sub objects which belong to this object. Array must match index for specific component. When deserialize, use FName to find FGuid. */
		TArray<FGuid> DefaultSubObjectGuidArray;
		TArray<FName> DefaultSubObjectNameArray;
	};

	struct FDreamUIObjectSaveData : FDreamUICommonObjectSaveData
	{
	public:
		FName ObjectName;
		FGuid OuterObjectGuid;//outer object

		friend FArchive& operator<<(FArchive& Ar, FDreamUIObjectSaveData& ObjectData)
		{
			Ar << ObjectData.ObjectClass;
			Ar << ObjectData.ObjectFlags;

			Ar << ObjectData.DefaultSubObjectGuidArray;
			Ar << ObjectData.DefaultSubObjectNameArray;

			Ar << ObjectData.ObjectName;
			Ar << ObjectData.OuterObjectGuid;
			return Ar;
		}

		friend void operator<<(FStructuredArchive::FSlot Slot, FDreamUIObjectSaveData& Data)
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

	struct FDreamUIPrefabOverrideParameterSaveData
	{
	public:
		TArray<uint8> OverrideParameterData;
		TArray<FName> OverrideParameterNames;
		friend FArchive& operator<<(FArchive& Ar, FDreamUIPrefabOverrideParameterSaveData& Data)
		{
			Ar << Data.OverrideParameterData;
			Ar << Data.OverrideParameterNames;
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FDreamUIPrefabOverrideParameterSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("OverrideObjectReferenceParameterData"), Data.OverrideParameterData);
			Record << SA_VALUE(TEXT("OverrideParameterNameSet"), Data.OverrideParameterNames);
		}
	};

	struct FDreamUISubPrefabObjectUniqueIdSaveData
	{
	public:
		FGuid RootWidgetGuidInParentPrefab;
		FGuid ObjectGuidInOriginPrefab;

		bool operator==(const FDreamUISubPrefabObjectUniqueIdSaveData& other)const
		{
			return this->RootWidgetGuidInParentPrefab == other.RootWidgetGuidInParentPrefab && this->ObjectGuidInOriginPrefab == other.ObjectGuidInOriginPrefab;
		}
		friend FORCEINLINE uint32 GetTypeHash(const FDreamUISubPrefabObjectUniqueIdSaveData& other)
		{
			return HashCombine(GetTypeHash(other.RootWidgetGuidInParentPrefab), GetTypeHash(other.ObjectGuidInOriginPrefab));
		}

		friend FArchive& operator<<(FArchive& Ar, FDreamUISubPrefabObjectUniqueIdSaveData& Data)
		{
			Ar << Data.RootWidgetGuidInParentPrefab;
			Ar << Data.ObjectGuidInOriginPrefab;
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FDreamUISubPrefabObjectUniqueIdSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("RootWidgetGuidInParentPrefab"), Data.RootWidgetGuidInParentPrefab);
			Record << SA_VALUE(TEXT("ObjectGuidInOriginPrefab"), Data.ObjectGuidInOriginPrefab);
		}
	};

	//Widget serialize and save data
	struct FDreamUIWidgetSaveData : FDreamUICommonObjectSaveData
	{
	public:
		bool bIsPrefab = false;
		int32 PrefabAssetIndex;
		TMap<FGuid, FDreamUIPrefabOverrideParameterSaveData> MapObjectGuidToSubPrefabOverrideParameter;//override sub prefab's parameter
		TMap<FDreamUISubPrefabObjectUniqueIdSaveData, FGuid> MapObjectIdToNewlyCreatedId;
		TMap<FGuid, FGuid> MapObjectGuidFromParentPrefabToSubPrefab;//sub prefab's object use a different guid in parent prefab. So multiple same sub prefab can exist in same parent prefab.

		FGuid WidgetGuid;

		friend FArchive& operator<<(FArchive& Ar, FDreamUIWidgetSaveData& WidgetData)
		{
			Ar << WidgetData.bIsPrefab;
			if (WidgetData.bIsPrefab)
			{
				Ar << WidgetData.PrefabAssetIndex;
				Ar << WidgetData.WidgetGuid;//sub prefab's root actor's guid
				Ar << WidgetData.MapObjectGuidToSubPrefabOverrideParameter;
				Ar << WidgetData.MapObjectIdToNewlyCreatedId;
				Ar << WidgetData.MapObjectGuidFromParentPrefabToSubPrefab;
			}
			else
			{
				Ar << WidgetData.WidgetGuid;
				Ar << WidgetData.ObjectClass;
				Ar << WidgetData.ObjectFlags;

				Ar << WidgetData.DefaultSubObjectGuidArray;
				Ar << WidgetData.DefaultSubObjectNameArray;
			}
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FDreamUIWidgetSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("bIsPrefab"), Data.bIsPrefab);
			if (Data.bIsPrefab)
			{
				Record << SA_VALUE(TEXT("PrefabAssetIndex"), Data.PrefabAssetIndex);
				Record << SA_VALUE(TEXT("WidgetGuid"), Data.WidgetGuid);
				Record << SA_VALUE(TEXT("SubPrefabOverrideParameterArray"), Data.MapObjectGuidToSubPrefabOverrideParameter);
				Record << SA_VALUE(TEXT("MapObjectIdToNewlyCreatedId"), Data.MapObjectIdToNewlyCreatedId);
				Record << SA_VALUE(TEXT("MapObjectGuidFromParentPrefabToSubPrefab"), Data.MapObjectGuidFromParentPrefabToSubPrefab);
			}
			else
			{
				Record << SA_VALUE(TEXT("WidgetGuid"), Data.WidgetGuid);
				Record << SA_VALUE(TEXT("ObjectClass"), Data.ObjectClass);
				Record << SA_VALUE(TEXT("ObjectFlags"), Data.ObjectFlags);

				Record << SA_VALUE(TEXT("DefaultSubObjectGuidArray"), Data.DefaultSubObjectGuidArray);
				Record << SA_VALUE(TEXT("DefaultSubObjectNameArray"), Data.DefaultSubObjectNameArray);
			}
		}
	};

	struct FDreamUIPrefabSaveData
	{
	public:
		TArray<FDreamUIWidgetSaveData> SavedWidgets;
		TMap<FGuid, FDreamUIObjectSaveData> SavedObjects;
		/** Key as child, value as parent. */
		TMap<FGuid, FGuid> MapWidgetToParent;
		/** Map guid to parameter data */
		TMap<FGuid, TArray<uint8>> SavedObjectData;

		friend FArchive& operator<<(FArchive& Ar, FDreamUIPrefabSaveData& GameData)
		{
			Ar << GameData.SavedWidgets;
			Ar << GameData.SavedObjects;
			Ar << GameData.MapWidgetToParent;
			Ar << GameData.SavedObjectData;
			return Ar;
		}
		friend void operator<<(FStructuredArchive::FSlot Slot, FDreamUIPrefabSaveData& Data)
		{
			FStructuredArchive::FRecord Record = Slot.EnterRecord();
			Record << SA_VALUE(TEXT("SavedWidgets"), Data.SavedWidgets);
			Record << SA_VALUE(TEXT("SavedObjects"), Data.SavedObjects);
			Record << SA_VALUE(TEXT("MapSceneComponentToParent"), Data.MapWidgetToParent);
			Record << SA_VALUE(TEXT("SavedObjectReferences"), Data.SavedObjectData);
		}
	};

	struct FDuplicateWidgetDataContainer;

	/*
	 * serialize/deserialize actor with hierarchy.
	 */
	class DREAMGUI_API WidgetSerializer : public DreamUIPrefabSystem::WidgetSerializerBase
	{
	public:
#if WITH_EDITOR
		/**
		 * Move an object that is squatting on a name the deserializer needs out of the way, and tear
		 * it down. Renaming is the part that matters and therefore goes first: it is what frees the
		 * name, and it is not safe once an object has begun destruction -- UObject::Rename ends in
		 * UnhashObject, which fatals with a hash consistency failure on an object that is no longer
		 * hashed. Objects already on their way out are left alone entirely.
		 * @return true if the name is free afterwards.
		 */
		static bool ReleaseNameFromExistingObject(UObject* InExistingObject);
#endif
		/**
		 * @param CallbackBeforeAwake	This callback function will execute before Awake event, parameter "DreamWidget" is the loaded root widget.
		 */
		static UDreamWidget* LoadPrefab(UWorld* InWorld, UObject* InOuter, UDreamUIPrefab* InPrefab, UDreamWidget* Parent, bool SetRelativeTransformToIdentity = true, TFunction<void(UDreamWidget*)> CallbackBeforeAwake = nullptr);
		/**
		 * @param CallbackBeforeAwake	This callback function will execute before Awake event, parameter "DreamWidget" is the loaded root widget.
		 */
		static UDreamWidget* LoadPrefab(UWorld* InWorld, UObject* InOuter, UDreamUIPrefab* InPrefab, UDreamWidget* Parent, FVector RelativeLocation, FQuat RelativeRotation, FVector RelativeScale, TFunction<void(UDreamWidget*)> CallbackBeforeAwake = nullptr);
		/**
		 * LoadPrefab and keep reference of objects.
		 */
		static UDreamWidget* LoadPrefabWithExistingObjects(UWorld* InWorld, UObject* InOuter, UDreamUIPrefab* InPrefab, UDreamWidget* Parent
			, TMap<FGuid, TObjectPtr<UObject>>& InOutMapGuidToObjects, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& OutSubPrefabMap
		);

		/** Save prefab data for editor use.
		 * @return If save prefab success
		 */
		static bool SavePrefab(UDreamWidget* RootWidget, UDreamUIPrefab* InPrefab
			, TMap<UObject*, FGuid>& OutMapObjectToGuid, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap
			, bool InForEditorOrRuntimeUse
		);
		
		/**
		 * Duplicate widget with hierarchy
		 */
		static UDreamWidget* DuplicateWidget(UWorld* InWorld, UObject* InOwnerObject, UDreamWidget* OriginRootWidget, UDreamWidget* Parent);
		/** Prepare one data and duplicate multiple times */
		static bool PrepareDataForDuplicate(UDreamWidget* RootWidget, FDuplicateWidgetDataContainer& OutData);
		static UDreamWidget* DuplicateWidgetWithPreparedData(UWorld* InWorld, UObject* InOwnerObject, FDuplicateWidgetDataContainer& InData, UDreamWidget* InParent);
		/**
		 * Editor version, duplicate widget with hierarchy, will also concern sub prefab.
		 */
		static UDreamWidget* DuplicateWidgetForEditor(UWorld* InWorld, UDreamWidget* OriginRootWidget, UDreamWidget* Parent
			, const TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap
			, const TMap<UObject*, FGuid>& InMapObjectToGuid
			, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& OutDuplicatedSubPrefabMap
			, TMap<FGuid, TObjectPtr<UObject>>& OutMapGuidToObject
		);

		static UDreamWidget* LoadSubPrefab(UWorld* InWorld,
			UObject* InOwnerObject, UDreamUIPrefab* InPrefab, UDreamWidget* Parent
			, TMap<FGuid, TObjectPtr<UObject>>& InMapGuidToObject
			, const TFunction<void(UDreamWidget*, const TMap<FGuid, TObjectPtr<UObject>>&, const TMap<TObjectPtr<UObject>, FGuid>&, const TArray<UDreamWidget*>&)>& InOnSubPrefabFinishDeserializeFunction
		);

	private:
		struct FWidgetAttachment
		{
			UDreamWidget* Widget = nullptr;
			FGuid ParentGuid;
		};
		TArray<FWidgetAttachment> WidgetAttachmentArray;
		//collection for all widgets, include sub-prefab
		TArray<UDreamWidget*> AllWidgetArray;

		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
		TArray<UDreamWidget*> SubPrefabWidgetArray;
		//this collection will collect all widgets of this prefab, and root widget of sub prefab
		TArray<UDreamWidget*> TrySerializeWidgetArray;
		//origin guid mean the object guid in it's origin prefab, not sub prefab
		TMap<TObjectPtr<UObject>, FGuid> MapObjectToOriginGuid;

		void CollectWidgetRecursive(UDreamWidget* Widget, TSet<const UDreamWidget*>& VisitedWidgets);

		struct FSubPrefabObjectOverrideParameterData
		{
			UObject* Object = nullptr;
			TArray<uint8> ParameterDatas;
			TArray<FName> ParameterNames;
		};
		TArray<FSubPrefabObjectOverrideParameterData> SubPrefabOverrideParameters;

		struct FSubPrefabRootPanelSlotOverrideData
		{
			UDreamWidget* RootWidget = nullptr;
			FGuid RootWidgetGuid;
			FGuid PanelSlotGuid;
			TArray<uint8> ParameterDatas;
			TArray<FName> ParameterNames;
		};
		TArray<FSubPrefabRootPanelSlotOverrideData> SubPrefabRootPanelSlotOverrides;

		//serialize widget
		bool SerializeWidget(UDreamWidget* RootWidget, UDreamUIPrefab* InPrefab);
		void SerializeWidgetArray(TMap<FGuid, FGuid>& MapWidgetToParent, TArray<FDreamUIWidgetSaveData>& SavedWidgets, TMap<FGuid, TArray<uint8>>& SavedObjectData);
		void SerializeObjectArray(TMap<FGuid, FDreamUIObjectSaveData>& ObjectSaveDataArray, TMap<FGuid, TArray<uint8>>& SavedObjectData);
		void SerializeWidgetToData(UDreamWidget* RootWidget, FDreamUIPrefabSaveData& OutData);
		//deserialize widget
		UDreamWidget* DeserializeWidget(UDreamWidget* Parent, UDreamUIPrefab* InPrefab, const TFunction<void()>& InCallbackBeforeDeserialize, bool ReplaceTransform = false, FVector InLocation = FVector::ZeroVector, FQuat InRotation = FQuat::Identity, FVector InScale = FVector::OneVector);
		UDreamWidget* DeserializeWidgetFromData(FDreamUIPrefabSaveData& SaveData, UDreamWidget* Parent, bool ReplaceTransform, FVector InLocation, FQuat InRotation, FVector InScale);
		UDreamWidget* GenerateWidgetArray(TArray<FDreamUIWidgetSaveData>& SavedWidgets, TMap<FGuid, FDreamUIObjectSaveData>& InSavedObjects, TMap<FGuid, FGuid>& MapWidgetToParent, FGuid ParentGuid);
		void GenerateObjectArray(TMap<FGuid, FDreamUIObjectSaveData>& SavedObjects, TMap<FGuid, FGuid>& MapWidgetToParent);

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

		TFunction<void(UDreamWidget*)> CallbackBeforeAwake = nullptr;

		/**
		 * @param	UDreamWidget*		SubPrefab's root widget
		 * @param	const TMap<FGuid, UObject*>&	SubPrefab's map guid to all object
		 * @param	const TArray<UDreamWidget*>&		SubPrefab's all created widget
		 */
		TFunction<void(UDreamWidget*, const TMap<FGuid, TObjectPtr<UObject>>&, const TMap<TObjectPtr<UObject>, FGuid>&, const TArray<UDreamWidget*>&)> OnSubPrefabFinishDeserializeFunction = nullptr;

		/**
		 * Writer and Reader for serialize or deserialize
		 * @param	UObject*	Object to serialize/deserialize
		 * @param	TArray<uint8>&	Data buffer
		 */
		TFunction<void(UObject*, TArray<uint8>&)> WriterOrReaderFunction = nullptr;
		/**
		 * Writer and Reader for serialize or deserialize
		 * @param	UObject*	Object to serialize/deserialize
		 * @param	TArray<uint8>&	Data buffer
		 * @param	TArray<FName>&	Member properties to filter
		 */
		TFunction<void(UObject*, TArray<uint8>&, const TArray<FName>&)> WriterOrReaderFunctionForSubPrefabOverride = nullptr;
	};

	struct FDuplicateWidgetDataContainer
	{
		FDreamUIPrefabSaveData WidgetData;
		WidgetSerializer Serializer;
	};
}
