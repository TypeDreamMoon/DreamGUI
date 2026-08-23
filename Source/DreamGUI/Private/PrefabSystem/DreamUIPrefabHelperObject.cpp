// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "DreamGUI.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "Utils/DreamUIUtils.h"
#include "PrefabSystem/DreamUIObjectReaderAndWriter.h"
#if WITH_EDITOR
#include "Core/DreamUISettings.h"
#include "PrefabSystem/DreamUIAuthoredGeometrySaveScope.h"
#include "PrefabSystem/DreamUIPrefabSaveVerification.h"
#endif

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#include LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE

#define LOCTEXT_NAMESPACE "DreamGUIPrefabManager"

namespace DreamUIPrefabHelperLocal
{
	bool IsSubPrefabRootPanelSlot(
		const TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& SubPrefabMap,
		const UObject* Object)
	{
		for (const TPair<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& Pair : SubPrefabMap)
		{
			const TObjectPtr<UObject>* RootPanelSlot =
				Pair.Value.MapGuidToObject.Find(DreamUIPrefabSystem::GetSubPrefabRootPanelSlotOriginGuid());
			if (RootPanelSlot && RootPanelSlot->Get() == Object)
			{
				return true;
			}
		}
		return false;
	}

	void RemoveGuidReferencesFromSubPrefabs(
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& SubPrefabMap,
		const FGuid& ParentGuid,
		UObject* RemovedObject)
	{
		for (TPair<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& Pair : SubPrefabMap)
		{
			FDreamUISubPrefabData& Data = Pair.Value;
			if (const FGuid* SubPrefabGuid = Data.MapObjectGuidFromParentPrefabToSubPrefab.Find(ParentGuid))
			{
				Data.MapGuidToObject.Remove(*SubPrefabGuid);
				Data.MapObjectGuidFromParentPrefabToSubPrefab.Remove(ParentGuid);
			}
			for (auto It = Data.MapObjectIdToNewlyCreatedId.CreateIterator(); It; ++It)
			{
				if (It.Value() == ParentGuid || It.Key().RootWidgetGuidInParentPrefab == ParentGuid)
				{
					It.RemoveCurrent();
				}
			}
			if (IsValid(RemovedObject))
			{
				Data.ObjectOverrideParameterArray.RemoveAll(
					[RemovedObject](const FDreamUIPrefabOverrideParameterData& Override)
					{
						return Override.Object.Get() == RemovedObject;
					});
			}
		}
	}
}


UDreamUIPrefabHelperObject::UDreamUIPrefabHelperObject()
{
	
}

#if WITH_EDITOR
void UDreamUIPrefabHelperObject::BeginDestroy()
{
	ClearLoadedPrefab();
	Super::BeginDestroy();
}

void UDreamUIPrefabHelperObject::Init(UDreamUIPrefab* InPrefab, FDreamUIPrefabInstanceScene* InPrefabInstanceScene)
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);

	PrefabAsset = InPrefab;
	PrefabInstanceWorld = InPrefabInstanceScene->GetWorld();
	if (!IsValid(LoadedRootWidget))
	{
		auto Parent = InPrefabInstanceScene->GetParentForLoadPrefab(PrefabAsset);
		LoadedRootWidget = PrefabAsset->LoadPrefabWithExistingObjects(PrefabInstanceWorld.Get()
			, Parent->GetOuter()
			, Parent
			, MapGuidToObject, SubPrefabMap
		);
	}
	if (LoadedRootWidget == nullptr)return;
	
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UDreamUIPrefabHelperObject::OnObjectPropertyChanged);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &UDreamUIPrefabHelperObject::OnPreObjectPropertyChanged);

	UDreamUIManagerWorldSubsystem::RefreshAllUI();
}
#endif

#if WITH_EDITOR

void UDreamUIPrefabHelperObject::ClearLoadedPrefab()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);

	if (IsValid(LoadedRootWidget))
	{
		LoadedRootWidget->DestroyWidget();
		LoadedRootWidget = nullptr;;
	}
	MapGuidToObject.Empty();
	SubPrefabMap.Empty();
}

bool UDreamUIPrefabHelperObject::IsWidgetBelongsToSubPrefab(const UDreamWidget* InWidget)
{
	CleanupInvalidSubPrefab();
	if (!IsValid(InWidget))return false;
	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		auto& SubMapGuidToObject = SubPrefabKeyValue.Value.MapGuidToObject;
		for (auto& SubMapGuidToObjectKeyValue : SubMapGuidToObject)
		{
			if (SubMapGuidToObjectKeyValue.Value == InWidget)
			{
				return true;
			}
		}
	}
	return false;
}
bool UDreamUIPrefabHelperObject::IsWidgetBelongsToMissingSubPrefab(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))return false;
#if WITH_EDITOR
	for (auto& Item : MissingPrefab)
	{
		if (InWidget == Item || InWidget->IsChildOf(Item))
		{
			return true;
		}
	}
#endif
	return false;
}

bool UDreamUIPrefabHelperObject::IsSubPrefabRootWidget(const UDreamWidget* InWidget)
{
	CleanupInvalidSubPrefab();
	if (!IsValid(InWidget))return false;
	return SubPrefabMap.Contains(InWidget);
}

bool UDreamUIPrefabHelperObject::IsWidgetBelongsToThis(const UDreamWidget* InWidget)
{
	if (IsValid(this->LoadedRootWidget))
	{
		if (InWidget->IsChildOf(LoadedRootWidget) || InWidget == LoadedRootWidget)
		{
			return true;
		}
	}
	return false;
}

bool UDreamUIPrefabHelperObject::ClearInvalidObjectAndGuid()
{
	TSet<FGuid> GuidsToRemove;
	for (auto& KeyValue : MapGuidToObject)
	{
		if (!IsValid(KeyValue.Value))
		{
			GuidsToRemove.Add(KeyValue.Key);
		}
	}
	for (auto& Item : GuidsToRemove)
	{
		MapGuidToObject.Remove(Item);

		for (auto& SubPrefabKeyValue : SubPrefabMap)
		{
			if (auto GuidInSubPrefabPtr = SubPrefabKeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab.Find(Item))
			{
				auto GuidInParentPrefab = Item;
				auto GuidInSubPrefab = *GuidInSubPrefabPtr;
				SubPrefabKeyValue.Value.MapGuidToObject.Remove(GuidInSubPrefab);
				SubPrefabKeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab.Remove(GuidInParentPrefab);
				break;
			}
		}
	}
	return GuidsToRemove.Num() > 0;
}

int32 UDreamUIPrefabHelperObject::CleanupObjectsOutsideRootHierarchy()
{
	if (!IsValid(LoadedRootWidget))
	{
		return 0;
	}

	TSet<const UDreamWidget*> ReachableWidgets;
	TArray<const UDreamWidget*> PendingWidgets;
	PendingWidgets.Add(LoadedRootWidget);
	while (!PendingWidgets.IsEmpty())
	{
		const UDreamWidget* Widget = PendingWidgets.Pop();
		if (!IsValid(Widget) || ReachableWidgets.Contains(Widget))
		{
			continue;
		}
		ReachableWidgets.Add(Widget);
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child))
			{
				PendingWidgets.Add(Child);
			}
		}
	}

	struct FStaleGuidObject
	{
		FGuid Guid;
		TObjectPtr<UObject> Object;
	};
	TArray<FStaleGuidObject> StaleObjects;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : MapGuidToObject)
	{
		UObject* Object = Pair.Value.Get();
		if (!IsValid(Object))
		{
			continue;
		}
		const UDreamWidget* OwningWidget = Cast<UDreamWidget>(Object);
		if (!OwningWidget)
		{
			OwningWidget = Object->GetTypedOuter<UDreamWidget>();
		}
		if (!IsValid(OwningWidget) || !ReachableWidgets.Contains(OwningWidget))
		{
			StaleObjects.Add({ Pair.Key, Object });
		}
	}

	for (const FStaleGuidObject& Stale : StaleObjects)
	{
		MapGuidToObject.Remove(Stale.Guid);
		DreamUIPrefabHelperLocal::RemoveGuidReferencesFromSubPrefabs(SubPrefabMap, Stale.Guid, Stale.Object.Get());
	}
	if (!StaleObjects.IsEmpty())
	{
		bAnythingDirty = true;
		if (IsValid(PrefabAsset))
		{
			PrefabAsset->MarkPackageDirty();
		}
		UE_LOG(DreamGUI, Display, TEXT("Removed %d stale prefab GUID mapping(s) outside the root hierarchy."), StaleObjects.Num());
	}
	return StaleObjects.Num();
}

void UDreamUIPrefabHelperObject::AddMemberPropertyToSubPrefab(UDreamWidget* InSubPrefabWidget, UObject* InObject, FName InPropertyName)
{
	CleanupInvalidSubPrefab();
	if (!IsValid(InSubPrefabWidget))return;
	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		for (auto& KeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
		{
			if (InSubPrefabWidget == KeyValue.Value)
			{
				SubPrefabKeyValue.Value.AddMemberProperty(InObject, InPropertyName);
			}
		}
	}
}

void UDreamUIPrefabHelperObject::RemoveMemberPropertyFromSubPrefab(UDreamWidget* InSubPrefabWidget, UObject* InObject, FName InPropertyName)
{
	CleanupInvalidSubPrefab();
	if (!IsValid(InSubPrefabWidget))return;
	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		for (auto& KeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
		{
			if (InSubPrefabWidget == KeyValue.Value)
			{
				SubPrefabKeyValue.Value.RemoveMemberProperty(InObject, InPropertyName);
				break;
			}
		}
	}
}

