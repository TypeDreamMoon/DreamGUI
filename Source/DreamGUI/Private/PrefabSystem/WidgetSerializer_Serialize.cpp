// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/WidgetSerializer.h"
#include "PrefabSystem/DreamUIAuthoredGeometrySaveScope.h"
#include "PrefabSystem/DreamUIObjectReaderAndWriter.h"
#include "DreamGUI.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Misc/NetworkVersion.h"
#include "Runtime/Launch/Resources/Version.h"


namespace DreamUIPrefabSystem
{
	namespace WidgetSerializerLocal
	{
		TSet<const UDreamWidget*> CollectReachableWidgets(const UDreamWidget* RootWidget)
		{
			TSet<const UDreamWidget*> Result;
			TArray<const UDreamWidget*> Pending;
			Pending.Add(RootWidget);
			while (!Pending.IsEmpty())
			{
				const UDreamWidget* Widget = Pending.Pop();
				if (!IsValid(Widget) || Result.Contains(Widget))
				{
					continue;
				}
				Result.Add(Widget);
				for (UDreamWidget* Child : Widget->GetChildren())
				{
					if (IsValid(Child))
					{
						Pending.Add(Child);
					}
				}
			}
			return Result;
		}

		bool IsObjectWithinRootHierarchy(UObject* Object, const TSet<const UDreamWidget*>& ReachableWidgets)
		{
			if (!IsValid(Object))
			{
				return false;
			}
			const UDreamWidget* OwningWidget = Cast<UDreamWidget>(Object);
			if (!OwningWidget)
			{
				OwningWidget = Object->GetTypedOuter<UDreamWidget>();
			}
			return IsValid(OwningWidget) && ReachableWidgets.Contains(OwningWidget);
		}
	}

