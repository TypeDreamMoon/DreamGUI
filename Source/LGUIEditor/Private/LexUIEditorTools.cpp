// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIEditorTools.h"
#include "Core/LexUIManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/EngineTypes.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Widgets/SViewport.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "DataFactory/LexUIPrefabActorFactory.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include LGUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "LGUIEditorModule.h"
#include "PrefabEditor/LexUIPrefabEditor.h"
#include "Core/Components/LexLayout.h"
#include "Core/Actor/LexWidgetRootActor.h"
#include "Event/LexEventSystem.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "Logging/MessageLog.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LGUIEditorTools"



FEditingPrefabChangedDelegate FLexUIEditorTools::OnEditingPrefabChanged;
FBeforeApplyPrefabDelegate FLexUIEditorTools::OnBeforeApplyPrefab;

namespace ReattachActorsHelper
{
	/** Holds the actor and socket name for attaching. */
	struct FActorAttachmentInfo
	{
		AActor* Actor;

		FName SocketName;
	};

	/** Used to cache the attachment info for an actor. */
	struct FActorAttachmentCache
	{
	public:
		/** The post-conversion actor. */
		AActor* NewActor;

		/** The parent actor and socket. */
		FActorAttachmentInfo ParentActor;

		/** Children actors and the sockets they were attached to. */
		TArray<FActorAttachmentInfo> AttachedActors;
	};

	/**
	 * Caches the attachment info for the actors being converted.
	 *
	 * @param InActorsToReattach			List of actors to reattach.
	 * @param InOutAttachmentInfo			List of attachment info for the list of actors.
	 */
	void CacheAttachments(const TArray<AActor*>& InActorsToReattach, TArray<FActorAttachmentCache>& InOutAttachmentInfo)
	{
		for (int32 ActorIdx = 0; ActorIdx < InActorsToReattach.Num(); ++ActorIdx)
		{
			AActor* ActorToReattach = InActorsToReattach[ActorIdx];

			InOutAttachmentInfo.AddZeroed();

			FActorAttachmentCache& CurrentAttachmentInfo = InOutAttachmentInfo[ActorIdx];

			// Retrieve the list of attached actors.
			TArray<AActor*> AttachedActors;
			ActorToReattach->GetAttachedActors(AttachedActors);

			// Cache the parent actor and socket name.
			CurrentAttachmentInfo.ParentActor.Actor = ActorToReattach->GetAttachParentActor();
			CurrentAttachmentInfo.ParentActor.SocketName = ActorToReattach->GetAttachParentSocketName();

			// Required to restore attachments properly.
			for (int32 AttachedActorIdx = 0; AttachedActorIdx < AttachedActors.Num(); ++AttachedActorIdx)
			{
				// Store the attached actor and socket name in the cache.
				CurrentAttachmentInfo.AttachedActors.AddZeroed();
				CurrentAttachmentInfo.AttachedActors[AttachedActorIdx].Actor = AttachedActors[AttachedActorIdx];
				CurrentAttachmentInfo.AttachedActors[AttachedActorIdx].SocketName = AttachedActors[AttachedActorIdx]->GetAttachParentSocketName();

				AActor* ChildActor = CurrentAttachmentInfo.AttachedActors[AttachedActorIdx].Actor;
				ChildActor->Modify();
				ChildActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			}

			// Modify the actor so undo will reattach it.
			ActorToReattach->Modify();
			ActorToReattach->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
	}

	/**
	 * Caches the actor old/new information, mapping the old actor to the new version for easy look-up and matching.
	 *
	 * @param InOldActor			The old version of the actor.
	 * @param InNewActor			The new version of the actor.
	 * @param InOutReattachmentMap	Map object for placing these in.
	 * @param InOutAttachmentInfo	Update the required attachment info to hold the Converted Actor.
	 */
	void CacheActorConvert(AActor* InOldActor, AActor* InNewActor, TMap<AActor*, AActor*>& InOutReattachmentMap, FActorAttachmentCache& InOutAttachmentInfo)
	{
		// Add mapping data for the old actor to the new actor.
		InOutReattachmentMap.Add(InOldActor, InNewActor);

		// Set the converted actor so re-attachment can occur.
		InOutAttachmentInfo.NewActor = InNewActor;
	}

	/**
	 * Checks if two actors can be attached, creates Message Log messages if there are issues.
	 *
	 * @param InParentActor			The parent actor.
	 * @param InChildActor			The child actor.
	 * @param InOutErrorMessages	Errors with attaching the two actors are stored in this array.
	 *
	 * @return Returns true if the actors can be attached, false if they cannot.
	 */
	bool CanParentActors(AActor* InParentActor, AActor* InChildActor)
	{
		FText ReasonText;
		if (GEditor->CanParentActors(InParentActor, InChildActor, &ReasonText))
		{
			return true;
		}
		else
		{
			FMessageLog("EditorErrors").Error(ReasonText);
			return false;
		}
	}

	/**
	 * Reattaches actors to maintain the hierarchy they had previously using a conversion map and an array of attachment info. All errors displayed in Message Log along with notifications.
	 *
	 * @param InReattachmentMap			Used to find the corresponding new versions of actors using an old actor pointer.
	 * @param InAttachmentInfo			Holds parent and child attachment data.
	 */
	void ReattachActors(TMap<AActor*, AActor*>& InReattachmentMap, TArray<FActorAttachmentCache>& InAttachmentInfo)
	{
		// Holds the errors for the message log.
		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.NewPage(LOCTEXT("AttachmentLogPage", "Actor Reattachment"));

		for (int32 ActorIdx = 0; ActorIdx < InAttachmentInfo.Num(); ++ActorIdx)
		{
			FActorAttachmentCache& CurrentAttachment = InAttachmentInfo[ActorIdx];

			// Need to reattach all of the actors that were previously attached.
			for (int32 AttachedIdx = 0; AttachedIdx < CurrentAttachment.AttachedActors.Num(); ++AttachedIdx)
			{
				// Check if the attached actor was converted. If it was it will be in the TMap.
				AActor** CheckIfConverted = InReattachmentMap.Find(CurrentAttachment.AttachedActors[AttachedIdx].Actor);
				if (CheckIfConverted)
				{
					// This should always be valid.
					if (*CheckIfConverted)
					{
						AActor* ParentActor = CurrentAttachment.NewActor;
						AActor* ChildActor = *CheckIfConverted;

						if (CanParentActors(ParentActor, ChildActor))
						{
							// Attach the previously attached and newly converted actor to the current converted actor.
							ChildActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform, CurrentAttachment.AttachedActors[AttachedIdx].SocketName);
						}
					}

				}
				else
				{
					AActor* ParentActor = CurrentAttachment.NewActor;
					AActor* ChildActor = CurrentAttachment.AttachedActors[AttachedIdx].Actor;

					if (CanParentActors(ParentActor, ChildActor))
					{
						// Since the actor was not converted, reattach the unconverted actor.
						ChildActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform, CurrentAttachment.AttachedActors[AttachedIdx].SocketName);
					}
				}

			}

			// Check if the parent was converted.
			AActor** CheckIfNewActor = InReattachmentMap.Find(CurrentAttachment.ParentActor.Actor);
			if (CheckIfNewActor)
			{
				// Since the actor was converted, attach the current actor to it.
				if (*CheckIfNewActor)
				{
					AActor* ParentActor = *CheckIfNewActor;
					AActor* ChildActor = CurrentAttachment.NewActor;

					if (CanParentActors(ParentActor, ChildActor))
					{
						ChildActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform, CurrentAttachment.ParentActor.SocketName);
					}
				}

			}
			else
			{
				AActor* ParentActor = CurrentAttachment.ParentActor.Actor;
				AActor* ChildActor = CurrentAttachment.NewActor;

				// Verify the parent is valid, the actor may not have actually been attached before.
				if (ParentActor && CanParentActors(ParentActor, ChildActor))
				{
					// The parent was not converted, attach to the unconverted parent.
					ChildActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform, CurrentAttachment.ParentActor.SocketName);
				}
			}
		}

		// Add the errors to the message log, notifications will also be displayed as needed.
		EditorErrors.Notify(NSLOCTEXT("ActorAttachmentError", "AttachmentsFailed", "Attachments Failed!"));
	}
}

