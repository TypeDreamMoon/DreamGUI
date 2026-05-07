// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/ActorSerializer.h"
#include "PrefabSystem/LexUIObjectReaderAndWriter.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "PrefabSystem/LexUIPrefabManager.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"
#include "Misc/NetworkVersion.h"
#include "PrefabSystem/ILexUIPrefabInterface.h"
#include "Runtime/Launch/Resources/Version.h"


namespace LexUIPrefabSystem
{
	bool ActorSerializer::SavePrefab(ULexWidget* OriginRootActor, ULexUIPrefab* InPrefab
		, TMap<UObject*, FGuid>& InOutMapObjectToGuid, TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& InSubPrefabMap
		, bool InForEditorOrRuntimeUse
	)
	{
		if (!OriginRootActor || !InPrefab)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d OriginRootActor Or InPrefab is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (!IsValid(OriginRootActor))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d OriginRootActor is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (!OriginRootActor->GetWorld())
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Cannot get World from OriginRootActor!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (OriginRootActor->HasAnyFlags(EObjectFlags::RF_Transient))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d OriginRootActor is transient!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (!InForEditorOrRuntimeUse && OriginRootActor->IsEditorOnly())
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d OriginRootActor is editor only!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		ActorSerializer serializer;
		serializer.OwnerObject = OriginRootActor->GetWorld();
		for (auto& KeyValue : InOutMapObjectToGuid)//Preprocess the map, ignore invalid object
		{
			if (IsValid(KeyValue.Key))
			{
				serializer.MapObjectToGuid.Add(KeyValue.Key, KeyValue.Value);
			}
		}
		serializer.SubPrefabMap = InSubPrefabMap;
		for (auto& SubPrefabKeyValue : InSubPrefabMap)
		{
			for (auto& GuidToObjectKeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
			{
				if (auto SubPrefabActor = Cast<ULexWidget>(GuidToObjectKeyValue.Value))
				{
					serializer.SubPrefabActorArray.Add(SubPrefabActor);
				}
			}
		}
		serializer.bIsEditorOrRuntime = InForEditorOrRuntimeUse;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, bool InIsSceneComponent) {
			auto ExcludeProperties = InIsSceneComponent ? serializer.GetSceneComponentExcludeProperties() : TSet<FName>();
			LexUIPrefabSystem::FLexUIObjectWriter Writer(InOutBuffer, serializer, ExcludeProperties);
			Writer.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectWriter Writer(InOutBuffer, serializer, InOverridePropertyNames);
			Writer.DoSerialize(InObject);
		};
		bool saveResult = serializer.SerializeActor(OriginRootActor, InPrefab);
		InOutMapObjectToGuid = serializer.MapObjectToGuid;
		return saveResult;
	}