void UDreamUIPrefabHelperObject::RemoveAllMemberPropertyFromSubPrefab(UDreamWidget* InSubPrefabRootWidget, bool InIncludeRootTransform)
{
	CleanupInvalidSubPrefab();
	if (!IsValid(InSubPrefabRootWidget))return;
	for (auto& KeyValue : SubPrefabMap)
	{
		auto SubPrefabRootWidget = KeyValue.Key;
		FDreamUISubPrefabData& SubPrefabData = KeyValue.Value;
		SubPrefabData.CheckParameters();
		if (InSubPrefabRootWidget == SubPrefabRootWidget)
		{
			for (int i = 0; i < SubPrefabData.ObjectOverrideParameterArray.Num(); i++)
			{
				auto& DataItem = SubPrefabData.ObjectOverrideParameterArray[i];
				TSet<FName> FilterNameSet;
				if (InSubPrefabRootWidget == DataItem.Object)//if prefab's root widget, then skip it's transform
				{
					if (!InIncludeRootTransform)
					{
						FilterNameSet.Add(UDreamWidget::GetPropertyName_RelativeLocation());
						FilterNameSet.Add(UDreamWidget::GetPropertyName_RelativeRotation());
						FilterNameSet.Add(UDreamWidget::GetPropertyName_RelativeScale());
					}
				}

				TSet<FName> NamesToClear;
				for (auto& PropertyName : DataItem.MemberPropertyNames)
				{
					if (FilterNameSet.Contains(PropertyName))continue;
					NamesToClear.Add(PropertyName);
				}
				for (auto& PropertyName : NamesToClear)
				{
					DataItem.MemberPropertyNames.RemoveSwap(PropertyName);
				}
				if (DataItem.MemberPropertyNames.Num() == 0)
				{
					SubPrefabData.ObjectOverrideParameterArray.RemoveAt(i);
					i--;
				}
			}
			return;
		}
	}
}

FDreamUISubPrefabData UDreamUIPrefabHelperObject::GetSubPrefabData(UDreamWidget* InSubPrefabWidget)
{
	CleanupInvalidSubPrefab();
	check(IsValid(InSubPrefabWidget));
	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		auto& SubMapGuidToObject = SubPrefabKeyValue.Value.MapGuidToObject;
		for (auto& KeyValue : SubMapGuidToObject)
		{
			if (InSubPrefabWidget == KeyValue.Value)
			{
				SubPrefabKeyValue.Value.CheckParameters();
				return SubPrefabKeyValue.Value;
			}
		}
	}
	return FDreamUISubPrefabData();
}

UDreamWidget* UDreamUIPrefabHelperObject::GetSubPrefabRootWidget(UDreamWidget* InSubPrefabWidget)
{
	CleanupInvalidSubPrefab();
	check(IsValid(InSubPrefabWidget));
	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		for (auto& KeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
		{
			if (InSubPrefabWidget == KeyValue.Value)
			{
				return SubPrefabKeyValue.Key;
			}
		}
	}
	return nullptr;
}

bool UDreamUIPrefabHelperObject::SavePrefab()
{
	CleanupInvalidSubPrefab();
	ClearInvalidObjectAndGuid();
	CleanupObjectsOutsideRootHierarchy();
	if (IsValid(PrefabAsset))
	{
		TMap<UObject*, FGuid> MapObjectToGuid;
		for (auto& KeyValue : MapGuidToObject)
		{
			if (IsValid(KeyValue.Value))
			{
				MapObjectToGuid.Add(KeyValue.Value, KeyValue.Key);
			}
		}
#if WITH_EDITOR
		// Loading during verification can refresh out-of-date sub prefabs, which saves them through this
		// same function — don't verify (or snapshot) recursively, the outermost save covers it.
		static bool bSaveVerificationInProgress = false;
		const bool bVerifyRoundTrip = GetDefault<UDreamUISettings>()->bVerifyPrefabSaveRoundTrip
			&& !bSaveVerificationInProgress;
		DreamUIPrefabSystem::FDreamUIPrefabEditorPayloadSnapshot PayloadSnapshot;
		if (bVerifyRoundTrip)
		{
			PayloadSnapshot.Capture(PrefabAsset);
		}
		// Hold authored geometry across BOTH the save and the round-trip verification: the asset persists
		// authored values, so the verification must compare authored-vs-authored, not arranged-vs-authored.
		// The serializer's own inner scope becomes a no-op under this one.
		FDreamUIAuthoredGeometrySaveScope AuthoredGeometryScope(LoadedRootWidget);
#endif
		const bool bSaveSucceeded = PrefabAsset->SavePrefab(LoadedRootWidget
			, MapObjectToGuid, SubPrefabMap
		);
		MapGuidToObject.Empty();
		for (auto KeyValue : MapObjectToGuid)
		{
			MapGuidToObject.Add(KeyValue.Value, KeyValue.Key);
		}
		if (bSaveSucceeded)
		{
#if WITH_EDITOR
			if (bVerifyRoundTrip)
			{
				TGuardValue<bool> ReentrancyGuard(bSaveVerificationInProgress, true);
				const DreamUIPrefabSystem::FDreamUIPrefabSaveVerificationResult Verification =
					DreamUIPrefabSystem::VerifyPrefabSaveRoundTrip(PrefabAsset, LoadedRootWidget);
				constexpr int32 MaxReportedDifferences = 20;
				int32 ReportedCount = 0;
				for (const FString& Difference : Verification.PropertyDifferences)
				{
					if (++ReportedCount > MaxReportedDifferences)
					{
						UE_LOG(DreamGUI, Warning, TEXT("Prefab save verification: ...and %d more property difference(s)."),
							Verification.PropertyDifferences.Num() - MaxReportedDifferences);
						break;
					}
					UE_LOG(DreamGUI, Warning, TEXT("Prefab save verification: property drift on %s: %s"),
						*PrefabAsset->GetName(), *Difference);
				}
				const bool bRefuseSave = !Verification.bStructureMatches
					|| (GetDefault<UDreamUISettings>()->bBlockPrefabSaveOnPropertyDrift
						&& Verification.PropertyDifferences.Num() > 0);
				if (bRefuseSave)
				{
					ReportedCount = 0;
					for (const FString& Difference : Verification.StructuralDifferences)
					{
						if (++ReportedCount > MaxReportedDifferences)
						{
							UE_LOG(DreamGUI, Error, TEXT("Prefab save verification: ...and %d more structural difference(s)."),
								Verification.StructuralDifferences.Num() - MaxReportedDifferences);
							break;
						}
						UE_LOG(DreamGUI, Error, TEXT("Prefab save verification: %s: %s"),
							*PrefabAsset->GetName(), *Difference);
					}
					UE_LOG(DreamGUI, Error,
						TEXT("Prefab save verification FAILED for %s: the just-serialized payload does not load back as the edited hierarchy. The asset has been rolled back to its previous payload and this save is refused."),
						*PrefabAsset->GetName());
					FDreamUIUtils::EditorNotification(FText::Format(
						LOCTEXT("PrefabSaveVerificationFailed",
							"Save of prefab '{0}' was refused: the serialized data does not load back as the edited hierarchy (see log). The asset keeps its previous data."),
						FText::FromString(PrefabAsset->GetName())), false, 10.0f);
					PayloadSnapshot.Restore(PrefabAsset);
					return false;
				}
			}
#endif
			bAnythingDirty = false;
			PrefabAsset->EnsureInstanceObjects();
		}
		return bSaveSucceeded;
	}

	UE_LOG(DreamGUI, Error, TEXT("PrefabAsset is null, please create a DreamGUIPrefab asset and assign to PrefabAsset"));
	return false;
}

UDreamUIPrefab* UDreamUIPrefabHelperObject::GetSubPrefabAsset(UDreamWidget* InSubPrefabWidget)
{
	CleanupInvalidSubPrefab();
	if (!IsValid(InSubPrefabWidget))return nullptr;
	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		for (auto& KeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
		{
			if (InSubPrefabWidget == KeyValue.Value)
			{
				return SubPrefabKeyValue.Value.PrefabAsset;
			}
		}
	}
	return nullptr;
}

void UDreamUIPrefabHelperObject::MarkOverrideParameterFromParentPrefab(UObject* InObject, const TArray<FName>& InPropertyNames)
{
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}

	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		for (auto& KeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
		{
			if (Widget == KeyValue.Value)
			{
				SubPrefabKeyValue.Value.AddMemberProperty(InObject, InPropertyNames);
			}
		}
	}
}
void UDreamUIPrefabHelperObject::MarkOverrideParameterFromParentPrefab(UObject* InObject, FName InPropertyName)
{
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}

	for (auto& SubPrefabKeyValue : SubPrefabMap)
	{
		for (auto& KeyValue : SubPrefabKeyValue.Value.MapGuidToObject)
		{
			if (Widget == KeyValue.Value)
			{
				SubPrefabKeyValue.Value.AddMemberProperty(InObject, InPropertyName);
				break;
			}
		}
	}
}