struct FLexUIEditorToolsHelperFunctionHolder
{
public:
	static TArray<AActor*> ConvertSelectionToActors(USelection* InSelection)
	{
		TArray<AActor*> result;
		auto count = InSelection->Num();
		for (int i = 0; i < count; i++)
		{
			auto obj = (AActor*)(InSelection->GetSelectedObject(i));
			if (obj != nullptr)
			{
				result.Add(obj);
			}
		}
		return result;
	}
	static FString GetLabelPrefixForCopy(const FString& srcActorLabel, FString& outNumetricSuffix)
	{
		int rightCount = 1;
		while (rightCount <= srcActorLabel.Len() && srcActorLabel.Right(rightCount).IsNumeric())
		{
			rightCount++;
		}
		rightCount--;
		outNumetricSuffix = srcActorLabel.Right(rightCount);
		return srcActorLabel.Left(srcActorLabel.Len() - rightCount);
	}
public:
	static FString GetCopiedActorLabel(AActor* Parent, FString OriginActorLabel, UWorld* World)
	{
		TArray<AActor*> SameParentActorList;//all actors attached at same parent actor. if parent is null then get all actors
		for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
		{
			if (AActor* itemActor = *ActorItr)
			{
				if (IsValid(itemActor))
				{
					if (IsValid(Parent))
					{
						if (itemActor->GetAttachParentActor() == Parent)
						{
							SameParentActorList.Add(itemActor);
						}
					}
					else
					{
						if (itemActor->GetAttachParentActor() == nullptr)
						{
							SameParentActorList.Add(itemActor);
						}
					}
				}
			}
		}
	

		FString MaxNumetricSuffixStr = TEXT("");//numetric suffix
		OriginActorLabel = GetLabelPrefixForCopy(OriginActorLabel, MaxNumetricSuffixStr);
		int MaxNumetricSuffixStrLength = MaxNumetricSuffixStr.Len();
		int SameNameActorCount = 0;//if actor name is same with source name, then collect it
		for (int i = 0; i < SameParentActorList.Num(); i ++)//search from same level actors, and get the right suffix
		{
			auto item = SameParentActorList[i];
			auto itemActorLabel = item->GetActorLabel();
			if (itemActorLabel == OriginActorLabel)SameNameActorCount++;
			if (OriginActorLabel.Len() == 0 || itemActorLabel.StartsWith(OriginActorLabel))
			{
				auto itemRightStr = itemActorLabel.Right(itemActorLabel.Len() - OriginActorLabel.Len());
				if (!itemRightStr.IsNumeric())//if rest is not numetric
				{
					continue;
				}
				FString itemNumetrixSuffixStr = itemRightStr;
				int itemNumetrix = FCString::Atoi(*itemNumetrixSuffixStr);
				int maxNumetrixSuffix = FCString::Atoi(*MaxNumetricSuffixStr);
				if (itemNumetrix > maxNumetrixSuffix)
				{
					maxNumetrixSuffix = itemNumetrix;
					MaxNumetricSuffixStr = FString::Printf(TEXT("%d"), maxNumetrixSuffix);
				}
			}
		}
		FString CopiedActorLabel = OriginActorLabel;
		if (!MaxNumetricSuffixStr.IsEmpty() || SameNameActorCount > 0)
		{
			int MaxNumtrixSuffix = FCString::Atoi(*MaxNumetricSuffixStr);
			MaxNumtrixSuffix++;
			FString NumetrixSuffixStr = FString::Printf(TEXT("%d"), MaxNumtrixSuffix);
			while (NumetrixSuffixStr.Len() < MaxNumetricSuffixStrLength)
			{
				NumetrixSuffixStr = TEXT("0") + NumetrixSuffixStr;
			}
			CopiedActorLabel += NumetrixSuffixStr;
		}
		return CopiedActorLabel;
	}
	
public:
	static TArray<UActorComponent*> ConvertSelectionToComponents(USelection* InSelection)
	{
		TArray<UActorComponent*> result;
		auto count = InSelection->Num();
		for (int i = 0; i < count; i++)
		{
			auto obj = (UActorComponent*)(InSelection->GetSelectedObject(i));
			if (obj != nullptr)
			{
				result.Add(obj);
			}
		}
		return result;
	}
};

TMap<FString, TWeakObjectPtr<ULexUIPrefab>> FLexUIEditorTools::CopiedActorPrefabMap;

FString FLexUIEditorTools::LexUIPresetPrefabPath = TEXT("/LGUI/Prefabs/");

