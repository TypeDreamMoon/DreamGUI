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
#include "PrefabSystem/LGUIPrefabHelperObject.h"
#include LGUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "LGUIEditorModule.h"
#include "PrefabEditor/LGUIPrefabEditor.h"
#include "LGUIHeaders.h"

#include "Settings/LevelEditorMiscSettings.h"
#include "Layers/LayersSubsystem.h"
#include "ActorEditorUtils.h"
#include "Core/Components/LexLayout.h"
#include "Core/Actor/LexWidgetRootActor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Logging/MessageLog.h"

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

TMap<FString, TWeakObjectPtr<class ULGUIPrefab>> FLexUIEditorTools::CopiedActorPrefabMap;
TWeakObjectPtr<class UActorComponent> FLexUIEditorTools::CopiedComponent;

FString FLexUIEditorTools::LGUIPresetPrefabPath = TEXT("/LGUI/Prefabs/");

FString FLexUIEditorTools::GetUniqueNumericName(const FString& InPrefix, const TArray<FString>& InExistNames)
{
	auto ExtractNumetric = [](const FString& InString, int32& Num) {
		int NumetricStringIndex = -1;
		FString SubNumetricString;
		int NumetricStringCharCount = 0;
		for (int i = InString.Len() - 1; i >= 0; i--)
		{
			auto SubChar = InString[i];
			if (SubChar >= '0' && SubChar <= '9')
			{
				NumetricStringIndex = i;

				NumetricStringCharCount++;
				if (NumetricStringCharCount >= 4)
				{
					break;
				}
			}
			else
			{
				break;
			}
		}
		if (NumetricStringIndex != -1)
		{
			auto NumetricSubString = InString.Right(InString.Len() - NumetricStringIndex);
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
		if (ExtractNumetric(Item, Num))
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
UWorld* FLexUIEditorTools::GetWorldFromSelection()
{
	if (auto selectedActor = GetFirstSelectedActor())
	{
		return selectedActor->GetWorld();
	}
	return GWorld;
}

void FLexUIEditorTools::CreateLexWidget(TFunction<AActor*()> GetSelectedActorFunction, FString Name, UClass* VisualClass, TFunction<void(ULexWidget*)> Callback)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	GEditor->BeginTransaction(LOCTEXT("CreateChildWidget_Transaction", "Create Child Widget"));
	MakeCurrentLevel(SelectedActor);
	auto NewActor = SelectedActor->GetWorld()->SpawnActor<ALexWidgetActor>(ALexWidgetActor::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	if (IsValid(NewActor))
	{
		NewActor->SetActorLabel(Name);
		if (SelectedActor != nullptr)
		{
			NewActor->AttachToActor(SelectedActor, FAttachmentTransformRules::KeepRelativeTransform);
			GEditor->SelectActor(SelectedActor, false, true);
		}
		if (VisualClass)
		{
			NewActor->GetLexWidget()->CreateNewVisual(VisualClass);
		}
		if (Callback)
		{
			Callback(NewActor->GetLexWidget());
		}
		GEditor->SelectActor(NewActor, true, true);
		ULexUIManagerWorldSubsystem::GetInstance(SelectedActor->GetWorld())->EventOnOutlineChanged.Broadcast();
	}
	GEditor->EndTransaction();
}

void FLexUIEditorTools::CreateEmptyActor(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	GEditor->BeginTransaction(LOCTEXT("CreateEmptyActor_Transaction", "LexUI create empty actor"));
	MakeCurrentLevel(SelectedActor);
	AActor* newActor = SelectedActor->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, FActorSpawnParameters());
	if (IsValid(newActor))
	{
		//create SceneComponent
		{
			USceneComponent* RootComponent = NewObject<USceneComponent>(newActor, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
			RootComponent->Mobility = EComponentMobility::Movable;
			RootComponent->bVisualizeComponent = false;

			newActor->SetRootComponent(RootComponent);
			RootComponent->RegisterComponent();
			newActor->AddInstanceComponent(RootComponent);
		}
		if (SelectedActor != nullptr)
		{
			newActor->AttachToActor(SelectedActor, FAttachmentTransformRules::KeepRelativeTransform);
			GEditor->SelectActor(SelectedActor, false, true);
		}
		GEditor->SelectActor(newActor, true, true);
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

TArray<AActor*> FLexUIEditorTools::GetSelectedActors()
{
	return FLexUIEditorToolsHelperFunctionHolder::ConvertSelectionToActors(GEditor->GetSelectedActors());
}

void FLexUIEditorTools::CreateUIControls(TFunction<AActor*()> GetSelectedActorFunction, FString InPrefabPath)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	GEditor->BeginTransaction(LOCTEXT("CreateUIControl_Transaction", "LexUI Create UI Control"));
	MakeCurrentLevel(SelectedActor);
	if (auto Prefab = LoadObject<ULGUIPrefab>(NULL, *InPrefabPath))
	{
		auto actor = Prefab->LoadPrefabInEditor(SelectedActor->GetWorld()
			, SelectedActor == nullptr ? nullptr : SelectedActor->GetRootComponent());
		GEditor->SelectActor(SelectedActor, false, true);
		GEditor->SelectActor(actor, true, true);
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
	for (auto Actor : RootActorList)
	{
		MakeCurrentLevel(Actor);
		Actor->GetLevel()->Modify();
		auto copiedActorLabel = FLexUIEditorToolsHelperFunctionHolder::GetCopiedActorLabel(Actor->GetAttachParentActor(), Actor->GetActorLabel(), Actor->GetWorld());
		AActor* copiedActor;
		USceneComponent* Parent = nullptr;
		if (Actor->GetAttachParentActor())
		{
			Parent = Actor->GetAttachParentActor()->GetRootComponent();
		}
		TMap<TObjectPtr<AActor>, FLGUISubPrefabData> DuplicatedSubPrefabMap;
		TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
		TMap<UObject*, FGuid> InMapObjectToGuid;
		if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor))
		{
			PrefabHelperObject->CleanupInvalidSubPrefab();//do cleanup before everything else
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetCanNotifyAttachment(false);
			struct LOCAL {
				static void CollectSubPrefabActors(AActor* InActor, const TMap<TObjectPtr<AActor>, FLGUISubPrefabData>& InSubPrefabMap, TArray<AActor*>& OutSubPrefabRootActors)
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
			copiedActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::DuplicateActorForEditor(Actor, Parent, PrefabHelperObject->SubPrefabMap, InMapObjectToGuid, DuplicatedSubPrefabMap, OutMapGuidToObject);
			if (auto UIItem = Cast<ULexWidget>(copiedActor->GetRootComponent()))
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
			copiedActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::DuplicateActorForEditor(Actor, Parent, {}, InMapObjectToGuid, DuplicatedSubPrefabMap, OutMapGuidToObject);
		}
		copiedActor->SetActorLabel(copiedActorLabel);
		GEditor->SelectActor(Actor, false, true);
		GEditor->SelectActor(copiedActor, true, true);
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
		auto prefab = NewObject<ULGUIPrefab>();
		prefab->AddToRoot();
		TMap<UObject*, FGuid> MapObjectToGuid;
		TMap<TObjectPtr<AActor>, FLGUISubPrefabData> SubPrefabMap;
		if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor))
		{
			SubPrefabMap = PrefabHelperObject->SubPrefabMap;

			if (PrefabHelperObject->CleanupInvalidSubPrefab())//do cleanup before everything else
			{
				PrefabHelperObject->Modify();
			}
			struct LOCAL {
				static void CollectSubPrefabActors(AActor* InActor, const TMap<TObjectPtr<AActor>, FLGUISubPrefabData>& InSubPrefabMap, TArray<AActor*>& OutSubPrefabRootActors)
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

		TMap<TObjectPtr<AActor>, FLGUISubPrefabData> TempSubPrefabMap;
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
	ULGUIPrefabHelperObject* PrefabHelperObject = nullptr;
	if (parentComp)
	{
		PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(parentComp->GetOwner());
	}
	if (!PrefabHelperObject)
	{
		UWorld* World = nullptr;
		if (parentComp != nullptr)
		{
			World = parentComp->GetWorld();
		}
		else
		{
			World = GWorld;
		}
		if (World)
		{
			if (auto Level = World->GetCurrentLevel())
			{
				if (auto ManagerActor = ALGUIPrefabLevelManagerActor::GetInstance(Level))
				{
					PrefabHelperObject = ManagerActor->PrefabHelperObject;
				}
			}
		}
	}
	if (PrefabHelperObject == nullptr)return;

	PrefabHelperObject->SetCanNotifyAttachment(false);
	GEditor->BeginTransaction(LOCTEXT("PasteActor_Transaction", "LexUI Paste Actors"));
	for (auto item : SelectedActors)
	{
		GEditor->SelectActor(item, false, true);
	}
	if (IsValid(parentComp))
	{
		MakeCurrentLevel(parentComp->GetOwner());
	}
	for (auto KeyValuePair : CopiedActorPrefabMap)
	{
		if (KeyValuePair.Value.IsValid())
		{
			TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
			TMap<TObjectPtr<AActor>, FLGUISubPrefabData> LoadedSubPrefabMap;
			auto copiedActorLabel = FLexUIEditorToolsHelperFunctionHolder::GetCopiedActorLabel(parentComp->GetOwner(), KeyValuePair.Key, parentComp->GetWorld());
			auto copiedActor = KeyValuePair.Value->LoadPrefabInEditor(parentComp->GetWorld(), parentComp, LoadedSubPrefabMap, OutMapGuidToObject, false);
			for (auto& KeyValue : LoadedSubPrefabMap)
			{
				TMap<FGuid, TObjectPtr<UObject>> SubMapGuidToObject;
				for (auto& MapGuidItem : KeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab)
				{
					SubMapGuidToObject.Add(MapGuidItem.Value, OutMapGuidToObject[MapGuidItem.Key]);
				}
				PrefabHelperObject->MakePrefabAsSubPrefab(KeyValue.Value.PrefabAsset, KeyValue.Key, SubMapGuidToObject, KeyValue.Value.ObjectOverrideParameterArray);
			}
			copiedActor->SetActorLabel(copiedActorLabel);
			GEditor->SelectActor(copiedActor, true, true, true);
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
		auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor);
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
		if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(Actor))
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

void FLexUIEditorTools::CopyComponentValues_Impl()
{
	auto selectedComponents = FLexUIEditorToolsHelperFunctionHolder::ConvertSelectionToComponents(GEditor->GetSelectedComponents());
	auto count = selectedComponents.Num();
	if (count == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	else if (count > 1)
	{
		UE_LOG(LGUIEditor, Error, TEXT("Only support one component"));
		return;
	}
	CopiedComponent = selectedComponents[0];
}
void FLexUIEditorTools::PasteComponentValues_Impl()
{
	auto selectedComponents = FLexUIEditorToolsHelperFunctionHolder::ConvertSelectionToComponents(GEditor->GetSelectedComponents());
	auto count = selectedComponents.Num();
	if (count == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	if (CopiedComponent.IsValid())
	{
		GEditor->BeginTransaction(LOCTEXT("PasteComponentValues_Transaction", "LGUI Paste Component Proeprties"));
		UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
		Options.bNotifyObjectReplacement = true;
		for (UActorComponent* SelectedComp : selectedComponents)
		{
			if (SelectedComp->IsRegistered() && SelectedComp->AllowReregistration())
			{
				SelectedComp->UnregisterComponent();
			}
			UEditorEngine::CopyPropertiesForUnrelatedObjects(CopiedComponent.Get(), SelectedComp, Options);
			if (!SelectedComp->IsRegistered())
			{
				SelectedComp->RegisterComponent();
			}
		}
		GEditor->EndTransaction();
		ULexUIManagerWorldSubsystem::RefreshAllUI();
	}
	else
	{
		UE_LOG(LGUIEditor, Error, TEXT("Selected component is missing!"));
	}
}
void FLexUIEditorTools::OpenAtlasViewer_Impl()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FLGUIEditorModule::LGUIDynamicSpriteAtlasViewerName);
}

void FLexUIEditorTools::CreateScreenSpaceUI_BasicSetup()
{
	FString prefabPath(TEXT("/LGUI/Prefabs/ScreenSpaceUI"));
	auto prefab = LoadObject<ULGUIPrefab>(NULL, *prefabPath);
	if (prefab)
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("LexUI Create Screen Space UI")));
		GetWorldFromSelection()->GetCurrentLevel()->MarkPackageDirty();
		auto actor = prefab->LoadPrefabInEditor(GetWorldFromSelection(), nullptr, true);
		actor->GetRootComponent()->SetRelativeScale3D(FVector::OneVector);
		actor->GetRootComponent()->SetRelativeLocation(FVector(0, 0, 250));
		if (auto selectedActor = GetFirstSelectedActor())
		{
			GEditor->SelectActor(selectedActor, false, true);
		}
		GEditor->SelectActor(actor, true, true);
		CreatePresetEventSystem_BasicSetup(false);
		GEditor->EndTransaction();
		ULexUIManagerWorldSubsystem::RefreshAllUI();
	}
	else
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load control prefab error! Path:%s. Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *prefabPath);
	}
}
void FLexUIEditorTools::CreateWorldSpaceUIBuiltinRenderer_BasicSetup()
{
	FString prefabPath(TEXT("/LGUI/Prefabs/WorldSpaceUI_UERenderer"));
	auto prefab = LoadObject<ULGUIPrefab>(NULL, *prefabPath);
	if (prefab)
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("LexUI Create World Space UI - UE Renderer")));
		GetWorldFromSelection()->GetCurrentLevel()->MarkPackageDirty();
		auto actor = prefab->LoadPrefabInEditor(GetWorldFromSelection(), nullptr, true);
		actor->SetActorLabel(TEXT("WorldSpaceUI-UERenderer"));
		actor->GetRootComponent()->SetRelativeLocation(FVector(0, 0, 250));
		actor->GetRootComponent()->SetWorldScale3D(FVector::OneVector);
		if (auto selectedActor = GetFirstSelectedActor())
		{
			GEditor->SelectActor(selectedActor, false, true);
		}
		GEditor->SelectActor(actor, true, true);
		CreatePresetEventSystem_BasicSetup(true);
		auto RaycasterSource = CreatePresetWorldSpaceRaycasterSource();
		auto Raycaster = actor->GetComponentByClass<ULexWorldSpaceRaycasterBase>();
		if (RaycasterSource && Raycaster)
		{
			Raycaster->SetRaycasterSourceObject(RaycasterSource);
		}
		GEditor->EndTransaction();
		ULexUIManagerWorldSubsystem::RefreshAllUI();
	}
	else
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load control prefab error! Path:%s. Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *prefabPath);
	}
}
void FLexUIEditorTools::CreateWorldSpaceUILexUIRenderer_BasicSetup()
{
	FString prefabPath(TEXT("/LGUI/Prefabs/WorldSpaceUI_LexUIRenderer"));
	auto prefab = LoadObject<ULGUIPrefab>(NULL, *prefabPath);
	if (prefab)
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("LexUI Create World Space UI - LexUI Renderer")));
		GetWorldFromSelection()->GetCurrentLevel()->MarkPackageDirty();
		auto actor = prefab->LoadPrefabInEditor(GetWorldFromSelection(), nullptr, true);
		actor->SetActorLabel(TEXT("WorldSpaceUI-LexUIRenderer"));
		actor->GetRootComponent()->SetRelativeLocation(FVector(0, 0, 250));
		actor->GetRootComponent()->SetWorldScale3D(FVector::OneVector);
		if (auto selectedActor = GetFirstSelectedActor())
		{
			GEditor->SelectActor(selectedActor, false, true);
		}
		GEditor->SelectActor(actor, true, true);
		CreatePresetEventSystem_BasicSetup(true);
		auto RaycasterSource = CreatePresetWorldSpaceRaycasterSource();
		auto Raycaster = actor->GetComponentByClass<ULexWorldSpaceRaycasterBase>();
		if (RaycasterSource && Raycaster)
		{
			Raycaster->SetRaycasterSourceObject(RaycasterSource);
		}
		GEditor->EndTransaction();
		ULexUIManagerWorldSubsystem::RefreshAllUI();
	}
	else
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load control prefab error! Path:%s. Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *prefabPath);
	}
}
void FLexUIEditorTools::CreatePresetEventSystem_BasicSetup(bool WorldSpace)
{
	bool bEventSystemExits = false;
	bool bWorldSpaceRaycasterExists = false;
	for (TActorIterator<AActor> ActorItr(GetWorldFromSelection()); ActorItr; ++ActorItr)
	{
		auto Actor = *ActorItr;
		if (Actor->FindComponentByClass<ULexEventSystem>())
		{
			bEventSystemExits = true;
		}
		if (Actor->FindComponentByClass<ULexWorldSpaceRaycasterBase>())
		{
			bWorldSpaceRaycasterExists = true;
		}
	}
	auto CreateActor = [](const TCHAR* ClassName)
	{
		if (auto ActorClass = LoadObject<UClass>(NULL, *FString::Printf(TEXT("/LGUI/Blueprints/%s.%s_C"), ClassName, ClassName)))
		{
			auto Actor = GetWorldFromSelection()->SpawnActor<AActor>(ActorClass);
			Actor->SetActorLabel(ClassName);
		}
		else
		{
			UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load %s error! Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ClassName);
		}
	};
	if (!bEventSystemExits)
	{
		CreateActor(TEXT("BP_PresetLexEventSystemActor"));
	}
	if (WorldSpace && !bWorldSpaceRaycasterExists)
	{
		CreateActor(TEXT("BP_PresetLexWorldSpaceRaycasterSource_Mouse_Actor"));
	}
}