bool UDreamUIPrefabHelperObject::RefreshOnSubPrefabDirty(UDreamUIPrefab* InSubPrefab, UDreamWidget* InSubPrefabRootWidget)
{
	CleanupInvalidSubPrefab();

	bCanCollectProperty = false;
	bCanNotifyComponentCreateDelete = false;

	bool AnythingChange = false;

	for (auto& SubPrefabKeyValue : this->SubPrefabMap)
	{
		auto SubPrefabRootWidget = SubPrefabKeyValue.Key;
		auto& SubPrefabData = SubPrefabKeyValue.Value;
		SubPrefabData.CheckParameters();
		if (SubPrefabData.PrefabAsset == InSubPrefab
			&& (InSubPrefabRootWidget != nullptr ? SubPrefabRootWidget == InSubPrefabRootWidget : true)
			)
		{
			//store override parameter to data
			LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer serializer;
			serializer.bOverrideVersions = false;
			auto OverrideData = serializer.SaveOverrideParameterToData(SubPrefabData.ObjectOverrideParameterArray);

			auto& SubPrefabMapGuidToObject = SubPrefabData.MapGuidToObject;

			TSet<FGuid> ExtraObjectsGuidsToRemove;
			TSet<UObject*> ExtraObjectsToDelete;
			//check objects to delete: compare guid in sub-prefab's assets and this parent stored guid
			auto& MapGuidToObjectInSubPrefab = SubPrefabData.PrefabAsset->GetPrefabHelperObject()->MapGuidToObject;
			for (auto& KeyValue : SubPrefabMapGuidToObject)
			{
				if (KeyValue.Key != DreamUIPrefabSystem::GetSubPrefabRootPanelSlotOriginGuid()
					&& !MapGuidToObjectInSubPrefab.Contains(KeyValue.Key))
				{
					ExtraObjectsGuidsToRemove.Add(KeyValue.Key);
					ExtraObjectsToDelete.Add(KeyValue.Value);
					AnythingChange = true;
				}
			}
			for (auto& Item : ExtraObjectsGuidsToRemove)
			{
				SubPrefabMapGuidToObject.Remove(Item);

				FGuid FoundGuid;
				for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
				{
					if (KeyValue.Value == Item)
					{
						FoundGuid = KeyValue.Key;
						break;
					}
				}
				if (FoundGuid.IsValid())
				{
					SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Remove(FoundGuid);
				}
				AnythingChange = true;
			}

			//refresh sub-prefab's object
			TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> TempSubSubPrefabMap;
			auto ParentWidget = SubPrefabRootWidget->GetParent();
			InSubPrefab->LoadPrefabWithExistingObjects(GetPrefabWorld()
				, ParentWidget->GetOuter()
				, ParentWidget
				, SubPrefabMapGuidToObject, TempSubSubPrefabMap
			);

			//collect newly added object and guid
			auto ObjectExist = [&](UObject* InObject) {
				for (auto& KeyValue : this->MapGuidToObject)
				{
					if (KeyValue.Value == InObject)
					{
						return true;
					}
				}
				return false;
			};
			for (auto& KeyValue : SubPrefabMapGuidToObject)
			{
				if (!ObjectExist(KeyValue.Value))
				{
					auto NewGuid = FGuid::NewGuid();
					this->MapGuidToObject.Add(NewGuid, KeyValue.Value);
					SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(NewGuid, KeyValue.Key);
					AnythingChange = true;
				}

				//this object could be the same one, but with different guid (because it's guid not stored in parent prefab, and generated every time)
				if (ExtraObjectsToDelete.Contains(KeyValue.Value))
				{
					ExtraObjectsToDelete.Remove(KeyValue.Value);
				}
			}

			//delete extra objects
			for (auto& Obj : ExtraObjectsToDelete)
			{
				if (!IsValid(Obj))continue;
				if (auto Widget = Cast<UDreamWidget>(Obj))
				{
					Widget->DestroyWidget();
				}
				else if (auto WidgetComponent = Cast<UDreamUIBehaviour>(Obj))
				{
					WidgetComponent->DestroyComponent();
				}
				else
				{
					Obj->ConditionalBeginDestroy();
				}
			}

			//apply override parameter.
			{
				//clear not valid objects first
				for (int i = SubPrefabData.ObjectOverrideParameterArray.Num() - 1; i >= 0; i--)
				{
					auto& Item = SubPrefabData.ObjectOverrideParameterArray[i];
					if (!Item.Object.IsValid())
					{
						SubPrefabData.ObjectOverrideParameterArray.RemoveAt(i);
						AnythingChange = true;
					}
				}

				for (auto& ObjectOverrideItem : SubPrefabData.ObjectOverrideParameterArray)
				{
					for (auto& PropName : ObjectOverrideItem.MemberPropertyNames)
					{
						FDreamUIUtils::NotifyPropertyPreChange(ObjectOverrideItem.Object.Get(), PropName);
					}
				}
				serializer.RestoreOverrideParameterFromData(OverrideData, SubPrefabData.ObjectOverrideParameterArray);
				for (auto& ObjectOverrideItem : SubPrefabData.ObjectOverrideParameterArray)
				{
					for (auto& PropName : ObjectOverrideItem.MemberPropertyNames)
					{
						FDreamUIUtils::NotifyPropertyChanged(ObjectOverrideItem.Object.Get(), PropName);
					}
				}
			}

			SubPrefabRootWidget->UpdateObjectToWorldTransform();//root comp may stay prev position if not do this

			if (SubPrefabData.CheckParameters())
			{
				AnythingChange = true;
			}
		}
	}

	bool bRefreshSucceeded = true;
	if (AnythingChange)
	{
		bRefreshSucceeded = this->SavePrefab();
		if (bRefreshSucceeded)
		{
			if (this->PrefabAsset != nullptr)//could be null in level editor
			{
#if WITH_EDITOR
				this->PrefabAsset->bThumbnailDirty = true;
#endif
				this->PrefabAsset->MarkPackageDirty();
			}
			ClearInvalidObjectAndGuid();//incase LevelPrefab reference invalid object, eg: delete object in sub-prefab's sub-prefab, and update the prefab in level
		}
		else
		{
			bAnythingDirty = true;
			if (IsValid(PrefabAsset))
			{
				PrefabAsset->MarkPackageDirty();
			}
			UE_LOG(DreamGUI, Error,
				TEXT("Failed to save parent prefab '%s' after refreshing sub-prefab '%s'; the refresh remains dirty for retry."),
				*GetNameSafe(PrefabAsset), *GetNameSafe(InSubPrefab));
		}
	}
	if (bRefreshSucceeded)
	{
		RefreshSubPrefabVersion(InSubPrefabRootWidget);
	}
	bCanCollectProperty = true;
	bCanNotifyComponentCreateDelete = true;
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
	return AnythingChange && bRefreshSucceeded;
}

void UDreamUIPrefabHelperObject::OnObjectPropertyChanged(UObject* InObject, struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (!IsValid(InObject))return;
	if (InPropertyChangedEvent.MemberProperty == nullptr || InPropertyChangedEvent.Property == nullptr)return;
	if (DreamUIPrefabSystem::DreamUIPrefab_ShouldSkipProperty(InPropertyChangedEvent.MemberProperty))return;
	if (DreamUIPrefabSystem::DreamUIPrefab_ShouldSkipProperty(InPropertyChangedEvent.Property))return;

	TryCollectPropertyToOverride(InObject, InPropertyChangedEvent.MemberProperty);
}
void UDreamUIPrefabHelperObject::OnPreObjectPropertyChanged(UObject* InObject, const class FEditPropertyChain& InEditPropertyChain)
{
	if (!IsValid(InObject))return;
	auto ActiveMemberNode = InEditPropertyChain.GetActiveMemberNode();
	if (ActiveMemberNode == nullptr)return;
	auto MemberProperty = ActiveMemberNode->GetValue();
	if (MemberProperty == nullptr)return;
	if (DreamUIPrefabSystem::DreamUIPrefab_ShouldSkipProperty(MemberProperty))return;
	auto ActiveNode = InEditPropertyChain.GetActiveNode();
	if (ActiveNode != ActiveMemberNode)
	{
		auto Property = ActiveNode->GetValue();
		if (Property == nullptr)return;
		if (Property->HasAnyPropertyFlags(CPF_Transient))return;
	}

	TryCollectPropertyToOverride(InObject, MemberProperty);
}