FString FLexUIEditorTools::GetUniqueNumericName(const FString& InPrefix, const TArray<FString>& InExistNames)
{
	auto ExtractNumeric = [](const FString& InString, int32& Num) {
		int NumericStringIndex = -1;
		FString SubNumericString;
		int NumericStringCharCount = 0;
		for (int i = InString.Len() - 1; i >= 0; i--)
		{
			auto SubChar = InString[i];
			if (SubChar >= '0' && SubChar <= '9')
			{
				NumericStringIndex = i;

				NumericStringCharCount++;
				if (NumericStringCharCount >= 4)
				{
					break;
				}
			}
			else
			{
				break;
			}
		}
		if (NumericStringIndex != -1)
		{
			auto NumetricSubString = InString.Right(InString.Len() - NumericStringIndex);
			Num = FCString::Atoi(*NumetricSubString);
			return true;
		}
		else
		{
			return false;
		}
	};
	int MaxNumSuffix = 0;
	for (int i = 0; i < InExistNames.Num(); i++)//search from same level actors, and get the right suffix
	{
		auto& Item = InExistNames[i];
		if (Item.Len() == 0)continue;
		int Num;
		if (ExtractNumeric(Item, Num))
		{
			if (Num > MaxNumSuffix)
			{
				MaxNumSuffix = Num;
			}
		}
	}
	return FString::Printf(TEXT("%s_%d"), *InPrefix, MaxNumSuffix + 1);
}

FString FLexUIEditorTools::GetNameForNewWidget(ULexWidget* InParentWidget, const FString& InBaseName)
{
	auto SiblingWidgetList = InParentWidget->GetUIChildren();

	FString MaxNumericSuffixStr = TEXT("");//numeric suffix
	auto OriginName = GetNamePrefixForCopy(InBaseName, MaxNumericSuffixStr);
	int MaxNumericSuffixStrLength = MaxNumericSuffixStr.Len();
	int SameNameActorCount = 0;//if actor name is same with source name, then collect it
	for (int i = 0; i < SiblingWidgetList.Num(); i ++)//search from same level actors, and get the right suffix
	{
		auto WidgetItem = SiblingWidgetList[i];
		auto WidgetItemName = WidgetItem->GetDisplayName();
		if (WidgetItemName == OriginName)SameNameActorCount++;
		if (OriginName.Len() == 0 || WidgetItemName.StartsWith(OriginName))
		{
			auto itemRightStr = WidgetItemName.Right(WidgetItemName.Len() - OriginName.Len());
			if (!itemRightStr.IsNumeric())//if rest is not numeric
			{
				continue;
			}
			FString ItemNumericSuffixStr = itemRightStr;
			int ItemNumeric = FCString::Atoi(*ItemNumericSuffixStr);
			int MaxNumericSuffix = FCString::Atoi(*MaxNumericSuffixStr);
			if (ItemNumeric > MaxNumericSuffix)
			{
				MaxNumericSuffix = ItemNumeric;
				MaxNumericSuffixStr = FString::Printf(TEXT("%d"), MaxNumericSuffix);
			}
		}
	}
	FString CopiedName = OriginName;
	if (!MaxNumericSuffixStr.IsEmpty() || SameNameActorCount > 0)
	{
		int MaxNumericSuffix = FCString::Atoi(*MaxNumericSuffixStr);
		MaxNumericSuffix++;
		FString NumericSuffixStr = FString::Printf(TEXT("%d"), MaxNumericSuffix);
		while (NumericSuffixStr.Len() < MaxNumericSuffixStrLength)
		{
			NumericSuffixStr = TEXT("0") + NumericSuffixStr;
		}
		CopiedName += NumericSuffixStr;
	}
	return CopiedName;
}

FString FLexUIEditorTools::GetNamePrefixForCopy(const FString& InSrcName, FString& OutNumericSuffix)
{
	int RightCount = 1;
	while (RightCount <= InSrcName.Len() && InSrcName.Right(RightCount).IsNumeric())
	{
		RightCount++;
	}
	RightCount--;
	OutNumericSuffix = InSrcName.Right(RightCount);
	return InSrcName.Left(InSrcName.Len() - RightCount);
}

TArray<AActor*> FLexUIEditorTools::GetRootActorListFromSelection(const TArray<AActor*>& selectedActors)
{
	TArray<AActor*> RootActorList;
	auto count = selectedActors.Num();
	//search upward find parent and put into list, only root actor can add to list
	for (int i = 0; i < count; i++)
	{
		auto obj = selectedActors[i];
		auto parent = obj->GetAttachParentActor();
		bool isRootActor = false;
		while (true)
		{
			if (parent == nullptr)//top level
			{
				isRootActor = true;
				break;
			}
			if (selectedActors.Contains(parent))//if parent is already in list, skip it
			{
				isRootActor = false;
				break;
			}
			else//if not in list, keep search upward
			{
				parent = parent->GetAttachParentActor();
				continue;
			}
		}
		if (isRootActor)
		{
			RootActorList.Add(obj);
		}
	}
	return RootActorList;
}

void FLexUIEditorTools::CreateLexWidget(TFunction<AActor*()> GetSelectedActorFunction, FString Name, UClass* VisualClass, TFunction<void(ULexWidget*)> Callback)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	GEditor->BeginTransaction(LOCTEXT("CreateChildWidget_Transaction", "Create Child Widget"));
	MakeCurrentLevel(SelectedActor);
	ULexUIManagerWorldSubsystem::GetSelection(SelectedActor->GetWorld())->Modify();
	auto NewActor = SelectedActor->GetWorld()->SpawnActor<ALexWidgetActor>(ALexWidgetActor::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	if (IsValid(NewActor))
	{
		NewActor->SetActorLabel(Name);
		if (SelectedActor != nullptr)
		{
			NewActor->AttachToActor(SelectedActor, FAttachmentTransformRules::KeepRelativeTransform);
			ULexUIManagerWorldSubsystem::GetSelection(SelectedActor->GetWorld())->SelectNone();
		}
		if (VisualClass)
		{
			NewActor->GetLexWidget()->CreateNewVisual(VisualClass);
		}
		if (Callback)
		{
			Callback(NewActor->GetLexWidget());
		}
		ULexUIManagerWorldSubsystem::GetSelection(SelectedActor->GetWorld())->SelectActor(NewActor);
	}
	GEditor->EndTransaction();
}

AActor* FLexUIEditorTools::GetFirstSelectedActor()
{
	auto SelectedActors = FLexUIEditorToolsHelperFunctionHolder::ConvertSelectionToActors(GEditor->GetSelectedActors());
	auto count = SelectedActors.Num();
	if (count == 0)
	{
		//UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return nullptr;
	}
	else if (count > 1)
	{
		//UE_LOG(LGUIEditor, Error, TEXT("Only support one component"));
		return nullptr;
	}
	return SelectedActors[0];
}