ULexWorldSpaceRaycasterSource* FLexUIEditorTools::CreatePresetWorldSpaceRaycasterSource()
{
	for (TActorIterator<AActor> ActorItr(GetWorldFromSelection()); ActorItr; ++ActorItr)
	{
		auto Actor = *ActorItr;
		if (auto RaycasterSource = Actor->FindComponentByClass<ULexWorldSpaceRaycasterSource>())
		{
			return RaycasterSource;
		}
	}
	auto CreateActor = [](const TCHAR* ClassName)
	{
		if (auto ActorClass = LoadObject<UClass>(NULL, *FString::Printf(TEXT("/LGUI/Blueprints/%s.%s_C"), ClassName, ClassName)))
		{
			auto Actor = GetWorldFromSelection()->SpawnActor<AActor>(ActorClass);
			Actor->SetActorLabel(ClassName);
			return Actor;
		}
		else
		{
			UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load %s error! Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ClassName);
		}
		return (AActor*)nullptr;
	};
	if (auto RaycasterSourceActor = CreateActor(TEXT("BP_PresetLexWorldSpaceRaycasterSource_Mouse_Actor")))
	{
		return RaycasterSourceActor->FindComponentByClass<ULexWorldSpaceRaycasterSource>();
	}
	return nullptr;
}