void UDreamUIPrefabHelperObject::TryCollectPropertyToOverride(UObject* InObject, FProperty* InMemberProperty)
{
	if (!bCanCollectProperty)return;
	if (InObject->GetWorld() == this->GetPrefabWorld())
	{
		// A nested root's panel slot belongs to the parent prefab's layout.
		// Saving the parent persists it directly; it cannot be applied back to
		// the child prefab because no corresponding source slot exists there.
		if (DreamUIPrefabHelperLocal::IsSubPrefabRootPanelSlot(SubPrefabMap, InObject))
		{
			SetAnythingDirty();
			return;
		}

		auto PropertyName = InMemberProperty->GetFName();
		UDreamWidget* PropertyWidgetInSubPrefab = nullptr;
		if (auto Widget = Cast<UDreamWidget>(InObject))
		{
			if (auto ObjectProperty = CastField<FObjectPropertyBase>(InMemberProperty))
			{
				if (ObjectProperty->PropertyClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
				{
					return;//property change is propagated from Component to Widget, ignore it
				}
			}
			if (IsWidgetBelongsToSubPrefab(Widget))
			{
				PropertyWidgetInSubPrefab = Widget;
			}

			if (PropertyWidgetInSubPrefab != nullptr//if drag in level editor, then property change event will notify actor, so we need to collect property on actor's root component
				&& (PropertyName == UDreamWidget::GetPropertyName_RelativeLocation()
					|| PropertyName == UDreamWidget::GetPropertyName_RelativeRotation()
					|| PropertyName == UDreamWidget::GetPropertyName_RelativeScale()
					)
				)
			{
				InObject = PropertyWidgetInSubPrefab;
			}
		}
		if (auto OuterWidget = InObject->GetTypedOuter<UDreamWidget>())
		{
			if (IsWidgetBelongsToSubPrefab(OuterWidget))
			{
				bool bFindObjectInGuidMap = false;
				for (auto& KeyValue : this->MapGuidToObject)
				{
					if (KeyValue.Value == InObject)
					{
						bFindObjectInGuidMap = true;
						break;
					}
				}
				if (bFindObjectInGuidMap)
				{
					PropertyWidgetInSubPrefab = OuterWidget;
				}
			}
		}

		if (PropertyWidgetInSubPrefab)//object's member property
		{
			auto Property = FindFProperty<FProperty>(InObject->GetClass(), PropertyName);
			if (Property != nullptr)
			{
				SetAnythingDirty();
				AddMemberPropertyToSubPrefab(PropertyWidgetInSubPrefab, InObject, PropertyName);
				if (auto Widget = Cast<UDreamWidget>(InObject))
				{
					if (PropertyName == UDreamWidget::GetPropertyName_RelativeLocation())//if UI's relative location change, then record anchor data too
					{
						this->AddMemberPropertyToSubPrefab(Widget, InObject, UDreamWidget::GetPropertyName_AnchorData());
					}
					else if (PropertyName == UDreamWidget::GetPropertyName_AnchorData())//if UI's anchor data change, then record relative location too
					{
						this->AddMemberPropertyToSubPrefab(Widget, InObject, UDreamWidget::GetPropertyName_RelativeLocation());
					}
				}
				//refresh override parameter
			}
		}
		else
		{
			SetAnythingDirty();
		}
	}
}

UWorld* UDreamUIPrefabHelperObject::GetPrefabWorld() const
{
	return PrefabInstanceWorld.Get();
}

bool UDreamUIPrefabHelperObject::CleanupInvalidLinkToSubPrefabObject()
{
	auto IsValidParentLinkedGuid = [&](const FGuid& InCheckGuid) {
		for (auto& SubPrefabKeyValue : SubPrefabMap)
		{
			auto& SubPrefabData = SubPrefabKeyValue.Value;
			if (SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Contains(InCheckGuid))
			{
				return true;
			}
		}
		return false;
	};

	TSet<FGuid> GuidsToRemove;
	for (auto& KeyValue : MapGuidToObject)
	{
		if (!IsValidParentLinkedGuid(KeyValue.Key))
		{
			GuidsToRemove.Add(KeyValue.Key);
		}
	}
	for (auto& Item : GuidsToRemove)
	{
		MapGuidToObject.Remove(Item);
	}
	return GuidsToRemove.Num() > 0;
}

#pragma region RevertAndApply
/**
 * When revert, if the parameter is RelativeLocation, then Widget's AnchorData will also be reverted. Revert parameter is just copy data from origin to dest, origin means the temporary created objects in prefab's preview world.
 * But since AnchorData is relative to parent, and parent may not have the same AnchorData (because parent is temporary created inside preview world), so we need to set parent's AnchorData to now object's parent's AnchorData.
 */
void UDreamUIPrefabHelperObject::CopyRootObjectParentAnchorData(UObject* InObject, UObject* OriginObject)
{
	if (auto Widget = Cast<UDreamWidget>(InObject))
	{
		if (SubPrefabMap.Contains(Widget))//if is sub prefab's root component
		{
			auto InObjectWidget = Cast<UDreamWidget>(InObject);
			auto OriginObjectWidget = Cast<UDreamWidget>(OriginObject);
			if (InObjectWidget != nullptr && OriginObjectWidget != nullptr)//if is Widget, we need to copy parent's property to origin object's parent property, to make anchor & location calculation right
			{
				auto InObjectParent = InObjectWidget->GetParent();
				auto OriginObjectParent = OriginObjectWidget->GetParent();
				if (InObjectParent != nullptr && OriginObjectParent != nullptr)
				{
					//copy relative location
					auto RelativeLocationProperty = FindFProperty<FProperty>(InObjectParent->GetClass(), UDreamWidget::GetPropertyName_RelativeLocation());
					RelativeLocationProperty->CopyCompleteValue_InContainer(OriginObjectParent, InObjectParent);
					FDreamUIUtils::NotifyPropertyChanged(OriginObjectParent, RelativeLocationProperty);
					//copy anchor data
					auto AnchorDataProperty = FindFProperty<FProperty>(InObjectParent->GetClass(), UDreamWidget::GetPropertyName_AnchorData());
					AnchorDataProperty->CopyCompleteValue_InContainer(OriginObjectParent, InObjectParent);
					FDreamUIUtils::NotifyPropertyChanged(OriginObjectParent, AnchorDataProperty);
				}
			}
		}
	}
}

void UDreamUIPrefabHelperObject::RevertPrefabPropertyValue(UObject* ContextObject, FProperty* Property, void* ContainerPointerInSrc, void* ContainerPointerInPrefab, const FDreamUISubPrefabData& SubPrefabData, int RawArrayIndex, bool IsInsideRawArray)
{
	if (Property->ArrayDim > 1 && !IsInsideRawArray)
	{
		for (int i = 0; i < Property->ArrayDim; i++)
		{
			RevertPrefabPropertyValue(ContextObject, Property, ContainerPointerInSrc, ContainerPointerInPrefab, SubPrefabData, i, true);
		}
		return;
	}
	bool bPropertySupportDirectCopyValue = false;
	if (CastField<FClassProperty>(Property) != nullptr)
	{
		bPropertySupportDirectCopyValue = true;
	}
	else if (auto ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (auto ObjectInPrefab = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerPointerInPrefab))
		{
			auto ObjectClass = ObjectInPrefab->GetClass();
			if (ObjectInPrefab->IsAsset())
			{
				bPropertySupportDirectCopyValue = true;
			}
			else
			{
				//search object in guid
				FGuid ObjectGuidInPrefab;
				for (auto& KeyValue : SubPrefabData.PrefabAsset->GetPrefabHelperObject()->MapGuidToObject)
				{
					if (KeyValue.Value == ObjectInPrefab)
					{
						ObjectGuidInPrefab = KeyValue.Key;
						break;
					}
				}
				FGuid ObjectGuidInParent;
				//find valid guid, get the guid in prarent, with the guid then get object, so that object is the real value
				if (ObjectGuidInPrefab.IsValid())
				{
					for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
					{
						if (KeyValue.Value == ObjectGuidInPrefab)
						{
							ObjectGuidInParent = KeyValue.Key;
							break;
						}
					}
				}
				if (ObjectGuidInParent.IsValid())
				{
					const TObjectPtr<UObject>* ObjectInParentPtr = this->MapGuidToObject.Find(ObjectGuidInParent);
					UObject* ObjectInParent = ObjectInParentPtr ? ObjectInParentPtr->Get() : nullptr;
					if (ObjectClass->IsChildOf(UDreamWidget::StaticClass())
						|| ObjectClass->IsChildOf(UDreamWidgetSubObjectBehaviour::StaticClass())
						|| ObjectClass->IsChildOf(UDreamUIBehaviour::StaticClass())
						)
					{
						if (IsValid(ObjectInParent))
						{
							ObjectProperty->SetObjectPropertyValue_InContainer(ContainerPointerInSrc, ObjectInParent, RawArrayIndex);
						}
						else
						{
							UE_LOG(DreamGUI, Warning,
								TEXT("Cannot revert nested prefab reference '%s' because its parent object mapping is missing."),
								*Property->GetName());
						}
					}
					else
					{
						if (ObjectClass->HasAnyClassFlags(EClassFlags::CLASS_EditInlineNew)
							//&& ObjectProperty->HasAnyPropertyFlags(EPropertyFlags::CPF_InstancedReference)//is this necessary?
							)
						{
							if (!IsValid(ObjectInParent))
							{
								//EditInlineNew object can create new one
								ObjectInParent = NewObject<UObject>(ContextObject, ObjectClass, NAME_None, RF_NoFlags, ObjectInPrefab);
								if (IsValid(ObjectInParent))
								{
									this->MapGuidToObject.Add(ObjectGuidInParent, ObjectInParent);
								}
							}
							if (IsValid(ObjectInParent))
							{
								ObjectProperty->SetObjectPropertyValue_InContainer(ContainerPointerInSrc, ObjectInParent, RawArrayIndex);
								for (const auto PropertyItem : TFieldRange<FProperty>(ObjectClass))//check property inside object
								{
									RevertPrefabPropertyValue(ObjectInParent, PropertyItem, ObjectInParent, ObjectInPrefab, SubPrefabData);
								}
							}
						}
						else
						{
							auto InfoText = FText::Format(LOCTEXT("RevertPrefabPropertyValue_MissingConditionWarning", "DreamUI have not handle this condition:\nobject: '{0}'\nobjectClass: '{1}'")
								, FText::FromString(ObjectInPrefab->GetPathName()), FText::FromString(ObjectClass->GetPathName()));
							UE_LOG(DreamGUI, Log, TEXT("%s"), *InfoText.ToString());
							FDreamUIUtils::EditorNotification(InfoText, false);
						}
					}
				}
			}
		}
		else
		{
			bPropertySupportDirectCopyValue = true;
		}
	}
	else if (auto ArrayProperty = CastField<FArrayProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInSrc, ContainerPointerInPrefab);//just copy so we don't need to resize it
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(ContainerPointerInSrc, RawArrayIndex));
		FScriptArrayHelper ArrayHelperForPrefab(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(ContainerPointerInPrefab, RawArrayIndex));
		for (int i = 0; i < ArrayHelper.Num(); i++)
		{
			RevertPrefabPropertyValue(ContextObject, ArrayProperty->Inner, ArrayHelper.GetRawPtr(i), ArrayHelperForPrefab.GetRawPtr(i), SubPrefabData);
		}
	}
	else if (auto MapProperty = CastField<FMapProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInSrc, ContainerPointerInPrefab);//just copy so we don't need to resize it
		FScriptMapHelper MapHelper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(ContainerPointerInSrc, RawArrayIndex));
		FScriptMapHelper MapHelperForPrefab(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(ContainerPointerInPrefab, RawArrayIndex));
		for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
		{
			if (!MapHelper.IsValidIndex(Index) || !MapHelperForPrefab.IsValidIndex(Index))continue;
			RevertPrefabPropertyValue(ContextObject, MapProperty->KeyProp, MapHelper.GetKeyPtr(Index), MapHelperForPrefab.GetKeyPtr(Index), SubPrefabData);
			RevertPrefabPropertyValue(ContextObject, MapProperty->ValueProp, MapHelper.GetPairPtr(Index), MapHelperForPrefab.GetPairPtr(Index), SubPrefabData);
		}
		MapHelper.Rehash();
	}
	else if (auto SetProperty = CastField<FSetProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInSrc, ContainerPointerInPrefab);//just copy so we don't need to resize it
		FScriptSetHelper SetHelper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(ContainerPointerInSrc, RawArrayIndex));
		FScriptSetHelper SetHelperForPrefab(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(ContainerPointerInPrefab, RawArrayIndex));
		for (int32 Index = 0; Index < SetHelper.GetMaxIndex(); ++Index)
		{
			if (!SetHelper.IsValidIndex(Index) || !SetHelperForPrefab.IsValidIndex(Index))continue;
			RevertPrefabPropertyValue(ContextObject, SetProperty->ElementProp, SetHelper.GetElementPtr(Index), SetHelperForPrefab.GetElementPtr(Index), SubPrefabData);
		}
		SetHelper.Rehash();
	}
	else if (auto StructProperty = CastField<FStructProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInSrc, ContainerPointerInPrefab);
		auto StructPtr = Property->ContainerPtrToValuePtr<uint8>(ContainerPointerInSrc, RawArrayIndex);
		auto StructPtrForPrefab = Property->ContainerPtrToValuePtr<uint8>(ContainerPointerInPrefab, RawArrayIndex);
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			RevertPrefabPropertyValue(ContextObject, *It, StructPtr, StructPtrForPrefab, SubPrefabData);
		}
	}
	else
	{
		bPropertySupportDirectCopyValue = true;
	}
	if (bPropertySupportDirectCopyValue)
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInSrc, ContainerPointerInPrefab);
	}
}
void UDreamUIPrefabHelperObject::RevertPrefabOverride(UObject* InObject, const TArray<FName>& InPropertyNames)
{
	if (!IsValid(InObject))return;
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}
	if (!IsValid(Widget))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot revert prefab override for '%s' without an owning widget."), *InObject->GetPathName());
		return;
	}
	auto SubPrefabRootWidget = GetSubPrefabRootWidget(Widget);
	auto SubPrefabDataPtr = SubPrefabMap.Find(SubPrefabRootWidget);
	if (!SubPrefabDataPtr || !IsValid(SubPrefabDataPtr->PrefabAsset))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot revert prefab override for '%s' because its sub-prefab data is missing."), *InObject->GetPathName());
		return;
	}
	auto SubPrefabAsset = SubPrefabDataPtr->PrefabAsset.Get();
	auto SubPrefabHelperObject = SubPrefabAsset->GetPrefabHelperObject();
	if (!IsValid(SubPrefabHelperObject))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot revert prefab override for '%s' because the source prefab helper is unavailable."), *InObject->GetPathName());
		return;
	}
	FGuid ObjectGuid;
	for (auto& KeyValue : MapGuidToObject)
	{
		if (KeyValue.Value == InObject)
		{
			ObjectGuid = KeyValue.Key;
			break;
		}
	}
	const FGuid* ObjectGuidInSubPrefab = ObjectGuid.IsValid()
		? SubPrefabDataPtr->MapObjectGuidFromParentPrefabToSubPrefab.Find(ObjectGuid)
		: nullptr;
	const TObjectPtr<UObject>* ObjectInPrefabPtr = ObjectGuidInSubPrefab
		? SubPrefabHelperObject->MapGuidToObject.Find(*ObjectGuidInSubPrefab)
		: nullptr;
	UObject* ObjectInPrefab = ObjectInPrefabPtr ? ObjectInPrefabPtr->Get() : nullptr;
	if (!IsValid(ObjectInPrefab))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot revert prefab override for '%s' because its source object mapping is missing."), *InObject->GetPathName());
		return;
	}
	const FDreamUISubPrefabData SubPrefabData = *SubPrefabDataPtr;
	const FScopedTransaction Transaction(FText::Format(
		LOCTEXT("RevertPrefabOnObjectProperties", "Revert Prefab Override: {0}"),
		FText::FromString(InObject->GetName())));
	InObject->Modify();
	this->Modify();
	CopyRootObjectParentAnchorData(InObject, ObjectInPrefab);

	bCanCollectProperty = false;
	{
		for (auto PropertyName : InPropertyNames)
		{
			if (auto Property = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), PropertyName))
			{
				//notify
				FDreamUIUtils::NotifyPropertyPreChange(InObject, Property);//need to do PreChange here, so that actor's PostContructionScript can work
				//set to default value
				RevertPrefabPropertyValue(InObject, Property, InObject, ObjectInPrefab, SubPrefabData);
				AfterObjectPropertyApplyOrRevert(InObject, PropertyName);
				//delete item
				RemoveMemberPropertyFromSubPrefab(Widget, InObject, PropertyName);
				//notify
				FDreamUIUtils::NotifyPropertyChanged(InObject, Property);
				SetAnythingDirty();

				auto RelatedPropertyName = GetExtraRelatedPropertyForApplyOrRevert(InObject, PropertyName);
				if (RelatedPropertyName != NAME_None)
				{
					if (auto RelatedProperty = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), RelatedPropertyName))
					{
						//set to default value
						RevertPrefabPropertyValue(InObject, RelatedProperty, InObject, ObjectInPrefab, SubPrefabData);
						AfterObjectPropertyApplyOrRevert(InObject, RelatedPropertyName);
						//delete item
						RemoveMemberPropertyFromSubPrefab(Widget, InObject, RelatedPropertyName);
					}
				}
			}
		}
	}
	bCanCollectProperty = true;
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
	//when apply or revert parameters in level editor, means we accept sub-prefab's current version, so we mark the version to newest, and we won't get 'update warning'.
	RefreshSubPrefabVersion(SubPrefabRootWidget);
}