void FLexUIEditorTools::CreateUIControls(TFunction<AActor*()> GetSelectedActorFunction, FString InPrefabPath)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	GEditor->BeginTransaction(LOCTEXT("CreateUIControl_Transaction", "LexUI Create UI Control"));
	MakeCurrentLevel(SelectedActor);
	ULexUIManagerWorldSubsystem::GetSelection(SelectedActor->GetWorld())->Modify();
	if (auto Prefab = LoadObject<ULexUIPrefab>(NULL, *InPrefabPath))
	{
		auto Actor = Prefab->LoadPrefabInEditor(SelectedActor->GetWorld()
			, SelectedActor == nullptr ? nullptr : SelectedActor->GetRootComponent());
		ULexUIManagerWorldSubsystem::GetSelection(SelectedActor->GetWorld())->SelectNone();
		ULexUIManagerWorldSubsystem::GetSelection(SelectedActor->GetWorld())->SelectActor(Actor);
	}
	else
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load control prefab error! Path:%s. Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InPrefabPath);
	}
	GEditor->EndTransaction();
}

void FLexUIEditorTools::DuplicateActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)//@todo: fix bug: duplicate subprefab then undo, this operation will revert the source copied prefab to orignal state
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	auto RootActorList = FLexUIEditorTools::GetRootActorListFromSelection(SelectedActors);
	GEditor->BeginTransaction(LOCTEXT("DuplicateActor_Transaction", "LexUI Duplicate Actors"));
	ULexUIManagerWorldSubsystem::GetSelection(SelectedActors[0]->GetWorld())->Modify();
	ULexUIManagerWorldSubsystem::GetSelection(SelectedActors[0]->GetWorld())->SelectNone();
	for (auto Actor : RootActorList)
	{
		MakeCurrentLevel(Actor);
		Actor->GetLevel()->Modify();
		auto CopiedActorLabel = FLexUIEditorToolsHelperFunctionHolder::GetCopiedActorLabel(Actor->GetAttachParentActor(), Actor->GetActorLabel(), Actor->GetWorld());
		AActor* CopiedActor;
		USceneComponent* Parent = nullptr;
		if (Actor->GetAttachParentActor())
		{
			Parent = Actor->GetAttachParentActor()->GetRootComponent();
		}
		TMap<TObjectPtr<AActor>, FLexUISubPrefabData> DuplicatedSubPrefabMap;
		TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
		TMap<UObject*, FGuid> InMapObjectToGuid;
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor))
		{
			PrefabHelperObject->CleanupInvalidSubPrefab();//do cleanup before everything else
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetCanNotifyAttachment(false);
			struct LOCAL {
				static void CollectSubPrefabActors(AActor* InActor, const TMap<TObjectPtr<AActor>, FLexUISubPrefabData>& InSubPrefabMap, TArray<AActor*>& OutSubPrefabRootActors)
				{
					if (InSubPrefabMap.Contains(InActor))
					{
						OutSubPrefabRootActors.Add(InActor);
					}
					else
					{
						TArray<AActor*> ChildrenActors;
						InActor->GetAttachedActors(ChildrenActors);
						for (auto& ChildActor : ChildrenActors)
						{
							CollectSubPrefabActors(ChildActor, InSubPrefabMap, OutSubPrefabRootActors);
						}
					}
				}
			};
			TArray<AActor*> SubPrefabRootActors;
			LOCAL::CollectSubPrefabActors(Actor, PrefabHelperObject->SubPrefabMap, SubPrefabRootActors);//collect sub prefabs that is attached to this Actor
			for (auto& SubPrefabKeyValue : PrefabHelperObject->SubPrefabMap)//generate MapObjectToGuid
			{
				auto SubPrefabRootActor = SubPrefabKeyValue.Key;
				if (SubPrefabRootActors.Contains(SubPrefabRootActor))
				{
					auto& SubPrefabData = SubPrefabKeyValue.Value;
					PrefabHelperObject->RefreshOnSubPrefabDirty(SubPrefabData.PrefabAsset, SubPrefabRootActor);//need to update subprefab to latest before duplicate
					auto FindObjectGuidInParentPrefab = [&](FGuid InGuidInSubPrefab) {
						for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
						{
							if (KeyValue.Value == InGuidInSubPrefab)
							{
								return KeyValue.Key;
							}
						}
						UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
						FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
						return FGuid::NewGuid();
					};
					for (auto& MapGuidToObjectKeyValue : SubPrefabData.MapGuidToObject)
					{
						InMapObjectToGuid.Add(MapGuidToObjectKeyValue.Value, FindObjectGuidInParentPrefab(MapGuidToObjectKeyValue.Key));
					}
				}
			}
			CopiedActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::DuplicateActorForEditor(Actor, Parent, PrefabHelperObject->SubPrefabMap, InMapObjectToGuid, DuplicatedSubPrefabMap, OutMapGuidToObject);
			if (auto UIItem = Cast<ULexWidget>(CopiedActor->GetRootComponent()))
			{
				if (auto UIParent = Cast<ULexWidget>(Parent))
				{
					UIItem->SetAsLastSibling();
				}
			}
			for (auto& KeyValue : DuplicatedSubPrefabMap)
			{
				TMap<FGuid, TObjectPtr<UObject>> SubMapGuidToObject;
				for (auto& MapGuidItem : KeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab)
				{
					SubMapGuidToObject.Add(MapGuidItem.Value, OutMapGuidToObject[MapGuidItem.Key]);
				}
				PrefabHelperObject->MakePrefabAsSubPrefab(KeyValue.Value.PrefabAsset, KeyValue.Key, SubMapGuidToObject, KeyValue.Value.ObjectOverrideParameterArray);
			}
			PrefabHelperObject->SetCanNotifyAttachment(true);
		}
		else 
		{
			CopiedActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::DuplicateActorForEditor(Actor, Parent, {}, InMapObjectToGuid, DuplicatedSubPrefabMap, OutMapGuidToObject);
		}
		CopiedActor->SetActorLabel(CopiedActorLabel);
		ULexUIManagerWorldSubsystem::GetSelection(CopiedActor->GetWorld())->SelectActor(CopiedActor);
	}
	GEditor->EndTransaction();
	ULexUIManagerWorldSubsystem::RefreshAllUI();
}
void FLexUIEditorTools::CopyActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	for (auto KeyValuePair : CopiedActorPrefabMap)
	{
		KeyValuePair.Value->RemoveFromRoot();
		KeyValuePair.Value->ConditionalBeginDestroy();
	}
	auto CopyActorList = FLexUIEditorTools::GetRootActorListFromSelection(SelectedActors);
	CopiedActorPrefabMap.Reset();
	for (auto Actor : CopyActorList)
	{
		auto prefab = NewObject<ULexUIPrefab>();
		prefab->AddToRoot();
		TMap<UObject*, FGuid> MapObjectToGuid;
		TMap<TObjectPtr<AActor>, FLexUISubPrefabData> SubPrefabMap;
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor))
		{
			SubPrefabMap = PrefabHelperObject->SubPrefabMap;

			if (PrefabHelperObject->CleanupInvalidSubPrefab())//do cleanup before everything else
			{
				PrefabHelperObject->Modify();
			}
			struct LOCAL {
				static void CollectSubPrefabActors(AActor* InActor, const TMap<TObjectPtr<AActor>, FLexUISubPrefabData>& InSubPrefabMap, TArray<AActor*>& OutSubPrefabRootActors)
				{
					if (InSubPrefabMap.Contains(InActor))
					{
						OutSubPrefabRootActors.Add(InActor);
					}
					else
					{
						TArray<AActor*> ChildrenActors;
						InActor->GetAttachedActors(ChildrenActors);
						for (auto& ChildActor : ChildrenActors)
						{
							CollectSubPrefabActors(ChildActor, InSubPrefabMap, OutSubPrefabRootActors);
						}
					}
				}
			};
			TArray<AActor*> SubPrefabRootActors;
			LOCAL::CollectSubPrefabActors(Actor, PrefabHelperObject->SubPrefabMap, SubPrefabRootActors);//collect sub prefabs that is attached to this Actor
			for (auto& SubPrefabKeyValue : PrefabHelperObject->SubPrefabMap)//generate MapObjectToGuid
			{
				auto SubPrefabRootActor = SubPrefabKeyValue.Key;
				if (SubPrefabRootActors.Contains(SubPrefabRootActor))
				{
					auto& SubPrefabData = SubPrefabKeyValue.Value;
					PrefabHelperObject->RefreshOnSubPrefabDirty(SubPrefabData.PrefabAsset, SubPrefabRootActor);//need to update subprefab to latest before duplicate
					auto FindObjectGuidInParentPrefab = [&](FGuid InGuidInSubPrefab) {
						for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
						{
							if (KeyValue.Value == InGuidInSubPrefab)
							{
								return KeyValue.Key;
							}
						}
						UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
						FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
						return FGuid::NewGuid();
					};
					for (auto& MapGuidToObjectKeyValue : SubPrefabData.MapGuidToObject)
					{
						MapObjectToGuid.Add(MapGuidToObjectKeyValue.Value, FindObjectGuidInParentPrefab(MapGuidToObjectKeyValue.Key));
					}
				}
			}
		}

		TMap<TObjectPtr<AActor>, FLexUISubPrefabData> TempSubPrefabMap;
		for (auto& SubPrefabKeyValue : SubPrefabMap)
		{
			if (SubPrefabKeyValue.Key->IsAttachedTo(Actor) || SubPrefabKeyValue.Key == Actor)
			{
				TempSubPrefabMap = SubPrefabMap;
				break;
			}
		}
		prefab->SavePrefab(Actor, MapObjectToGuid, TempSubPrefabMap);
		CopiedActorPrefabMap.Add(Actor->GetActorLabel(), prefab);
	}
}
void FLexUIEditorTools::PasteActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	USceneComponent* parentComp = nullptr;
	if (SelectedActors.Num() > 0)
	{
		parentComp = SelectedActors[0]->GetRootComponent();
	}
	ULexUIPrefabHelperObject* PrefabHelperObject = nullptr;
	if (parentComp)
	{
		PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(parentComp->GetOwner());
	}
	if (PrefabHelperObject == nullptr)return;

	PrefabHelperObject->SetCanNotifyAttachment(false);
	GEditor->BeginTransaction(LOCTEXT("PasteActor_Transaction", "LexUI Paste Actors"));
	ULexUIManagerWorldSubsystem::GetSelection(SelectedActors[0]->GetWorld())->Modify();
	ULexUIManagerWorldSubsystem::GetSelection(SelectedActors[0]->GetWorld())->SelectNone();
	if (IsValid(parentComp))
	{
		MakeCurrentLevel(parentComp->GetOwner());
	}
	for (auto KeyValuePair : CopiedActorPrefabMap)
	{
		if (KeyValuePair.Value.IsValid())
		{
			TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
			TMap<TObjectPtr<AActor>, FLexUISubPrefabData> LoadedSubPrefabMap;
			auto copiedActorLabel = FLexUIEditorToolsHelperFunctionHolder::GetCopiedActorLabel(parentComp->GetOwner(), KeyValuePair.Key, parentComp->GetWorld());
			auto CopiedActor = KeyValuePair.Value->LoadPrefabInEditor(parentComp->GetWorld(), parentComp, LoadedSubPrefabMap, OutMapGuidToObject, false);
			for (auto& KeyValue : LoadedSubPrefabMap)
			{
				TMap<FGuid, TObjectPtr<UObject>> SubMapGuidToObject;
				for (auto& MapGuidItem : KeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab)
				{
					SubMapGuidToObject.Add(MapGuidItem.Value, OutMapGuidToObject[MapGuidItem.Key]);
				}
				PrefabHelperObject->MakePrefabAsSubPrefab(KeyValue.Value.PrefabAsset, KeyValue.Key, SubMapGuidToObject, KeyValue.Value.ObjectOverrideParameterArray);
			}
			CopiedActor->SetActorLabel(copiedActorLabel);
			ULexUIManagerWorldSubsystem::GetSelection(CopiedActor->GetWorld())->SelectActor(CopiedActor);
		}
		else
		{
			UE_LOG(LGUIEditor, Error, TEXT("Source copied actor is missing!"));
		}
	}
	PrefabHelperObject->SetCanNotifyAttachment(true);
	GEditor->EndTransaction();
	ULexUIManagerWorldSubsystem::RefreshAllUI();
}
void FLexUIEditorTools::DeleteActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	auto RootActorList = FLexUIEditorTools::GetRootActorListFromSelection(SelectedActors);
	GEditor->BeginTransaction(LOCTEXT("DestroyActor_Transaction", "LexUI Destroy Actor"));
	for (auto Actor : RootActorList)
	{
		auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor);
		if (PrefabHelperObject != nullptr)
		{
			PrefabHelperObject->SetCanNotifyAttachment(false);
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetAnythingDirty();
			TArray<AActor*> ChildrenActors;
			FLexUIUtils::CollectChildrenActors(Actor, ChildrenActors);
			for (auto ChildActor : ChildrenActors)
			{
				PrefabHelperObject->RemoveSubPrefabByAnyActorOfSubPrefab(ChildActor);
			}
			FLexUIUtils::DestroyActorWithHierarchy(Actor);
			PrefabHelperObject->SetCanNotifyAttachment(true);
		}
		else//common actor
		{
			FLexUIUtils::DestroyActorWithHierarchy(Actor);
		}
	}
	GEditor->EndTransaction();
	CleanupPrefabsInWorld(RootActorList[0]->GetWorld());
}
void FLexUIEditorTools::CutActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	CopyActors(GetSelectedActorArrayFunction);
	DeleteActors(GetSelectedActorArrayFunction);
}
void FLexUIEditorTools::ToggleSelectedActorsSpatiallyLoaded(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	struct LOCAL
	{
		static void SetSpatiallyLoadedValue_Recursive(AActor* Actor, bool value)
		{
			if (Actor->CanChangeIsSpatiallyLoadedFlag())
			{
				if (Actor->GetIsSpatiallyLoaded() != value)
				{
					Actor->SetIsSpatiallyLoaded(value);
					FLexUIUtils::NotifyPropertyChanged(Actor, AActor::GetIsSpatiallyLoadedPropertyName());
				}
			}
			TArray<AActor*> ChildActors;
			Actor->GetAttachedActors(ChildActors);
			for (auto ChildActor : ChildActors)
			{
				if (IsValid(ChildActor))
				{
					SetSpatiallyLoadedValue_Recursive(ChildActor, value);
				}
			}
		}
	};
	GEditor->BeginTransaction(LOCTEXT("ToggleSpatiallyLoaded_Transaction", "LexUI Toggle Actors IsSpatiallyLoaded"));
	auto ActorList = FLexUIEditorTools::GetRootActorListFromSelection(SelectedActors);
	for (auto Actor : ActorList)
	{
		Actor->Modify();
		auto bIsSpatiallyLoadedValue = !Actor->GetIsSpatiallyLoaded();
		LOCAL::SetSpatiallyLoadedValue_Recursive(Actor, bIsSpatiallyLoadedValue);
	}
	GEditor->EndTransaction();
}
ECheckBoxState FLexUIEditorTools::GetActorsSpatiallyLoadedProperty(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() == 0)
	{
		return ECheckBoxState::Undetermined;
	}
	auto ActorList = FLexUIEditorTools::GetRootActorListFromSelection(SelectedActors);
	bool bIsSpatiallyLoadedValue = ActorList[0]->GetIsSpatiallyLoaded();
	for (int i = 1; i < ActorList.Num(); i++)
	{
		if (bIsSpatiallyLoadedValue != ActorList[i]->GetIsSpatiallyLoaded())
		{
			return ECheckBoxState::Undetermined;
		}
	}
	return bIsSpatiallyLoadedValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool FLexUIEditorTools::CanDuplicateActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() <= 0)return false;
	return true;
}
bool FLexUIEditorTools::CanCopyActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() <= 0)return false;
	return true;
}
bool FLexUIEditorTools::CanPasteActor(TFunction<AActor*()> GetSelectedActorFunction)
{
	if (FLexUIEditorTools::CopiedActorPrefabMap.Num() == 0)return false;
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!FLexUIEditorTools::IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	return true;
}
bool FLexUIEditorTools::CanCutActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	return CanDeleteActor(GetSelectedActorArrayFunction);
}
bool FLexUIEditorTools::CanDeleteActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() == 0)return false;
	for (auto Actor : SelectedActors)
	{
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor))
		{
			if (!PrefabHelperObject->IsSubPrefabRootActor(Actor)//allowed to delete sub prefab's root actor
				&& PrefabHelperObject->IsActorBelongsToSubPrefab(Actor))//not allowed to delete sub prefab's actor
			{
				return false;
			}
		}
	}
	return true;
}
bool FLexUIEditorTools::CanToggleActorsSpatiallyLoaded(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction)
{
	auto SelectedActors = GetSelectedActorArrayFunction();
	if (SelectedActors.Num() <= 0)return false;
	auto ActorList = FLexUIEditorTools::GetRootActorListFromSelection(SelectedActors);
	for (auto Actor : ActorList)
	{
		if (!Actor->CanChangeIsSpatiallyLoadedFlag())
		{
			return false;
		}
	}
	return true;
}