	void ActorSerializer::SerializeActorArray(TMap<FGuid, FGuid>& MapWidgetToParent, TArray<FLexUIActorSaveData>& SavedActors, TMap<FGuid, TArray<uint8>>& SavedObjectData)
	{
		for (int i = 0; i < TrySerializeActorArray.Num(); i++)
		{
			auto& Widget = TrySerializeActorArray[i];
			FLexUIActorSaveData ActorSaveData;
			if (auto SubPrefabDataPtr = SubPrefabMap.Find(Widget))//sub prefab's actor is not collected in WillSerializeActorArray
			{
				ActorSaveData.bIsPrefab = true;
				ActorSaveData.PrefabAssetIndex = FindOrAddAssetIdFromList(SubPrefabDataPtr->PrefabAsset);
				ActorSaveData.ActorGuid = MapObjectToGuid[Widget];
				ActorSaveData.MapObjectGuidFromParentPrefabToSubPrefab = SubPrefabDataPtr->MapObjectGuidFromParentPrefabToSubPrefab;

				//serialize override parameter data
				for (auto& DataItem : SubPrefabDataPtr->ObjectOverrideParameterArray)
				{
					TArray<uint8> SubPrefabOverrideData;
					auto SubPrefabObject = DataItem.Object.Get();
					if (MapObjectToGuid.Contains(SubPrefabObject))
					{
						FLexUIPrefabOverrideParameterSaveData RecordDataItem;
						RecordDataItem.OverrideParameterNames = DataItem.MemberPropertyNames;
						WriterOrReaderFunctionForSubPrefabOverride(SubPrefabObject, RecordDataItem.OverrideParameterData, DataItem.MemberPropertyNames);
						ActorSaveData.MapObjectGuidToSubPrefabOverrideParameter.Add(MapObjectToGuid[SubPrefabObject], RecordDataItem);
					}
				}
				for (auto& DataItem : SubPrefabDataPtr->MapObjectIdToNewlyCreatedId)
				{
					ActorSaveData.MapObjectIdToNewlyCreatedId.Add({ DataItem.Key.RootActorGuidInParentPrefab, DataItem.Key.ObjectGuidInOriginPrefab }, DataItem.Value);
				}

				if (auto Parent = Widget->GetParent())
				{
					if (MapObjectToGuid.Contains(Parent))//check if parent component belongs to this prefab
					{
						MapWidgetToParent.Add(MapObjectToGuid[Widget], MapObjectToGuid[Parent]);
					}
				}
			}
			else
			{
				auto ActorGuid = MapObjectToGuid[Widget];

				ActorSaveData.ObjectClass = FindOrAddClassFromList(Widget->GetClass());
				ActorSaveData.ActorGuid = ActorGuid;
				ActorSaveData.ObjectFlags = (uint32)Widget->GetFlags();
				WriterOrReaderFunction(Widget, SavedObjectData.Add(ActorGuid), false);
				ActorSaveData.RootComponentGuid = MapObjectToGuid[Widget];
				TArray<UObject*> DefaultSubObjects;
				Widget->GetDefaultSubobjects(DefaultSubObjects);
				for (auto DefaultSubObject : DefaultSubObjects)
				{
					FGuid DefaultSubObjectGuid;
					if (!CollectObjectToSerialize(DefaultSubObject, DefaultSubObjectGuid))continue;
					ActorSaveData.DefaultSubObjectGuidArray.Add(MapObjectToGuid[DefaultSubObject]);
					ActorSaveData.DefaultSubObjectNameArray.Add(DefaultSubObject->GetFName());
				}
			}
			SavedActors.Add(ActorSaveData);
		}
	}
	void ActorSerializer::SerializeActorToData(ULexWidget* OriginRootActor, FLexUIPrefabSaveData& OutData)
	{
		if (LGUIPrefabManager == nullptr)
		{
			LGUIPrefabManager = ULexUIPrefabWorldSubsystem::GetInstance(OriginRootActor->GetWorld());
		}
		CollectWidgetRecursive(OriginRootActor);
		//serialize actor
		SerializeActorArray(OutData.MapSceneComponentToParent, OutData.SavedActors, OutData.SavedObjectData);
		//serialize objects and components
		SerializeObjectArray(OutData.SavedObjects, OutData.SavedObjectData, OutData.MapSceneComponentToParent);
	}
	bool ActorSerializer::SerializeActor(ULexWidget* OriginRootActor, ULexUIPrefab* InPrefab)
	{
		auto StartTime = FDateTime::Now();

		FLexUIPrefabSaveData SaveData;
		SerializeActorToData(OriginRootActor, SaveData);

		FBufferArchive ToBinary;
#if WITH_EDITOR
		if (bIsEditorOrRuntime)
		{
			FStructuredArchiveFromArchive(ToBinary).GetSlot() << SaveData;
		}
		else
#endif
		{
			ToBinary << SaveData;
		}

		if (ToBinary.Num() <= 0)
		{
			UE_LOG(LGUI, Warning, TEXT("Save binary length is 0!"));
			return false;
		}
#if WITH_EDITOR
		if (bIsEditorOrRuntime)
		{
			InPrefab->BinaryData = ToBinary;
			InPrefab->bThumbnailDirty = true;
			InPrefab->CreateTime = FDateTime::UtcNow();

			//clear old reference data
			InPrefab->ReferenceAssetList.Empty();
			InPrefab->ReferenceClassList.Empty();
			InPrefab->ReferenceNameList.Empty();
			//fill new reference data
			InPrefab->ReferenceAssetList = this->ReferenceAssetList;
			InPrefab->ReferenceClassList = this->ReferenceClassList;
			InPrefab->ReferenceNameList = this->ReferenceNameList;

			InPrefab->ArchiveVersion = GPackageFileUEVersion.FileVersionUE4;
			InPrefab->ArchiveVersionUE5 = GPackageFileUEVersion.FileVersionUE5;
			InPrefab->ArchiveLicenseeVer = GPackageFileLicenseeUEVersion;
			InPrefab->ArEngineNetVer = FNetworkVersion::GetNetworkProtocolVersion(FEngineNetworkCustomVersion::Guid);
			InPrefab->ArGameNetVer = FNetworkVersion::GetNetworkProtocolVersion(FGameNetworkCustomVersion::Guid);
		}
		else
#endif
		{
			InPrefab->BinaryDataForBuild = ToBinary;

			//fill new reference data
			InPrefab->ReferenceAssetListForBuild = this->ReferenceAssetList;
			InPrefab->ReferenceClassListForBuild = this->ReferenceClassList;
			InPrefab->ReferenceNameListForBuild = this->ReferenceNameList;

			InPrefab->ArchiveVersion_ForBuild = GPackageFileUEVersion.FileVersionUE4;
			InPrefab->ArchiveVersionUE5_ForBuild = GPackageFileUEVersion.FileVersionUE5;
			InPrefab->ArchiveLicenseeVer_ForBuild = GPackageFileLicenseeUEVersion;
			InPrefab->ArEngineNetVer_ForBuild = FNetworkVersion::GetNetworkProtocolVersion(FEngineNetworkCustomVersion::Guid);
			InPrefab->ArGameNetVer_ForBuild = FNetworkVersion::GetNetworkProtocolVersion(FGameNetworkCustomVersion::Guid);
		}

		InPrefab->EngineMajorVersion = ENGINE_MAJOR_VERSION;
		InPrefab->EngineMinorVersion = ENGINE_MINOR_VERSION;
		InPrefab->PrefabVersion = LGUI_CURRENT_PREFAB_VERSION;

		auto TimeSpan = FDateTime::Now() - StartTime;
		UE_LOG(LGUI, Log, TEXT("Take %fs saving prefab: %s"), TimeSpan.GetTotalSeconds(), *InPrefab->GetName());
		
		return true;
	}