void UDreamUIPrefabHelperObject::RevertAllPrefabOverride(UObject* InObject)
{
	if (!IsValid(InObject))return;
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}
	if (!IsValid(Widget))return;
	auto SubPrefabRootWidget = GetSubPrefabRootWidget(Widget);
	auto SubPrefabDataPtr = SubPrefabMap.Find(SubPrefabRootWidget);
	if (!SubPrefabDataPtr || !IsValid(SubPrefabDataPtr->PrefabAsset))return;
	auto SubPrefabData = *SubPrefabDataPtr;
	auto SubPrefabAsset = SubPrefabData.PrefabAsset.Get();
	auto SubPrefabHelperObject = SubPrefabAsset->GetPrefabHelperObject();
	if (!IsValid(SubPrefabHelperObject))return;

	bCanCollectProperty = false;
	{
		GEditor->BeginTransaction(LOCTEXT("RevertPrefabOnAll_Transaction", "Revert Prefab Override"));
		for (int i = 0; i < SubPrefabData.ObjectOverrideParameterArray.Num(); i++)
		{
			auto& DataItem = SubPrefabData.ObjectOverrideParameterArray[i];
			DataItem.Object->Modify();
		}
		this->Modify();

		auto FindOriginObjectInSourcePrefab = [&](UObject* Object) -> UObject* {
			FGuid ObjectGuid;
			for (auto& KeyValue : MapGuidToObject)
			{
				if (KeyValue.Value == Object)
				{
					ObjectGuid = KeyValue.Key;
					break;
				}
			}
			const FGuid* ObjectGuidInSubPrefab = ObjectGuid.IsValid()
				? SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Find(ObjectGuid)
				: nullptr;
			const TObjectPtr<UObject>* SourceObject = ObjectGuidInSubPrefab
				? SubPrefabHelperObject->MapGuidToObject.Find(*ObjectGuidInSubPrefab)
				: nullptr;
			return SourceObject ? SourceObject->Get() : nullptr;
		};
		for (int i = 0; i < SubPrefabData.ObjectOverrideParameterArray.Num(); i++)
		{
			auto& DataItem = SubPrefabData.ObjectOverrideParameterArray[i];
			auto SourceObject = DataItem.Object.Get();
			TSet<FName> FilterNameSet;
			auto ObjectInPrefab = FindOriginObjectInSourcePrefab(SourceObject);
			if (!IsValid(SourceObject) || !IsValid(ObjectInPrefab))
			{
				UE_LOG(DreamGUI, Warning, TEXT("Skipping prefab override with a missing source mapping while reverting all overrides."));
				continue;
			}
			CopyRootObjectParentAnchorData(SourceObject, ObjectInPrefab);

			TSet<FName> NamesToClear;
			for (auto PropertyName : DataItem.MemberPropertyNames)
			{
				if (FilterNameSet.Contains(PropertyName))continue;
				NamesToClear.Add(PropertyName);
				if (auto Property = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), PropertyName))
				{
					//notify
					FDreamUIUtils::NotifyPropertyPreChange(SourceObject, Property);//need to do PreChange here, so that actor's PostContructionScript can work
					//set to default value
					RevertPrefabPropertyValue(InObject, Property, SourceObject, ObjectInPrefab, SubPrefabData);
					AfterObjectPropertyApplyOrRevert(InObject, PropertyName);
					//notify
					FDreamUIUtils::NotifyPropertyChanged(SourceObject, Property);

					auto RelatedPropertyName = GetExtraRelatedPropertyForApplyOrRevert(InObject, PropertyName);
					if (RelatedPropertyName != NAME_None)
					{
						NamesToClear.Add(RelatedPropertyName);
						if (auto RelatedProperty = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), RelatedPropertyName))
						{
							//set to default value
							RevertPrefabPropertyValue(InObject, RelatedProperty, SourceObject, ObjectInPrefab, SubPrefabData);
							AfterObjectPropertyApplyOrRevert(InObject, RelatedPropertyName);
							//delete item
							RemoveMemberPropertyFromSubPrefab(Widget, SourceObject, RelatedPropertyName);
						}
					}
				}
			}
			for (auto& PropertyName : NamesToClear)
			{
				DataItem.MemberPropertyNames.Remove(PropertyName);
			}
		}
		RemoveAllMemberPropertyFromSubPrefab(SubPrefabRootWidget, true);

		SetAnythingDirty();
		GEditor->EndTransaction();
		//when apply or revert parameters in level editor, means we accept sub-prefab's current version, so we mark the version to newest, and we won't get 'update warning'.
		RefreshSubPrefabVersion(GetSubPrefabRootWidget(Widget));
	}
	bCanCollectProperty = true;
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
}