bool FLexUIEditorTools::HaveValidCopiedActors()
{
	if (CopiedActorPrefabMap.Num() == 0)return false;
	for (auto KeyValuePair : CopiedActorPrefabMap)
	{
		if (!KeyValuePair.Value.IsValid())
		{
			return false;
		}
	}
	return true;
}

bool FLexUIEditorTools::CanCreatePrefab(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (SelectedActor->HasAnyFlags(EObjectFlags::RF_Transient))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->LoadedRootActor == SelectedActor)
		{
			return false;
		}
		if (PrefabHelperObject->IsActorBelongsToSubPrefab(SelectedActor))
		{
			return false;
		}
		else if (PrefabHelperObject->IsActorBelongsToMissingSubPrefab(SelectedActor))
		{
			return false;
		}
	}
	return true;
}
FString FLexUIEditorTools::PrevSavePrefabFolder = TEXT("");
void FLexUIEditorTools::CreatePrefabAsset(TFunction<AActor*()> GetSelectedActorFunction)//@todo: make some referenced parameter as override parameter(eg: Actor parameter reference other actor that is not belongs to prefab hierarchy)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	auto OldPrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
	if (IsValid(OldPrefabHelperObject) && OldPrefabHelperObject->LoadedRootActor == SelectedActor)//If create prefab from an existing prefab's root actor, this is not allowed
	{
		auto Message = LOCTEXT("CreatePrefabError_BelongToOtherPrefab", "This actor is a root actor of another prefab, this is not allowed! Instead you can duplicate the prefab asset.");
		FMessageDialog::Open(EAppMsgType::Ok, Message);
		return;
	}
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFileNames;
		DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(FSlateApplication::Get().GetGameViewport()),
			TEXT("Choose a path to save prefab asset, must inside Content folder"),
			PrevSavePrefabFolder.IsEmpty() ? FPaths::ProjectContentDir() : PrevSavePrefabFolder,
			SelectedActor->GetActorLabel() + TEXT("_Prefab"),
			TEXT("*.*"),
			EFileDialogFlags::None,
			OutFileNames
		);
		if (OutFileNames.Num() > 0)
		{
			FString selectedFilePath = OutFileNames[0];
			if (selectedFilePath.StartsWith(FPaths::ProjectDir()))
			{
				PrevSavePrefabFolder = FPaths::GetPath(selectedFilePath);
				if (FPaths::FileExists(selectedFilePath + TEXT(".uasset")))
				{
					auto returnValue = FMessageDialog::Open(EAppMsgType::YesNo
						, FText::Format(LOCTEXT("Error_AssetAlreadyExist", "Asset already exist at path: \"{0}\" !\nReplace it?"), FText::FromString(selectedFilePath)));
					if (returnValue != EAppReturnType::Yes)
					{
						return;
					}
				}
				selectedFilePath.RemoveFromStart(FPaths::ProjectContentDir(), ESearchCase::CaseSensitive);
				FString packageName = TEXT("/Game/") + selectedFilePath;
				UPackage* package = CreatePackage(*packageName);
				if (package == nullptr)
				{
					FMessageDialog::Open(EAppMsgType::Ok
						, LOCTEXT("Error_NotValidPathForSavePrefab", "Selected path not valid, please choose another path to save prefab."));
					return;
				}
				package->FullyLoad();
				FString fileName = FPaths::GetBaseFilename(selectedFilePath);
				auto OutPrefab = NewObject<ULexUIPrefab>(package, ULexUIPrefab::StaticClass(), *fileName, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
				FAssetRegistryModule::AssetCreated(OutPrefab);

				auto PrefabHelperObjectWhichManageThisActor = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
				check(PrefabHelperObjectWhichManageThisActor != nullptr)
				{
					struct LOCAL
					{
						static auto Make_MapGuidFromParentToSub(const TMap<UObject*, FGuid>& InNewParentMapObjectToGuid, ULexUIPrefabHelperObject* InPrefabHelperObject, const FLexUISubPrefabData& InOriginSubPrefabData)
						{
							TMap<FGuid, FGuid> Result;
							for (auto& KeyValue : InOriginSubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
							{
								auto Object = InPrefabHelperObject->MapGuidToObject[KeyValue.Key];
								if (IsValid(Object))
								{
									auto Guid = InNewParentMapObjectToGuid[Object];
									if (!Result.Contains(Guid))
									{
										Result.Add(Guid, KeyValue.Value);
									}
								}
							}
							return Result;
						}
						static void CollectSubPrefab(AActor* InActor, TMap<TObjectPtr<AActor>, FLexUISubPrefabData>& InOutSubPrefabMap, ULexUIPrefabHelperObject* InPrefabHelperObject, const TMap<UObject*, FGuid>& InMapObjectToGuid)
						{
							if (InPrefabHelperObject->IsActorBelongsToSubPrefab(InActor))
							{
								auto OriginSubPrefabData = InPrefabHelperObject->GetSubPrefabData(InActor);
								FLexUISubPrefabData SubPrefabData;
								SubPrefabData.PrefabAsset = OriginSubPrefabData.PrefabAsset;
								SubPrefabData.ObjectOverrideParameterArray = OriginSubPrefabData.ObjectOverrideParameterArray;
								SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab = Make_MapGuidFromParentToSub(InMapObjectToGuid, InPrefabHelperObject, OriginSubPrefabData);
								InOutSubPrefabMap.Add(InActor, SubPrefabData);
								return;
							}
							TArray<AActor*> ChildrenActors;
							InActor->GetAttachedActors(ChildrenActors);
							for (auto ChildActor : ChildrenActors)
							{
								CollectSubPrefab(ChildActor, InOutSubPrefabMap, InPrefabHelperObject, InMapObjectToGuid);//collect all actor, include subprefab's actor
							}
						}
					};
					TMap<TObjectPtr<AActor>, FLexUISubPrefabData> SubPrefabMap;
					TMap<UObject*, FGuid> MapObjectToGuid;
					OutPrefab->SavePrefab(SelectedActor, MapObjectToGuid, SubPrefabMap);//save prefab first step, just collect guid and sub prefab
					LOCAL::CollectSubPrefab(SelectedActor, SubPrefabMap, PrefabHelperObjectWhichManageThisActor, MapObjectToGuid);
					for (auto& KeyValue : SubPrefabMap)
					{
						PrefabHelperObjectWhichManageThisActor->RemoveSubPrefabByAnyActorOfSubPrefab(KeyValue.Key);//remove prefab from origin PrefabHelperObject
					}
					OutPrefab->SavePrefab(SelectedActor, MapObjectToGuid, SubPrefabMap);//save prefab second step, store sub prefab data
					OutPrefab->EnsureInstanceObjects();

					//make it as sub-prefab
					TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
					for (auto KeyValue : MapObjectToGuid)
					{
						MapGuidToObject.Add(KeyValue.Value, KeyValue.Key);
					}
					PrefabHelperObjectWhichManageThisActor->MakePrefabAsSubPrefab(OutPrefab, SelectedActor, MapGuidToObject, {});
				}
				CleanupPrefabsInWorld(SelectedActor->GetWorld());
			}
			else
			{
				FMessageDialog::Open(EAppMsgType::Ok
					, LOCTEXT("Error_PrefabSaveLocation", "Prefab should only save inside Content folder!"));
			}
		}
	}
}