	bool WidgetSerializer::SavePrefab(UDreamWidget* OriginRootWidget, UDreamUIPrefab* InPrefab
		, TMap<UObject*, FGuid>& InOutMapObjectToGuid, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap
		, bool InForEditorOrRuntimeUse
	)
	{
		if (!OriginRootWidget || !InPrefab)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d OriginRootWidget Or InPrefab is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (!IsValid(OriginRootWidget))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d OriginRootWidget is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (OriginRootWidget->HasAnyFlags(EObjectFlags::RF_Transient))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d OriginRootWidget is transient!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		if (!InForEditorOrRuntimeUse && OriginRootWidget->IsEditorOnly())
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d OriginRootWidget is editor only!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return false;
		}
		const FDreamUIPrefabSchemaMigrationReport MigrationReport = InPrefab->ApplySchemaMigration(OriginRootWidget);
		for (const FString& Warning : MigrationReport.Warnings)
		{
			UE_LOG(DreamGUI, Warning, TEXT("Prefab schema migration: %s"), *Warning);
		}
		for (const FString& Error : MigrationReport.Errors)
		{
			UE_LOG(DreamGUI, Error, TEXT("Prefab schema migration: %s"), *Error);
		}
		if (MigrationReport.HasErrors())
		{
			return false;
		}
		WidgetSerializer serializer;
		const TSet<const UDreamWidget*> ReachableWidgets = WidgetSerializerLocal::CollectReachableWidgets(OriginRootWidget);
		int32 RemovedStaleMappings = 0;
		for (auto& KeyValue : InOutMapObjectToGuid)//Preprocess the map, ignore invalid object
		{
			if (WidgetSerializerLocal::IsObjectWithinRootHierarchy(KeyValue.Key, ReachableWidgets))
			{
				serializer.MapObjectToGuid.Add(KeyValue.Key, KeyValue.Value);
			}
			else
			{
				++RemovedStaleMappings;
			}
		}
		if (RemovedStaleMappings > 0)
		{
			UE_LOG(DreamGUI, Display, TEXT("Filtered %d stale GUID mapping(s) before prefab serialization."), RemovedStaleMappings);
		}
		serializer.SubPrefabMap = InSubPrefabMap;
		for (auto& SubPrefabKeyValue : InSubPrefabMap)
		{
			for (auto& GuidToObjectKeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
			{
				if (auto SubPrefabWidget = Cast<UDreamWidget>(GuidToObjectKeyValue.Value))
				{
					serializer.SubPrefabWidgetArray.Add(SubPrefabWidget);
				}
			}
		}
		serializer.bIsEditorOrRuntime = InForEditorOrRuntimeUse;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer) {
			DreamUIPrefabSystem::FDreamUIObjectWriter Writer(InOutBuffer, serializer, {});
			Writer.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			DreamUIPrefabSystem::FDreamUIOverrideParameterObjectWriter Writer(InOutBuffer, serializer, InOverridePropertyNames);
			Writer.DoSerialize(InObject);
		};
		// The asset stores authored geometry only: panel-arranged rects are swapped out for the duration
		// of serialization (see FDreamUIAuthoredGeometrySaveScope) and re-derived by layout after load.
		// A no-op when an outer scope (e.g. the helper's save+verify span) already swapped.
		// The scope is editor-only, matching where prefab assets are authored.
#if WITH_EDITOR
		FDreamUIAuthoredGeometrySaveScope AuthoredGeometryScope(OriginRootWidget);
#endif
		bool saveResult = serializer.SerializeWidget(OriginRootWidget, InPrefab);
		InOutMapObjectToGuid = serializer.MapObjectToGuid;
		return saveResult;
	}

	void WidgetSerializer::SerializeWidgetArray(TMap<FGuid, FGuid>& MapWidgetToParent, TArray<FDreamUIWidgetSaveData>& SavedWidgets, TMap<FGuid, TArray<uint8>>& SavedObjectData)
	{
		for (int i = 0; i < TrySerializeWidgetArray.Num(); i++)
		{
			auto& Widget = TrySerializeWidgetArray[i];
			FDreamUIWidgetSaveData WidgetSaveData;
			if (auto SubPrefabDataPtr = SubPrefabMap.Find(Widget))//sub prefab's Widget is not collected in WillSerializeWidgetArray
			{
				WidgetSaveData.bIsPrefab = true;
				WidgetSaveData.PrefabAssetIndex = FindOrAddAssetIdFromList(SubPrefabDataPtr->PrefabAsset);
				WidgetSaveData.WidgetGuid = MapObjectToGuid[Widget];
				WidgetSaveData.MapObjectGuidFromParentPrefabToSubPrefab = SubPrefabDataPtr->MapObjectGuidFromParentPrefabToSubPrefab;

				//serialize override parameter data
				for (auto& DataItem : SubPrefabDataPtr->ObjectOverrideParameterArray)
				{
					TArray<uint8> SubPrefabOverrideData;
					auto SubPrefabObject = DataItem.Object.Get();
					if (MapObjectToGuid.Contains(SubPrefabObject))
					{
						FDreamUIPrefabOverrideParameterSaveData RecordDataItem;
						RecordDataItem.OverrideParameterNames = DataItem.MemberPropertyNames;
						WriterOrReaderFunctionForSubPrefabOverride(SubPrefabObject, RecordDataItem.OverrideParameterData, DataItem.MemberPropertyNames);
						WidgetSaveData.MapObjectGuidToSubPrefabOverrideParameter.Add(MapObjectToGuid[SubPrefabObject], RecordDataItem);
					}
				}

				// The nested root's panel slot is created by its parent and therefore
				// has no object GUID in the source prefab. Persist all editable slot
				// fields as parent-owned attachment data.
				if (UDreamPanelSlot* PanelSlot = Widget->GetPanelSlot(); IsValid(PanelSlot))
				{
					FDreamUISubPrefabObjectUniqueId PanelSlotId;
					PanelSlotId.RootWidgetGuidInParentPrefab = WidgetSaveData.WidgetGuid;
					PanelSlotId.ObjectGuidInOriginPrefab = GetSubPrefabRootPanelSlotOriginGuid();

					FGuid PanelSlotGuid;
					if (const FGuid* StoredGuid = SubPrefabDataPtr->MapObjectIdToNewlyCreatedId.Find(PanelSlotId))
					{
						PanelSlotGuid = *StoredGuid;
					}
					else if (const FGuid* ExistingGuid = MapObjectToGuid.Find(PanelSlot))
					{
						PanelSlotGuid = *ExistingGuid;
					}
					else
					{
						PanelSlotGuid = FGuid::NewGuid();
					}

					MapObjectToGuid.Add(PanelSlot, PanelSlotGuid);
					SubPrefabDataPtr->MapGuidToObject.Add(GetSubPrefabRootPanelSlotOriginGuid(), PanelSlot);
					SubPrefabDataPtr->MapObjectGuidFromParentPrefabToSubPrefab.Add(
						PanelSlotGuid, GetSubPrefabRootPanelSlotOriginGuid());
					SubPrefabDataPtr->MapObjectIdToNewlyCreatedId.Add(PanelSlotId, PanelSlotGuid);

					FDreamUIPrefabOverrideParameterSaveData RecordDataItem;
					for (TFieldIterator<FProperty> PropertyIt(PanelSlot->GetClass(), EFieldIterationFlags::IncludeSuper);
						PropertyIt; ++PropertyIt)
					{
						FProperty* Property = *PropertyIt;
						if (Property->HasAnyPropertyFlags(CPF_Edit)
							&& !DreamUIPrefab_ShouldSkipProperty(Property))
						{
							RecordDataItem.OverrideParameterNames.Add(Property->GetFName());
						}
					}
					WriterOrReaderFunctionForSubPrefabOverride(
						PanelSlot, RecordDataItem.OverrideParameterData, RecordDataItem.OverrideParameterNames);
					WidgetSaveData.MapObjectGuidToSubPrefabOverrideParameter.Add(
						PanelSlotGuid, MoveTemp(RecordDataItem));
				}

				for (auto& DataItem : SubPrefabDataPtr->MapObjectIdToNewlyCreatedId)
				{
					WidgetSaveData.MapObjectIdToNewlyCreatedId.Add({ DataItem.Key.RootWidgetGuidInParentPrefab, DataItem.Key.ObjectGuidInOriginPrefab }, DataItem.Value);
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
				auto WidgetGuid = MapObjectToGuid[Widget];

				WidgetSaveData.ObjectClass = FindOrAddClassFromList(Widget->GetClass());
				WidgetSaveData.WidgetGuid = WidgetGuid;
				WidgetSaveData.ObjectFlags = (uint32)Widget->GetFlags();
				WriterOrReaderFunction(Widget, SavedObjectData.Add(WidgetGuid));
				TArray<UObject*> DefaultSubObjects;
				Widget->GetDefaultSubobjects(DefaultSubObjects);
				for (auto DefaultSubObject : DefaultSubObjects)
				{
					FGuid DefaultSubObjectGuid;
					if (!CollectObjectToSerialize(DefaultSubObject, DefaultSubObjectGuid))continue;
					WidgetSaveData.DefaultSubObjectGuidArray.Add(MapObjectToGuid[DefaultSubObject]);
					WidgetSaveData.DefaultSubObjectNameArray.Add(DefaultSubObject->GetFName());
				}
				
				if (auto Parent = Widget->GetParent())
				{
					if (MapObjectToGuid.Contains(Parent))//check if parent component belongs to this prefab
					{
						MapWidgetToParent.Add(MapObjectToGuid[Widget], MapObjectToGuid[Parent]);
					}
				}
			}
			SavedWidgets.Add(WidgetSaveData);
		}
	}
	void WidgetSerializer::SerializeWidgetToData(UDreamWidget* OriginRootWidget, FDreamUIPrefabSaveData& OutData)
	{
		TSet<const UDreamWidget*> VisitedWidgets;
		CollectWidgetRecursive(OriginRootWidget, VisitedWidgets);
		//serialize Widget
		SerializeWidgetArray(OutData.MapWidgetToParent, OutData.SavedWidgets, OutData.SavedObjectData);
		//serialize objects and components
		SerializeObjectArray(OutData.SavedObjects, OutData.SavedObjectData);
	}
	bool WidgetSerializer::SerializeWidget(UDreamWidget* OriginRootWidget, UDreamUIPrefab* InPrefab)
	{
		auto StartTime = FDateTime::Now();

		// Encode with the version this save is about to stamp onto the asset (see LEXUI_CURRENT_PREFAB_VERSION
		// below), not the version the asset happened to carry beforehand. Adopting the previous version here made
		// the writer and the later reader disagree whenever the asset was not already at the newest version —
		// most visibly for a brand-new transient prefab (editor copy/paste), whose version starts at 0. FText is
		// the only type whose encoding is version-gated: it was written inline and then read back as a list
		// index, so FindTextFromListByIndex silently returned FText::GetEmpty() and every string vanished.
		this->PrefabVersion = LEXUI_CURRENT_PREFAB_VERSION;

		FDreamUIPrefabSaveData SaveData;
		SerializeWidgetToData(OriginRootWidget, SaveData);

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
			UE_LOG(DreamGUI, Warning, TEXT("Save binary length is 0!"));
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
			InPrefab->ReferenceTextList.Empty();
			//fill new reference data
			InPrefab->ReferenceAssetList = this->ReferenceAssetList;
			InPrefab->ReferenceClassList = this->ReferenceClassList;
			InPrefab->ReferenceNameList = this->ReferenceNameList;
			InPrefab->ReferenceTextList = this->ReferenceTextList;

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
			InPrefab->ReferenceTextListForBuild = this->ReferenceTextList;

			InPrefab->ArchiveVersion_ForBuild = GPackageFileUEVersion.FileVersionUE4;
			InPrefab->ArchiveVersionUE5_ForBuild = GPackageFileUEVersion.FileVersionUE5;
			InPrefab->ArchiveLicenseeVer_ForBuild = GPackageFileLicenseeUEVersion;
			InPrefab->ArEngineNetVer_ForBuild = FNetworkVersion::GetNetworkProtocolVersion(FEngineNetworkCustomVersion::Guid);
			InPrefab->ArGameNetVer_ForBuild = FNetworkVersion::GetNetworkProtocolVersion(FGameNetworkCustomVersion::Guid);
		}

		InPrefab->EngineMajorVersion = ENGINE_MAJOR_VERSION;
		InPrefab->EngineMinorVersion = ENGINE_MINOR_VERSION;
		InPrefab->EnginePatchVersion = ENGINE_PATCH_VERSION;
		InPrefab->PrefabVersion = LEXUI_CURRENT_PREFAB_VERSION;

		auto TimeSpan = FDateTime::Now() - StartTime;
		UE_LOG(DreamGUI, Log, TEXT("Take %fs saving prefab: %s"), TimeSpan.GetTotalSeconds(), *InPrefab->GetName());
		
		return true;
	}

	void WidgetSerializer::CollectWidgetRecursive(UDreamWidget* Widget, TSet<const UDreamWidget*>& VisitedWidgets)
	{
		if (!IsValid(Widget))return;
		if (VisitedWidgets.Contains(Widget))return;
		VisitedWidgets.Add(Widget);
		if (Widget->HasAnyFlags(EObjectFlags::RF_Transient))return;
		//collect actor
		bool bIsSubPrefabWidget = SubPrefabWidgetArray.Contains(Widget);
		if (!bIsSubPrefabWidget)//sub prefab's Widget should not put to the list
		{
			WillSerializeWidgetArray.Add(Widget);//sub-prefab just keep a reference, no need to serialize
			TrySerializeWidgetArray.Add(Widget);
		}
		else
		{
			if (SubPrefabMap.Contains(Widget))//sub-prefab's root Widget
			{
				TrySerializeWidgetArray.Add(Widget);
			}
		}
		//collect all Widgets include sub-prefab's Widget, because some property could reference it
		if (!MapObjectToGuid.Contains(Widget))
		{
			MapObjectToGuid.Add(Widget, FGuid::NewGuid());
		}

		auto& Children = Widget->GetChildren();
		for (auto& ChildWidget : Children)
		{
			CollectWidgetRecursive(ChildWidget, VisitedWidgets);
		}
	}

	void WidgetSerializer::SerializeObjectArray(TMap<FGuid, FDreamUIObjectSaveData>& ObjectSaveDataArray, TMap<FGuid, TArray<uint8>>& SavedObjectData)
	{
		for (int i = 0; i < WillSerializeObjectArray.Num(); i++)
		{
			auto Object = WillSerializeObjectArray[i];
			auto Class = Object->GetClass();
			FDreamUIObjectSaveData ObjectSaveDataItem;
			ObjectSaveDataItem.ObjectClass = FindOrAddClassFromList(Class);
			ObjectSaveDataItem.ObjectName = Object->GetFName();
			ObjectSaveDataItem.ObjectFlags = (uint32)Object->GetFlags();
			ObjectSaveDataItem.OuterObjectGuid = MapObjectToGuid[Object->GetOuter()];
			WriterOrReaderFunction(Object, SavedObjectData.Add(MapObjectToGuid[Object]));
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