FName UDreamUIPrefabHelperObject::GetExtraRelatedPropertyForApplyOrRevert(UObject* InObject, FName InPropertyName)
{
	if (IsValid(InObject)
		&& InObject->IsA<UDreamWidget>()
		&& InPropertyName == UDreamWidget::GetPropertyName_RelativeLocation())
	{
		return UDreamWidget::GetPropertyName_AnchorData();
	}
	return NAME_None;
}
void UDreamUIPrefabHelperObject::AfterObjectPropertyApplyOrRevert(UObject* InObject, FName InPropertyName)
{
	if (auto Widget = Cast<UDreamWidget>(InObject))
	{
		if (InPropertyName == UDreamWidget::GetPropertyName_AnchorData())
		{
			Widget->CalculateTransformFromAnchor();//calculate transform here, because when NotifyPropertyChanged the PostActorConstruction->MoveComponent will call then anchor will calculate from transform value which is wrong
			this->RemoveMemberPropertyFromSubPrefab(Widget, InObject, UDreamWidget::GetPropertyName_RelativeLocation());//remove RelativeLocation override because Widget use AnchorData to calculate RelativeLocation
		}
	}
}

void UDreamUIPrefabHelperObject::ApplyPrefabPropertyValue(UObject* ContextObject, FProperty* Property, void* ContainerPointerInSrc, void* ContainerPointerInPrefab, const FDreamUISubPrefabData& SubPrefabData, int RawArrayIndex, bool IsInsideRawArray)
{
	if (Property->ArrayDim > 1 && !IsInsideRawArray)
	{
		for (int i = 0; i < Property->ArrayDim; i++)
		{
			ApplyPrefabPropertyValue(ContextObject, Property, ContainerPointerInSrc, ContainerPointerInPrefab, SubPrefabData, i, true);
		}
		return;
	}
	bool bPropertySupportDirectCopyValue = false;
	if (CastField<FClassProperty>(Property) != nullptr)
	{
		bPropertySupportDirectCopyValue = true;
	}
	else if (auto ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (auto ObjectInParent = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerPointerInSrc))
		{
			auto ObjectClass = ObjectInParent->GetClass();
			if (ObjectInParent->IsAsset())
			{
				bPropertySupportDirectCopyValue = true;
			}
			else
			{
				//search object in guid
				FGuid ObjectGuidInParent;
				for (auto& KeyValue : this->MapGuidToObject)
				{
					if (KeyValue.Value == ObjectInParent)
					{
						ObjectGuidInParent = KeyValue.Key;
						break;
					}
				}
				FGuid ObjectGuidInPrefab;
				//find valid guid, get the guid in prefab, with the guid then get object, so that object is the real value
				if (ObjectGuidInParent.IsValid()
					&& SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Contains(ObjectGuidInParent)//check if the guid exist in this sub-prefab, because there could be multiple sub-prefabs
					)
				{
					ObjectGuidInPrefab = SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab[ObjectGuidInParent];
				}
				if (ObjectGuidInPrefab.IsValid())
				{
					UDreamUIPrefabHelperObject* SubPrefabHelper = SubPrefabData.PrefabAsset->GetPrefabHelperObject();
					const TObjectPtr<UObject>* ObjectInPrefabPtr = IsValid(SubPrefabHelper)
						? SubPrefabHelper->MapGuidToObject.Find(ObjectGuidInPrefab)
						: nullptr;
					UObject* ObjectInPrefab = ObjectInPrefabPtr ? ObjectInPrefabPtr->Get() : nullptr;
					if (ObjectClass->IsChildOf(UDreamWidget::StaticClass())
						|| ObjectClass->IsChildOf(UDreamWidgetSubObjectBehaviour::StaticClass())
						|| ObjectClass->IsChildOf(UDreamUIBehaviour::StaticClass())
						)
					{
						if (IsValid(ObjectInPrefab))
						{
							ObjectProperty->SetObjectPropertyValue_InContainer(ContainerPointerInPrefab, ObjectInPrefab, RawArrayIndex);
						}
						else
						{
							UE_LOG(DreamGUI, Warning,
								TEXT("Cannot apply nested prefab reference '%s' because its source object mapping is missing."),
								*Property->GetName());
						}
					}
					else
					{
						if (ObjectClass->HasAnyClassFlags(EClassFlags::CLASS_EditInlineNew)
							//&& ObjectProperty->HasAnyPropertyFlags(EPropertyFlags::CPF_InstancedReference)//is this necessary?
							)
						{
							if (!IsValid(ObjectInPrefab))
							{
								//EditInlineNew object can create new one
								ObjectInPrefab = NewObject<UObject>(ContextObject, ObjectClass, NAME_None, RF_NoFlags, ObjectInParent);
								if (IsValid(ObjectInPrefab) && IsValid(SubPrefabHelper))
								{
									SubPrefabHelper->MapGuidToObject.Add(ObjectGuidInPrefab, ObjectInPrefab);
								}
							}
							if (IsValid(ObjectInPrefab))
							{
								ObjectProperty->SetObjectPropertyValue_InContainer(ContainerPointerInPrefab, ObjectInPrefab, RawArrayIndex);
								for (const auto PropertyItem : TFieldRange<FProperty>(ObjectClass))//check property inside object
								{
									ApplyPrefabPropertyValue(ObjectInPrefab, PropertyItem, ObjectInParent, ObjectInPrefab, SubPrefabData);
								}
							}
						}
						else
						{
							auto InfoText = FText::Format(LOCTEXT("ApplyPrefabPropertyValue_MissingConditionWarning", "DreamUI have not handle this condition:\nobject: '{0}'\nobjectClass: '{1}'")
								, FText::FromString(ObjectInParent->GetPathName()), FText::FromString(ObjectClass->GetPathName()));
							UE_LOG(DreamGUI, Warning, TEXT("%s"), *InfoText.ToString());
							FDreamUIUtils::EditorNotification(InfoText, false);
						}
					}
				}
				else
				{
					auto InfoText = FText::Format(LOCTEXT("ApplyPrefabPropertyValue_ReferencingOuterObject", "This property '{0}' is referencing object which is not belongs to this prefab, will ignore it.")
						, FText::FromString(ObjectProperty->GetPathName()));
					UE_LOG(DreamGUI, Log, TEXT("%s"), *InfoText.ToString());
					FDreamUIUtils::EditorNotification(InfoText, false);
				}
			}
		}
		else
		{
			bPropertySupportDirectCopyValue = true;
		}
	}
	else if (auto ArrayProperty = CastField<FArrayProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInPrefab, ContainerPointerInSrc);//just copy so we don't need to resize it
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(ContainerPointerInSrc, RawArrayIndex));
		FScriptArrayHelper ArrayHelperForDst(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(ContainerPointerInPrefab, RawArrayIndex));
		for (int i = 0; i < ArrayHelper.Num(); i++)
		{
			ApplyPrefabPropertyValue(ContextObject, ArrayProperty->Inner, ArrayHelper.GetRawPtr(i), ArrayHelperForDst.GetRawPtr(i), SubPrefabData);
		}
	}
	else if (auto MapProperty = CastField<FMapProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInPrefab, ContainerPointerInSrc);//just copy so we don't need to resize it
		FScriptMapHelper MapHelper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(ContainerPointerInSrc, RawArrayIndex));
		FScriptMapHelper MapHelperForDst(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(ContainerPointerInPrefab, RawArrayIndex));
		for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
		{
			if (!MapHelper.IsValidIndex(Index) || !MapHelperForDst.IsValidIndex(Index))continue;
			ApplyPrefabPropertyValue(ContextObject, MapProperty->KeyProp, MapHelper.GetKeyPtr(Index), MapHelperForDst.GetKeyPtr(Index), SubPrefabData);
			ApplyPrefabPropertyValue(ContextObject, MapProperty->ValueProp, MapHelper.GetPairPtr(Index), MapHelperForDst.GetPairPtr(Index), SubPrefabData);
		}
		MapHelperForDst.Rehash();
	}
	else if (auto SetProperty = CastField<FSetProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInPrefab, ContainerPointerInSrc);//just copy so we don't need to resize it
		FScriptSetHelper SetHelper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(ContainerPointerInSrc, RawArrayIndex));
		FScriptSetHelper SetHelperForDst(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(ContainerPointerInPrefab, RawArrayIndex));
		for (int32 Index = 0; Index < SetHelper.GetMaxIndex(); ++Index)
		{
			if (!SetHelper.IsValidIndex(Index) || !SetHelperForDst.IsValidIndex(Index))continue;
			ApplyPrefabPropertyValue(ContextObject, SetProperty->ElementProp, SetHelper.GetElementPtr(Index), SetHelperForDst.GetElementPtr(Index), SubPrefabData);
		}
		SetHelperForDst.Rehash();
	}
	else if (auto StructProperty = CastField<FStructProperty>(Property))
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInPrefab, ContainerPointerInSrc);
		auto StructPtr = Property->ContainerPtrToValuePtr<uint8>(ContainerPointerInSrc, RawArrayIndex);
		auto StructPtrForDst = Property->ContainerPtrToValuePtr<uint8>(ContainerPointerInPrefab, RawArrayIndex);
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			ApplyPrefabPropertyValue(ContextObject, *It, StructPtr, StructPtrForDst, SubPrefabData);
		}
	}
	else
	{
		bPropertySupportDirectCopyValue = true;
	}
	if (bPropertySupportDirectCopyValue)
	{
		Property->CopyCompleteValue_InContainer(ContainerPointerInPrefab, ContainerPointerInSrc);
	}
}
void UDreamUIPrefabHelperObject::ApplyPrefabOverride(UObject* InObject, const TArray<FName>& InPropertyNames)
{
	if (!IsValid(InObject))return;
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}
	if (!IsValid(Widget))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot apply prefab override for '%s' without an owning widget."), *InObject->GetPathName());
		return;
	}
	auto SubPrefabRootWidget = GetSubPrefabRootWidget(Widget);
	auto SubPrefabDataPtr = SubPrefabMap.Find(SubPrefabRootWidget);
	if (!SubPrefabDataPtr || !IsValid(SubPrefabDataPtr->PrefabAsset))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot apply prefab override for '%s' because its sub-prefab data is missing."), *InObject->GetPathName());
		return;
	}
	auto SubPrefabAsset = SubPrefabDataPtr->PrefabAsset.Get();
	auto SubPrefabHelperObject = SubPrefabAsset->GetPrefabHelperObject();
	if (!IsValid(SubPrefabHelperObject))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot apply prefab override for '%s' because the source prefab helper is unavailable."), *InObject->GetPathName());
		return;
	}
	FGuid ObjectGuid;
	for (auto& KeyValue : MapGuidToObject)
	{
		if (KeyValue.Value == InObject)
		{
			ObjectGuid = KeyValue.Key;
			break;
		}
	}
	const FGuid* ObjectGuidInSubPrefab = ObjectGuid.IsValid()
		? SubPrefabDataPtr->MapObjectGuidFromParentPrefabToSubPrefab.Find(ObjectGuid)
		: nullptr;
	const TObjectPtr<UObject>* ObjectInPrefabPtr = ObjectGuidInSubPrefab
		? SubPrefabHelperObject->MapGuidToObject.Find(*ObjectGuidInSubPrefab)
		: nullptr;
	UObject* ObjectInPrefab = ObjectInPrefabPtr ? ObjectInPrefabPtr->Get() : nullptr;
	if (!IsValid(ObjectInPrefab))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Cannot apply prefab override for '%s' because its source object mapping is missing."), *InObject->GetPathName());
		return;
	}
	const FDreamUISubPrefabData SubPrefabData = *SubPrefabDataPtr;
	const FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ApplyPrefabOnObjectProperties", "Apply Prefab Override: {0}"),
		FText::FromString(InObject->GetName())));
	InObject->Modify();
	this->Modify();

	bCanCollectProperty = false;
	{
		for (auto PropertyName : InPropertyNames)
		{
			if (auto Property = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), PropertyName))
			{
				//set to default value
				ApplyPrefabPropertyValue(ObjectInPrefab, Property, InObject, ObjectInPrefab, SubPrefabData);
				AfterObjectPropertyApplyOrRevert(InObject, PropertyName);
				//delete item
				RemoveMemberPropertyFromSubPrefab(Widget, InObject, PropertyName);
				//notify
				FDreamUIUtils::NotifyPropertyChanged(ObjectInPrefab, Property);

				SetAnythingDirty();
				
				auto RelatedPropertyName = GetExtraRelatedPropertyForApplyOrRevert(InObject, PropertyName);
				if (RelatedPropertyName != NAME_None)
				{
					if (auto RelatedProperty = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), RelatedPropertyName))
					{
						//set to default value
						ApplyPrefabPropertyValue(ObjectInPrefab, RelatedProperty, InObject, ObjectInPrefab, SubPrefabData);
						AfterObjectPropertyApplyOrRevert(InObject, RelatedPropertyName);
						//delete item
						RemoveMemberPropertyFromSubPrefab(Widget, InObject, RelatedPropertyName);
					}
				}
			}
		}
		//save origin prefab
		if (bAnythingDirty)
		{
			//mark on sub prefab, because the object could belongs to subprefab's subprefab.
			SubPrefabAsset->GetPrefabHelperObject()->MarkOverrideParameterFromParentPrefab(ObjectInPrefab, InPropertyNames);

			SubPrefabAsset->Modify();
			SubPrefabAsset->GetPrefabHelperObject()->SavePrefab();
		}
	}
	bCanCollectProperty = true;
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
	//when apply or revert parameters in level editor, means we accept sub-prefab's current version, so we mark the version to newest, and we won't get 'update warning'.
	RefreshSubPrefabVersion(SubPrefabRootWidget);
}
void UDreamUIPrefabHelperObject::ApplyAllOverrideToPrefab(UObject* InObject)
{
	if (!IsValid(InObject))return;
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}
	if (!IsValid(Widget))return;
	auto SubPrefabRootWidget = GetSubPrefabRootWidget(Widget);
	auto SubPrefabDataPtr = SubPrefabMap.Find(SubPrefabRootWidget);
	if (!SubPrefabDataPtr || !IsValid(SubPrefabDataPtr->PrefabAsset))return;
	auto SubPrefabData = *SubPrefabDataPtr;
	auto SubPrefabAsset = SubPrefabData.PrefabAsset.Get();
	auto SubPrefabHelperObject = SubPrefabAsset->GetPrefabHelperObject();
	if (!IsValid(SubPrefabHelperObject))return;

	bCanCollectProperty = false;
	{
		GEditor->BeginTransaction(LOCTEXT("ApplyPrefabOnAll_Transaction", "Apply Prefab Override"));
		for (int i = 0; i < SubPrefabData.ObjectOverrideParameterArray.Num(); i++)
		{
			auto& DataItem = SubPrefabData.ObjectOverrideParameterArray[i];
			DataItem.Object->Modify();
		}
		this->Modify();

		auto FindOriginObjectInSourcePrefab = [&](UObject* Object) -> UObject* {
			FGuid ObjectGuid;
			for (auto& KeyValue : MapGuidToObject)
			{
				if (KeyValue.Value == Object)
				{
					ObjectGuid = KeyValue.Key;
					break;
				}
			}
			const FGuid* ObjectGuidInSubPrefab = ObjectGuid.IsValid()
				? SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Find(ObjectGuid)
				: nullptr;
			const TObjectPtr<UObject>* SourceObject = ObjectGuidInSubPrefab
				? SubPrefabHelperObject->MapGuidToObject.Find(*ObjectGuidInSubPrefab)
				: nullptr;
			return SourceObject ? SourceObject->Get() : nullptr;
		};
		for (int i = 0; i < SubPrefabData.ObjectOverrideParameterArray.Num(); i++)
		{
			auto& DataItem = SubPrefabData.ObjectOverrideParameterArray[i];
			auto SourceObject = DataItem.Object.Get();
			TSet<FName> FilterNameSet;
			if (SourceObject == SubPrefabRootWidget)//if is root widget of prefab, then skip it's transform
			{
				FilterNameSet.Add(UDreamWidget::GetPropertyName_RelativeLocation());
				FilterNameSet.Add(UDreamWidget::GetPropertyName_RelativeRotation());
				FilterNameSet.Add(UDreamWidget::GetPropertyName_RelativeScale());
			}
			if (auto ObjectInPrefab = FindOriginObjectInSourcePrefab(SourceObject))
			{
				TSet<FName> NamesToClear;
				for (auto PropertyName : DataItem.MemberPropertyNames)
				{
					if (FilterNameSet.Contains(PropertyName))continue;
					NamesToClear.Add(PropertyName);
					if (auto Property = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), PropertyName))
					{
						//set to default value
						ApplyPrefabPropertyValue(ObjectInPrefab, Property, SourceObject, ObjectInPrefab, SubPrefabData);
						AfterObjectPropertyApplyOrRevert(InObject, PropertyName);
						//notify
						FDreamUIUtils::NotifyPropertyChanged(ObjectInPrefab, Property);

						auto RelatedPropertyName = GetExtraRelatedPropertyForApplyOrRevert(InObject, PropertyName);
						if (RelatedPropertyName != NAME_None)
						{
							NamesToClear.Add(RelatedPropertyName);
							if (auto RelatedProperty = FindFProperty<FProperty>(ObjectInPrefab->GetClass(), RelatedPropertyName))
							{
								ApplyPrefabPropertyValue(ObjectInPrefab, RelatedProperty, SourceObject, ObjectInPrefab, SubPrefabData);
								AfterObjectPropertyApplyOrRevert(InObject, RelatedPropertyName);
							}
						}
					}
				}
				//mark on sub prefab, because the object could belongs to subprefab's subprefab.
				SubPrefabAsset->GetPrefabHelperObject()->MarkOverrideParameterFromParentPrefab(ObjectInPrefab, DataItem.MemberPropertyNames);

				for (auto& PropertyName : NamesToClear)
				{
					DataItem.MemberPropertyNames.Remove(PropertyName);
				}
			}
			else//if not find OriginObject, means the SourceObject is newly created (added new component) @todo: automatic add component to origin prefab
			{
				if (SourceObject->IsA(UDreamUIBehaviour::StaticClass()))
				{
					auto InfoText = FText::Format(LOCTEXT("NewComponentInPrefabInstance", "Detect none tracked component: '{0}' in PrefabInstance. Note children of a Prefab instance cannot add or remove component.\
\n\nYou can open the prefab in prefab editor to add component to the prefab asset itself, or unpack the prefab instance to remove its prefab connection."), FText::FromString(SourceObject->GetName()));
					FMessageDialog::Open(EAppMsgType::Ok, InfoText);
				}
			}
		}
		RemoveAllMemberPropertyFromSubPrefab(SubPrefabRootWidget, false);
		//save origin prefab
		{
			SubPrefabAsset->Modify();
			SubPrefabAsset->GetPrefabHelperObject()->SavePrefab();
		}

		SetAnythingDirty();
		GEditor->EndTransaction();
	}
	bCanCollectProperty = true;
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
	//when apply or revert parameters in level editor, means we accept sub-prefab's current version, so we mark the version to newest, and we won't get 'update warning'.
	RefreshSubPrefabVersion(GetSubPrefabRootWidget(Widget));
}
#pragma endregion RevertAndApply