void FLexUIEditorTools::RefreshLevelLoadedPrefab()
{
	for (TObjectIterator<ULexUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CheckPrefabVersion();
	}
	for (TObjectIterator<ALexWidgetRootActor> Itr; Itr; ++Itr)
	{
		if (Itr->GetWorld())
		{
			Itr->CheckPrefabVersion();
		}
	}
}

void FLexUIEditorTools::RefreshOpenedPrefabEditor(ULexUIPrefab* InPrefab)
{
	if (auto PrefabEditor = FLexUIPrefabEditor::GetEditorForPrefabIfValid(InPrefab))//refresh opened prefab
	{
		if (PrefabEditor->GetAnythingDirty())
		{
			auto Msg = LOCTEXT("PrefabEditorChangedDataWillLose", "Prefab editor will automaticallly refresh changed prefab, but detect some data changed in prefab editor, refresh the prefab editor will lose these data, do you want to continue?");
			auto Result = FMessageDialog::Open(EAppMsgType::YesNo, Msg);
			if (Result == EAppReturnType::Yes)
			{
				//reopen this prefab editor
				PrefabEditor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				AssetEditorSubsystem->OpenEditorForAsset(InPrefab);
			}
		}
		else
		{
			//reopen this prefab editor
			PrefabEditor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(InPrefab);
		}
	}
}