void FLexUIEditorTools::AttachComponentToSelectedActor(TSubclassOf<UActorComponent> InComponentClass)
{
	GEditor->BeginTransaction(FText::FromString(TEXT("LGUI Attach Component to Actor")));

	auto selectedActors = FLexUIEditorToolsHelperFunctionHolder::ConvertSelectionToActors(GEditor->GetSelectedActors());
	auto count = selectedActors.Num();
	if (count == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	UActorComponent* lastCreatedComponent = nullptr;
	for (auto actor : selectedActors)
	{
		if (IsValid(actor))
		{
			auto comp = NewObject<UActorComponent>(actor, InComponentClass, *FComponentEditorUtils::GenerateValidVariableName(InComponentClass, actor), RF_Transactional);
			actor->AddInstanceComponent(comp);
			comp->RegisterComponent();
			lastCreatedComponent = comp;
		}
	}

	GEditor->EndTransaction();

	if (selectedActors.Num() == 1)
	{
		GEditor->SelectNone(true, true);
		GEditor->SelectActor(lastCreatedComponent->GetOwner(), true, true, false, true);
		GEditor->SelectComponent(lastCreatedComponent, true, true, false);
	}
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
bool FLexUIEditorTools::HaveValidCopiedComponent()
{
	return CopiedComponent.IsValid();
}


bool FLexUIEditorTools::CanCreatePrefab(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (SelectedActor->HasAnyFlags(EObjectFlags::RF_Transient))return false;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
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
	if (Cast<ALGUIPrefabLoadHelperActor>(SelectedActor) != nullptr || Cast<ALGUIPrefabLevelManagerActor>(SelectedActor) != nullptr)
	{
		auto Message = LOCTEXT("CreatePrefabError_PrefabActor", "Cannot create prefab on a LGUIPrefabLoadHelperActor or LGUIPrefabLevelManagerActor!");
		FMessageDialog::Open(EAppMsgType::Ok, Message);
		return;
	}
	auto OldPrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
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
				auto OutPrefab = NewObject<ULGUIPrefab>(package, ULGUIPrefab::StaticClass(), *fileName, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
				FAssetRegistryModule::AssetCreated(OutPrefab);

				auto PrefabHelperObjectWhichManageThisActor = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
				if (PrefabHelperObjectWhichManageThisActor == nullptr)//not exist, means in level editor and not create PrefabManagerActor yet, so create it
				{
					auto ManagerActor = ALGUIPrefabLevelManagerActor::GetInstance(SelectedActor->GetLevel());
					if (ManagerActor != nullptr)
					{
						PrefabHelperObjectWhichManageThisActor = ManagerActor->PrefabHelperObject;
					}
				}
				check(PrefabHelperObjectWhichManageThisActor != nullptr)
				{
					struct LOCAL
					{
						static auto Make_MapGuidFromParentToSub(const TMap<UObject*, FGuid>& InNewParentMapObjectToGuid, ULGUIPrefabHelperObject* InPrefabHelperObject, const FLGUISubPrefabData& InOriginSubPrefabData)
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
						static void CollectSubPrefab(AActor* InActor, TMap<TObjectPtr<AActor>, FLGUISubPrefabData>& InOutSubPrefabMap, ULGUIPrefabHelperObject* InPrefabHelperObject, const TMap<UObject*, FGuid>& InMapObjectToGuid)
						{
							if (InPrefabHelperObject->IsActorBelongsToSubPrefab(InActor))
							{
								auto OriginSubPrefabData = InPrefabHelperObject->GetSubPrefabData(InActor);
								FLGUISubPrefabData SubPrefabData;
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
					TMap<TObjectPtr<AActor>, FLGUISubPrefabData> SubPrefabMap;
					TMap<UObject*, FGuid> MapObjectToGuid;
					OutPrefab->SavePrefab(SelectedActor, MapObjectToGuid, SubPrefabMap);//save prefab first step, just collect guid and sub prefab
					LOCAL::CollectSubPrefab(SelectedActor, SubPrefabMap, PrefabHelperObjectWhichManageThisActor, MapObjectToGuid);
					for (auto& KeyValue : SubPrefabMap)
					{
						PrefabHelperObjectWhichManageThisActor->RemoveSubPrefabByAnyActorOfSubPrefab(KeyValue.Key);//remove prefab from origin PrefabHelperObject
					}
					OutPrefab->SavePrefab(SelectedActor, MapObjectToGuid, SubPrefabMap);//save prefab second step, store sub prefab data
					OutPrefab->RefreshAgentObjectsInPreviewWorld();

					//make it as subprefab
					TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
					for (auto KeyValue : MapObjectToGuid)
					{
						MapGuidToObject.Add(KeyValue.Value, KeyValue.Key);
					}
					PrefabHelperObjectWhichManageThisActor->MakePrefabAsSubPrefab(OutPrefab, SelectedActor, MapGuidToObject, {});
					if (auto PrefabManagerActor = ALGUIPrefabLevelManagerActor::GetInstanceByPrefabHelperObject(PrefabHelperObjectWhichManageThisActor))
					{
						PrefabManagerActor->MarkPackageDirty();
					}

					if (OldPrefabHelperObject != nullptr && OldPrefabHelperObject->PrefabAsset != nullptr)
					{
						if (auto PrefabEditor = FLGUIPrefabEditor::GetEditorForPrefabIfValid(OldPrefabHelperObject->PrefabAsset))//if is create prefab inside a prefab editor, then apply the prefab editor
						{
							PrefabEditor->ApplyPrefab();
						}
					}
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

void FLexUIEditorTools::RefreshLevelLoadedPrefab(ULGUIPrefab* InPrefab)
{
	for (TObjectIterator<ULGUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		if (Itr->GetIsManagerObject())
		{
			if (!Itr->IsInsidePrefabEditor())
			{
				Itr->CheckPrefabVersion();
			}
		}
	}
	for (TObjectIterator<ALexWidgetRootActor> Itr; Itr; ++Itr)
	{
		if (Itr->GetWorld())
		{
			Itr->CheckPrefabVersion();
		}
	}
}

void FLexUIEditorTools::RefreshOpenedPrefabEditor(ULGUIPrefab* InPrefab)
{
	if (auto PrefabEditor = FLGUIPrefabEditor::GetEditorForPrefabIfValid(InPrefab))//refresh opened prefab
	{
		if (PrefabEditor->GetAnythingDirty())
		{
			auto Msg = LOCTEXT("PrefabEditorChangedDataWillLose", "Prefab editor will automaticallly refresh changed prefab, but detect some data changed in prefab editor, refresh the prefab editor will lose these data, do you want to continue?");
			auto Result = FMessageDialog::Open(EAppMsgType::YesNo, Msg);
			if (Result == EAppReturnType::Yes)
			{
				//reopen this prefab editor
				PrefabEditor->CloseWithoutCheckDataDirty();
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				AssetEditorSubsystem->OpenEditorForAsset(InPrefab);
			}
		}
		else
		{
			//reopen this prefab editor
			PrefabEditor->CloseWithoutCheckDataDirty();
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(InPrefab);
		}
	}
}

void FLexUIEditorTools::RefreshOnSubPrefabChange(ULGUIPrefab* InSubPrefab)
{
	auto AllPrefabs = GetAllPrefabArray();

	struct Local
	{
	public:
		static void RefreshAllPrefabsOnSubPrefabChange(const TArray<ULGUIPrefab*>& InPrefabs, ULGUIPrefab* InSubPrefab)
		{
			for (auto& Prefab : InPrefabs)
			{
				if (Prefab->IsPrefabBelongsToThisSubPrefab(InSubPrefab, false))
				{
					//check if is opened by prefab editor
					if (auto PrefabEditor = FLGUIPrefabEditor::GetEditorForPrefabIfValid(Prefab))//refresh opened prefab
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

TArray<ULGUIPrefab*> FLexUIEditorTools::GetAllPrefabArray()
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

	TArray<ULGUIPrefab*> AllPrefabs;
	auto PrefabClassName = ULGUIPrefab::StaticClass()->GetClassPathName();
	// Ensure all assets are loaded
	for (const FAssetData& Asset : ScriptAssetList)
	{
		// Gets the loaded asset, loads it if necessary
		if (Asset.AssetClassPath == PrefabClassName)
		{
			auto AssetObject = Asset.GetAsset();
			if (auto Prefab = Cast<ULGUIPrefab>(AssetObject))
			{
				Prefab->MakeAgentObjectsInPreviewWorld();
				AllPrefabs.Add(Prefab);
			}
		}
	}
#else
	TArray<ULGUIPrefab*> AllPrefabs;
#endif
	//collect prefabs that are not saved to disc yet
	for (TObjectIterator<ULGUIPrefab> Itr; Itr; ++Itr)
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
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
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
	GEditor->BeginTransaction(FText::FromString(TEXT("LGUI UnpackPrefab")));
	auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
	if (PrefabHelperObject != nullptr)
	{
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
	GEditor->BeginTransaction(FText::FromString(TEXT("LGUI SelectPrefabAsset")));
	auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
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
	GEditor->EndTransaction();
}
bool FLexUIEditorTools::CanBrowsePrefabAsset(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
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
	auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
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

bool FLexUIEditorTools::CanUpdateLevelPrefab(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedActor) && !PrefabHelperObject->IsInsidePrefabEditor())//Can only update prefab in level editor
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
void FLexUIEditorTools::UpdateLevelPrefab(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (auto SubPrefabDataPtr = PrefabHelperObject->SubPrefabMap.Find(SelectedActor))
		{
			PrefabHelperObject->RefreshOnSubPrefabDirty(SubPrefabDataPtr->PrefabAsset, SelectedActor);
		}
	}
}

ECheckBoxState FLexUIEditorTools::GetAutoUpdateLevelPrefab(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return ECheckBoxState::Undetermined;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return ECheckBoxState::Undetermined;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (auto SubPrefabDataPtr = PrefabHelperObject->SubPrefabMap.Find(SelectedActor))
		{
			return SubPrefabDataPtr->bAutoUpdate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
	}
	return ECheckBoxState::Undetermined;
}
void FLexUIEditorTools::ToggleLevelPrefabAutoUpdate(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (auto SubPrefabDataPtr = PrefabHelperObject->SubPrefabMap.Find(SelectedActor))
		{
			SubPrefabDataPtr->bAutoUpdate = !SubPrefabDataPtr->bAutoUpdate;
		}
	}
}
bool FLexUIEditorTools::CanCheckPrefabOverrideParameter(TFunction<AActor*()> GetSelectedActorFunction)
{
	auto SelectedActor = GetSelectedActorFunction();
	if (SelectedActor == nullptr)return false;
	if (!IsActorCompatibleWithLexUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = ULGUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
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
	for (TObjectIterator<ULGUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CleanupInvalidSubPrefab();
	}
}

bool FLexUIEditorTools::IsCanvasActor(AActor* InActor)
{
	if (auto rootComp = InActor->GetRootComponent())
	{
		if (auto rootUIItem = Cast<ULexWidget>(rootComp))
		{
			if (rootUIItem->IsCanvasWidget())
			{
				return true;
			}
		}
	}
	return false;
}
bool FLexUIEditorTools::IsSelectUIActor()
{
	auto selectedActors = FLexUIEditorToolsHelperFunctionHolder::ConvertSelectionToActors(GEditor->GetSelectedActors());
	if (selectedActors.Num() > 0)
	{
		bool allIsUI = true;
		for (auto actor : selectedActors)
		{
			if (IsValid(actor))
			{
				if (auto rootComp = actor->GetRootComponent())
				{
					auto uiRootComp = Cast<ULexWidget>(rootComp);
					if (uiRootComp == nullptr)
					{
						allIsUI = false;
					}
				}
			}
		}
		return allIsUI;
	}
	return false;
}
int FLexUIEditorTools::GetDrawcallCount(AActor* InActor)
{
	if (auto rootComp = InActor->GetRootComponent())
	{
		if (auto rootUIItem = Cast<ULexWidget>(rootComp))
		{
			if (auto canvas = rootUIItem->GetRenderCanvas())
			{
				return canvas->GetDrawCallCount();
			}
		}
	}
	return 0;
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
void FLexUIEditorTools::FocusToScreenSpaceUI()
{
	if (!GWorld)return;
	if (!GEditor)return;
	if (auto activeViewport = GEditor->GetActiveViewport())
	{
		if (auto viewportClient = activeViewport->GetClient())
		{
			auto editorViewportClient = (FEditorViewportClient*)viewportClient;
			for (TActorIterator<ALexWidgetActor> ActorItr(GWorld); ActorItr; ++ActorItr)
			{
				auto canvas = ActorItr->FindComponentByClass<ULexCanvas>();
				if (canvas != nullptr && canvas->IsRootCanvas() && canvas->IsRenderToScreenSpace())//make sure is screen space UI root
				{
					auto viewDistance = FVector::Distance(canvas->GetViewLocation(), canvas->GetLexWidget()->GetComponentLocation());
					auto halfViewWidth = viewDistance * FMath::Tan(FMath::DegreesToRadians(canvas->GetFieldOfView() * 0.5f));
					auto editorViewDistance = halfViewWidth / FMath::Tan(FMath::DegreesToRadians(editorViewportClient->FOVAngle * 0.5f));
					auto viewRotation = canvas->GetViewRotator().Quaternion();
					editorViewportClient->SetViewLocation(canvas->GetLexWidget()->GetComponentLocation() - viewRotation.GetForwardVector() * editorViewDistance);
					editorViewportClient->SetViewRotation(viewRotation.Rotator());
					editorViewportClient->SetLookAtLocation(canvas->GetLexWidget()->GetComponentLocation());
					break;
				}
			}
		}
	}
}
void FLexUIEditorTools::FocusToSelectedUI()
{
	if (!GEditor)return;
	if (auto activeViewport = GEditor->GetActiveViewport())
	{
		if (auto viewportClient = activeViewport->GetClient())
		{
			auto editorViewportClient = (FEditorViewportClient*)viewportClient;
			if (auto selectedActor = GetFirstSelectedActor())
			{
				if (auto selectedUIItem = Cast<ALexWidgetActor>(selectedActor))
				{
					if (auto renderCavnas = selectedUIItem->GetLexWidget()->GetRenderCanvas())
					{
						if (auto canvas = renderCavnas->GetRootCanvas())
						{
							if (canvas != nullptr)
							{
								editorViewportClient->SetViewLocation(canvas->GetViewLocation());
								auto viewRotation = canvas->GetViewRotator().Quaternion();
								editorViewportClient->SetViewRotation(viewRotation.Rotator());
								editorViewportClient->SetLookAtLocation(canvas->GetLexWidget()->GetComponentLocation());
							}
						}
					}
				}
			}
		}
	}
}

bool FLexUIEditorTools::IsActorCompatibleWithLexUIToolsMenu(AActor* InActor)
{
	if (InActor->IsA<ALexWidgetActor>()
		&& !FLGUIPrefabEditor::ActorIsRootAgent(InActor))
	{
		return true;
	}
	return false;
}

void FLexUIEditorTools::ForceGC()
{
	GEngine->ForceGarbageCollection();
}



#undef LOCTEXT_NAMESPACE