void UDreamUIPrefabHelperObject::RefreshSubPrefabVersion(UDreamWidget* InSubPrefabRootWidget)
{
	if (InSubPrefabRootWidget != nullptr)
	{
		FDreamUISubPrefabData* SubPrefabData = SubPrefabMap.Find(InSubPrefabRootWidget);
		if (SubPrefabData && IsValid(SubPrefabData->PrefabAsset))
		{
			SubPrefabData->OverallVersionMD5 = SubPrefabData->PrefabAsset->GenerateOverallVersionMD5();
		}
	}
	else
	{
		for (auto& KeyValue : SubPrefabMap)
		{
			if (IsValid(KeyValue.Value.PrefabAsset))
			{
				KeyValue.Value.OverallVersionMD5 = KeyValue.Value.PrefabAsset->GenerateOverallVersionMD5();
			}
		}
	}
}

void UDreamUIPrefabHelperObject::MakePrefabAsSubPrefab(UDreamUIPrefab* InPrefab, UDreamWidget* InWidget, const TMap<FGuid, TObjectPtr<UObject>>& InSubMapGuidToObject, const TArray<FDreamUIPrefabOverrideParameterData>& InObjectOverrideParameterArray)
{
	FDreamUISubPrefabData SubPrefabData;
	SubPrefabData.PrefabAsset = InPrefab;
	SubPrefabData.OverallVersionMD5 = InPrefab->GenerateOverallVersionMD5();
	SubPrefabData.MapGuidToObject = InSubMapGuidToObject;
	SubPrefabData.ObjectOverrideParameterArray = InObjectOverrideParameterArray;
	
	auto FindOrAddSubPrefabObjectGuidInParentPrefab = [&](UObject* InObject) {
		for (auto& KeyValue : MapGuidToObject)
		{
			if (KeyValue.Value == InObject)
			{
				return KeyValue.Key;
			}
		}
		auto NewGuid = FGuid::NewGuid();
		MapGuidToObject.Add(NewGuid, InObject);
		return NewGuid;
	};
	for (auto& KeyValue : InSubMapGuidToObject)
	{
		auto GuidInParentPrefab = FindOrAddSubPrefabObjectGuidInParentPrefab(KeyValue.Value);
		if (!SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Contains(GuidInParentPrefab))
		{
			SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(GuidInParentPrefab, KeyValue.Key);
		}
	}
	SubPrefabMap.Add(InWidget, SubPrefabData);
	//mark SiblingIndex as default override parameter
	this->AddMemberPropertyToSubPrefab(InWidget, InWidget, UDreamWidget::GetPropertyName_SiblingIndex());

	SetAnythingDirty();
}