void FLexUIEditorTools::RefreshOnSubPrefabChange(ULexUIPrefab* InSubPrefab)
{
	auto AllPrefabs = GetAllPrefabArray();

	struct Local
	{
	public:
		static void RefreshAllPrefabsOnSubPrefabChange(const TArray<ULexUIPrefab*>& InPrefabs, ULexUIPrefab* InSubPrefab)
		{
			for (auto& Prefab : InPrefabs)
			{
				if (Prefab->IsPrefabBelongsToThisSubPrefab(InSubPrefab, false))
				{
					//check if is opened by prefab editor
					if (auto PrefabEditor = FLexUIPrefabEditor::GetEditorForPrefabIfValid(Prefab))//refresh opened prefab
					{
						PrefabEditor->RefreshOnSubPrefabDirty(InSubPrefab);
					}
					RefreshAllPrefabsOnSubPrefabChange(InPrefabs, Prefab);
				}
			}
		}
	};

	Local::RefreshAllPrefabsOnSubPrefabChange(AllPrefabs, InSubPrefab);
}

TArray<ULexUIPrefab*> FLexUIEditorTools::GetAllPrefabArray()
{
#if 0//Why disable? Because we don't need to refresh not-loaded prefab, because prefab will reload all sub prefab when load
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Need to do this if running in the editor with -game to make sure that the assets in the following path are available
	TArray<FString> PathsToScan;
	PathsToScan.Add(TEXT("/Game/"));
	AssetRegistry.ScanPathsSynchronous(PathsToScan);

	// Get asset in path
	TArray<FAssetData> ScriptAssetList;
	AssetRegistry.GetAssetsByPath(FName("/Game/"), ScriptAssetList, /*bRecursive=*/true);

	TArray<ULexUIPrefab*> AllPrefabs;
	auto PrefabClassName = ULexUIPrefab::StaticClass()->GetClassPathName();
	// Ensure all assets are loaded
	for (const FAssetData& Asset : ScriptAssetList)
	{
		// Gets the loaded asset, loads it if necessary
		if (Asset.AssetClassPath == PrefabClassName)
		{
			auto AssetObject = Asset.GetAsset();
			if (auto Prefab = Cast<ULexUIPrefab>(AssetObject))
			{
				Prefab->MakeAgentObjectsInPreviewWorld();
				AllPrefabs.Add(Prefab);
			}
		}
	}
#else
	TArray<ULexUIPrefab*> AllPrefabs;
#endif
	//collect prefabs that are not saved to disc yet
	for (TObjectIterator<ULexUIPrefab> Itr; Itr; ++Itr)
	{
		if (!AllPrefabs.Contains(*Itr))
		{
			AllPrefabs.Add(*Itr);
		}
	}
	return AllPrefabs;
}