	void ActorSerializer::CollectWidgetRecursive(ULexWidget* Widget)
	{
		if (!IsValid(Widget))return;
		if (Widget->HasAnyFlags(EObjectFlags::RF_Transient))return;
#if WITH_EDITOR
		if (!bIsEditorOrRuntime)
#endif
		//collect actor
		bool bIsSubPrefab = SubPrefabActorArray.Contains(Widget);
		if (!bIsSubPrefab)//sub prefab's actor should not put to the list
		{
			WillSerializeWidgetArray.Add(Widget);//sub-prefab just keep a reference, no need to serialize
			TrySerializeActorArray.Add(Widget);
		}
		else
		{
			if (SubPrefabMap.Contains(Widget))//sub-prefab's root actor
			{
				TrySerializeActorArray.Add(Widget);
			}
		}
		//collect all actors include sub-prefab's actor, because some property could reference it
		if (!MapObjectToGuid.Contains(Widget))
		{
			MapObjectToGuid.Add(Widget, FGuid::NewGuid());
		}

		auto& Children = Widget->GetChildren();
		for (auto& ChildWidget : Children)
		{
			CollectWidgetRecursive(ChildWidget);
		}
	}

	void ActorSerializer::SerializeObjectArray(TMap<FGuid, FLexUIObjectSaveData>& ObjectSaveDataArray, TMap<FGuid, TArray<uint8>>& SavedObjectData, TMap<FGuid, FGuid>& MapSceneComponentToParent)
	{
		for (int i = 0; i < WillSerializeObjectArray.Num(); i++)
		{
			auto Object = WillSerializeObjectArray[i];
			auto Class = Object->GetClass();
			FLexUIObjectSaveData ObjectSaveDataItem;
			ObjectSaveDataItem.ObjectClass = FindOrAddClassFromList(Class);
			ObjectSaveDataItem.ObjectName = Object->GetFName();
			ObjectSaveDataItem.ObjectFlags = (uint32)Object->GetFlags();
			ObjectSaveDataItem.OuterObjectGuid = MapObjectToGuid[Object->GetOuter()];
			auto SceneComp = Cast<USceneComponent>(Object);
			if (SceneComp)
			{
				if (auto ParentComp = SceneComp->GetAttachParent())
				{
					if (MapObjectToGuid.Contains(ParentComp))//check if parent component belongs to this prefab
					{
						MapSceneComponentToParent.Add(MapObjectToGuid[Object], MapObjectToGuid[ParentComp]);
					}
				}
			}
			WriterOrReaderFunction(Object, SavedObjectData.Add(MapObjectToGuid[Object]), SceneComp != nullptr);
			TArray<UObject*> DefaultSubObjects;
			Object->GetDefaultSubobjects(DefaultSubObjects);
			for (auto DefaultSubObject : DefaultSubObjects)
			{
				FGuid DefaultSubObjectGuid;
				if (!CollectObjectToSerialize(DefaultSubObject, DefaultSubObjectGuid))continue;
				ObjectSaveDataItem.DefaultSubObjectGuidArray.Add(MapObjectToGuid[DefaultSubObject]);
				ObjectSaveDataItem.DefaultSubObjectNameArray.Add(DefaultSubObject->GetFName());
			}
			ObjectSaveDataArray.Add(MapObjectToGuid[Object], ObjectSaveDataItem);
		}
	}
}