void UDreamUIPrefabHelperObject::RemoveSubPrefabByRootWidget(UDreamWidget* InPrefabRootWidget)
{
	if (SubPrefabMap.Contains(InPrefabRootWidget))
	{
		auto SubPrefabData = SubPrefabMap[InPrefabRootWidget];
		for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
		{
			MapGuidToObject.Remove(KeyValue.Key);
		}
		SubPrefabMap.Remove(InPrefabRootWidget);
	}
#if WITH_EDITOR
	else if (MissingPrefab.Contains(InPrefabRootWidget))
	{
		MissingPrefab.Remove(InPrefabRootWidget);
	}
#endif
	ClearInvalidObjectAndGuid();
}

void UDreamUIPrefabHelperObject::RemoveSubPrefabByAnyWidgetOfSubPrefab(UDreamWidget* InPrefabWidget)
{
	if (auto RootWidget = GetSubPrefabRootWidget(InPrefabWidget))
	{
		RemoveSubPrefabByRootWidget(RootWidget);
	}
}

UDreamUIPrefab* UDreamUIPrefabHelperObject::GetPrefabAssetBySubPrefabObject(UObject* InObject)
{
	auto Widget = Cast<UDreamWidget>(InObject);
	if (!Widget)
	{
		Widget = InObject->GetTypedOuter<UDreamWidget>();
	}
	return GetSubPrefabData(Widget).PrefabAsset;
}

bool UDreamUIPrefabHelperObject::CleanupInvalidSubPrefab()
{
	bool bAnythingChanged = false;

	{
		//invalid sub prefab
		TSet<UDreamWidget*> SubPrefabKeysToRemove;
		for (auto& KeyValue : SubPrefabMap)
		{
			if (!IsValid(KeyValue.Key) || !IsValid(KeyValue.Value.PrefabAsset))
			{
				SubPrefabKeysToRemove.Add(KeyValue.Key);
#if WITH_EDITOR
				if (IsValid(KeyValue.Key))
				{
					MissingPrefab.Add(KeyValue.Key);
				}
#endif
			}
		}
		//invalid guid mapped object
		TSet<FGuid> GuidKeysToRemove;
		for (auto& Item : SubPrefabKeysToRemove)
		{
			if (const FDreamUISubPrefabData* SubPrefabData = SubPrefabMap.Find(Item))
			{
				for (const TPair<FGuid, FGuid>& GuidPair : SubPrefabData->MapObjectGuidFromParentPrefabToSubPrefab)
				{
					GuidKeysToRemove.Add(GuidPair.Key);
				}
			}
			SubPrefabMap.Remove(Item);
			//cleanup MapGuidToObject, because these objects could belong to the sub prefab being removed
			for (auto& GuidToObjectKeyValue : MapGuidToObject)
			{
				if (!IsValid(GuidToObjectKeyValue.Value)
					|| (IsValid(Item)
						&& (GuidToObjectKeyValue.Value->IsInOuter(Item) || GuidToObjectKeyValue.Value == Item)))
				{
					GuidKeysToRemove.Add(GuidToObjectKeyValue.Key);
				}
			}
		}
		if (SubPrefabKeysToRemove.Num() > 0)
		{
			if (OnSubPrefabNewVersionUpdated.IsBound())
			{
				OnSubPrefabNewVersionUpdated.Broadcast();
			}
		}
		for (auto& Item : GuidKeysToRemove)
		{
			MapGuidToObject.Remove(Item);
		}
		bAnythingChanged = SubPrefabKeysToRemove.Num() > 0 || GuidKeysToRemove.Num() > 0;
		if (bAnythingChanged)
		{
			SetAnythingDirty();
		}
#if WITH_EDITOR
		for (auto It = MissingPrefab.CreateIterator(); It; ++It)
		{
			if (!IsValid(*It))
			{
				It.RemoveCurrent();
			}
		}
#endif
	}
	return bAnythingChanged;
}
bool UDreamUIPrefabHelperObject::GetAnythingDirty()const
{
	return bAnythingDirty; 
}
void UDreamUIPrefabHelperObject::SetAnythingDirty()
{
	bAnythingDirty = true;
	if (IsValid(PrefabAsset))
	{
		PrefabAsset->MarkPackageDirty();
	}
}

#if WITH_EDITOR
#include "Editor.h"
#endif
void UDreamUIPrefabHelperObject::CheckPrefabVersion()
{
	bool bAnythingChanged = CleanupInvalidSubPrefab();
	for (auto& KeyValue : SubPrefabMap)
	{
		auto& SubPrefabData = KeyValue.Value;
		if (SubPrefabData.OverallVersionMD5 != SubPrefabData.PrefabAsset->GenerateOverallVersionMD5())
		{
			this->RefreshOnSubPrefabDirty(SubPrefabData.PrefabAsset, KeyValue.Key);
			bAnythingChanged = true;
		}
	}

	if (this->ClearInvalidObjectAndGuid())
	{
		bAnythingChanged = true;
	}
	if (bAnythingChanged && IsValid(PrefabAsset))
	{
		this->PrefabAsset->MarkPackageDirty();
	}
}

UDreamUIPrefabHelperObject* UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))return nullptr;
	for (TObjectIterator<UDreamUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		if (Itr->IsWidgetBelongsToThis(InWidget))
		{
			return *Itr;
		}
	}
	return nullptr;
}

bool UDreamUIPrefabHelperObject::IsWidgetInsideSubPrefabInstance(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))return false;
	for (TObjectIterator<UDreamUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		if (Itr->IsWidgetBelongsToThis(InWidget))
		{
			return Itr->IsWidgetBelongsToSubPrefab(InWidget);
		}
	}
	return false;
}

#endif


#undef LOCTEXT_NAMESPACE