bool FLexUIEditorTools::CanUnpackActorForPrefab(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedActor))
		{
			return true;
		}
		else if (PrefabHelperObject->MissingPrefab.Contains(SelectedActor))
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}
void FLexUIEditorTools::UnpackPrefab(TFunction<AActor*()> GetSelectedActorFunction)
{	
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	GEditor->BeginTransaction(FText::FromString(TEXT("LexUI UnpackPrefab")));
	auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
	if (PrefabHelperObject != nullptr)
	{
		SelectedActor->GetWorld()->Modify();
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedActor) || PrefabHelperObject->MissingPrefab.Contains(SelectedActor));//should already filtered by menu
		PrefabHelperObject->Modify();
		PrefabHelperObject->RemoveSubPrefabByRootActor(SelectedActor);//the SelectedActor must be root actor, should already filtered by menu
	}
	GEditor->EndTransaction();
	CleanupPrefabsInWorld(SelectedActor->GetWorld());
}

void FLexUIEditorTools::SelectPrefabAsset(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
	if (PrefabHelperObject != nullptr)
	{
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedActor));//should have being checked in Browse button
		auto PrefabAsset = PrefabHelperObject->GetSubPrefabAsset(SelectedActor);
		if (IsValid(PrefabAsset))
		{
			TArray<UObject*> ObjectsToSync;
			ObjectsToSync.Add(PrefabAsset);
			GEditor->SyncBrowserToObjects(ObjectsToSync);
		}
	}
}
bool FLexUIEditorTools::CanBrowsePrefabAsset(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedActor))
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}

void FLexUIEditorTools::OpenPrefabAsset(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
	if (PrefabHelperObject != nullptr)
	{
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedActor));//should have being check in menu
		auto PrefabAsset = PrefabHelperObject->GetSubPrefabAsset(SelectedActor);
		if (IsValid(PrefabAsset))
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(PrefabAsset);
		}
	}
}

bool FLexUIEditorTools::CanCheckPrefabOverrideParameter(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
		{
			if (KeyValue.Key == SelectedActor || SelectedActor->IsAttachedTo(KeyValue.Key))
			{
				return true;
			}
		}
		return false;
	}
	else
	{
		return false;
	}
}

bool FLexUIEditorTools::CanCreateActor(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	return true;
}

void FLexUIEditorTools::CleanupPrefabsInWorld(UWorld* World)
{
	for (TObjectIterator<ULexUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CleanupInvalidSubPrefab();
	}
}

void FLexUIEditorTools::MakeCurrentLevel(AActor* InActor)
{
	if (IsValid(InActor) && InActor->GetWorld() && InActor->GetLevel())
	{
		if (InActor->GetWorld()->GetCurrentLevel() != InActor->GetLevel())
		{
			if (!InActor->GetWorld()->GetCurrentLevel()->bLocked)
			{
				if (!InActor->GetLevel()->IsCurrentLevel())
				{
					InActor->GetWorld()->SetCurrentLevel(InActor->GetLevel());
				}
			}
			else
			{
				FLexUIUtils::EditorNotification(FText::FromString(FString::Printf(TEXT("The level of selected actor:%s is locked!"), *(InActor->GetActorLabel()))), false);
			}
		}
	}
}

bool FLexUIEditorTools::IsActorCompatibleWithLexUIToolsMenu(AActor* InActor)
{
	if (InActor->IsA<ALexWidgetActor>()
		&& !FLexUIPrefabEditor::ActorIsRootAgent(InActor))
	{
		return true;
	}
	return false;
}


#undef LOCTEXT_NAMESPACE