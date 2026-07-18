// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabBehaviourUtils.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexWidget.h"

#include "Core/Components/LexVisual.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "LexUIPrefabBehaviourUtils"

namespace LexUIPrefabBehaviourUtils
{

FString GetCompanionBlueprintName(ULexUIPrefab* InPrefab)
{
	return InPrefab != nullptr ? FString::Printf(TEXT("BP_%s"), *InPrefab->GetName()) : FString();
}

ULexUIBehaviour* FindBehaviourComponent(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab)
{
	if (InRootWidget == nullptr || InPrefab == nullptr)return nullptr;
	// the companion is the blueprint ULexUIBehaviour on the root widget whose blueprint asset
	// name matches the convention -- name-matching (not "first blueprint behaviour") so a
	// reusable behaviour attached to the root isn't mistaken for the companion
	const FString CompanionName = GetCompanionBlueprintName(InPrefab);
	for (ULexUIBehaviour* Comp : InRootWidget->GetAllComponents())
	{
		if (Comp == nullptr)continue;
		if (auto BP = Cast<UBlueprint>(Comp->GetClass()->ClassGeneratedBy))
		{
			if (BP->GetName() == CompanionName)
			{
				return Comp;
			}
		}
	}
	return nullptr;
}

UBlueprint* FindBehaviourBlueprint(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab)
{
	if (auto Comp = FindBehaviourComponent(InRootWidget, InPrefab))
	{
		return Cast<UBlueprint>(Comp->GetClass()->ClassGeneratedBy);
	}
	return nullptr;
}

UBlueprint* CreateBehaviourBlueprint(ULexUIPrefab* InPrefab, ULexWidget* InRootWidget)
{
	if (InPrefab == nullptr || InRootWidget == nullptr)return nullptr;

	const FString PackagePath = FPackageName::GetLongPackagePath(InPrefab->GetOutermost()->GetName());
	const FString BaseName = GetCompanionBlueprintName(InPrefab);

	// re-attach an orphaned companion asset (created before, component lost without saving)
	// instead of minting BP_<PrefabName>1
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(PackagePath / BaseName + TEXT(".") + BaseName));
	if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr
		|| !Blueprint->GeneratedClass->IsChildOf(ULexUIBehaviour::StaticClass()))
	{
		FString PackageName, AssetName;
		FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(PackagePath / BaseName, TEXT(""), PackageName, AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)return nullptr;
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ULexUIBehaviour::StaticClass(), Package, *AssetName
			, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (Blueprint == nullptr)return nullptr;
		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
	}

	// attach the logic host to the root widget; LexUI's AddComponent registers it in the
	// widget's Components array, which the prefab serializes
	InRootWidget->Modify();
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	InRootWidget->AddComponent(GeneratedClass);
	return Blueprint;
}

FString MakeVariableNameForTarget(UObject* InTarget)
{
	FString Raw;
	if (auto Widget = Cast<ULexWidget>(InTarget))
	{
		Raw = Widget->GetDisplayName();
	}
	else if (auto Visual = Cast<ULexVisual>(InTarget))
	{
		Raw = Visual->GetWidget() != nullptr ? Visual->GetWidget()->GetDisplayName() : Visual->GetName();
	}
	else if (auto Behaviour = Cast<ULexUIBehaviour>(InTarget))
	{
		Raw = Behaviour->GetWidget() != nullptr ? Behaviour->GetWidget()->GetDisplayName() : Behaviour->GetName();
	}
	else if (InTarget != nullptr)
	{
		Raw = InTarget->GetName();
	}

	// sanitize to an identifier: alnum/underscore, non-ASCII kept (CJK display names are common)
	FString Result;
	Result.Reserve(Raw.Len());
	for (TCHAR Char : Raw)
	{
		Result.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') || Char > 0x7F ? Char : TEXT('_'));
	}
	if (Result.IsEmpty())Result = TEXT("Element");
	if (FChar::IsDigit(Result[0]))Result.InsertAt(0, TEXT('_'));
	return Result;
}

namespace
{
	enum class EDeclareVariableResult { Added, Reused, Incompatible, Failed };
	/** Declare (or accept a compatible existing) Instance-Editable member variable able to hold InTargetClass. */
	EDeclareVariableResult DeclareVariable(UBlueprint* InBlueprint, const FName InVarName, UClass* InTargetClass)
	{
		const int32 ExistingVarIndex = FBlueprintEditorUtils::FindNewVariableIndex(InBlueprint, InVarName);
		if (ExistingVarIndex == INDEX_NONE)
		{
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = InTargetClass;
			if (!FBlueprintEditorUtils::AddMemberVariable(InBlueprint, InVarName, PinType))
			{
				return EDeclareVariableResult::Failed;
			}
			// LexUI's prefab writer skips CPF_DisableEditOnInstance, which blueprint variables
			// carry by default -- clear it (== Instance Editable) so the value serializes
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(InBlueprint, InVarName, false);
			return EDeclareVariableResult::Added;
		}
		const FBPVariableDescription& ExistingVar = InBlueprint->NewVariables[ExistingVarIndex];
		UClass* ExistingClass = Cast<UClass>(ExistingVar.VarType.PinSubCategoryObject.Get());
		if (ExistingVar.VarType.PinCategory != UEdGraphSchema_K2::PC_Object
			|| ExistingClass == nullptr || !InTargetClass->IsChildOf(ExistingClass))
		{
			return EDeclareVariableResult::Incompatible;
		}
		if ((ExistingVar.PropertyFlags & CPF_DisableEditOnInstance) != 0)
		{
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(InBlueprint, InVarName, false);
		}
		return EDeclareVariableResult::Reused;
	}
}

bool PromoteToVariable(UBlueprint* InBlueprint, ULexWidget* InRootWidget, UObject* InTarget, const FString& InVariableName, FText& OutMessage)
{
	if (InBlueprint == nullptr || InRootWidget == nullptr || InTarget == nullptr)
	{
		OutMessage = LOCTEXT("PromoteError_InvalidInput", "Invalid input.");
		return false;
	}
	if (InTarget->GetClass()->ClassGeneratedBy == InBlueprint)
	{
		OutMessage = LOCTEXT("PromoteError_SelfClass", "Cannot promote an instance of the behaviour blueprint itself.");
		return false;
	}
	UClass* TargetClass = InTarget->GetClass();

	// preferred name first; on collision with an incompatible/inherited member, disambiguate
	// with the target class ("Text" taken -> "Text_LexText") instead of dead-ending
	FString ClassSuffix = TargetClass->GetName();
	const FString CandidateNames[] = { InVariableName, InVariableName + TEXT("_") + ClassSuffix };
	FName VarName = NAME_None;
	bool bAddedNew = false;
	for (const FString& Candidate : CandidateNames)
	{
		const EDeclareVariableResult Result = DeclareVariable(InBlueprint, FName(*Candidate), TargetClass);
		if (Result == EDeclareVariableResult::Added || Result == EDeclareVariableResult::Reused)
		{
			VarName = FName(*Candidate);
			bAddedNew = (Result == EDeclareVariableResult::Added);
			break;
		}
	}
	if (VarName.IsNone())
	{
		OutMessage = FText::Format(LOCTEXT("PromoteError_NoUsableName", "Neither \"{0}\" nor \"{1}\" can be used on {2}. Rename the element and promote again.")
			, FText::FromString(CandidateNames[0]), FText::FromString(CandidateNames[1]), FText::FromString(InBlueprint->GetName()));
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(InBlueprint);
	if (InBlueprint->Status == BS_Error)
	{
		if (bAddedNew)FBlueprintEditorUtils::RemoveMemberVariable(InBlueprint, VarName);
		OutMessage = FText::Format(LOCTEXT("PromoteError_CompileFailed", "{0} failed to compile; fix its errors and promote again."), FText::FromString(InBlueprint->GetName()));
		return false;
	}

	// re-find the companion instance (compile reinstanced it)
	ULexUIBehaviour* BehaviourComp = nullptr;
	for (ULexUIBehaviour* Comp : InRootWidget->GetAllComponents())
	{
		if (Comp != nullptr && Comp->GetClass()->ClassGeneratedBy == InBlueprint)
		{
			BehaviourComp = Comp;
			break;
		}
	}
	if (BehaviourComp == nullptr)
	{
		OutMessage = FText::Format(LOCTEXT("PromoteError_NoInstance", "No {0} instance found on the prefab root widget."), FText::FromString(InBlueprint->GetName()));
		return false;
	}

	auto Property = FindFProperty<FObjectProperty>(BehaviourComp->GetClass(), VarName);
	if (Property == nullptr || !TargetClass->IsChildOf(Property->PropertyClass))
	{
		OutMessage = FText::Format(LOCTEXT("PromoteError_NoProperty", "Compiled class has no compatible property \"{0}\"."), FText::FromName(VarName));
		return false;
	}
	UObject* PreviousValue = Property->GetObjectPropertyValue_InContainer(BehaviourComp);
	BehaviourComp->Modify();
	Property->SetObjectPropertyValue_InContainer(BehaviourComp, InTarget);

	if (PreviousValue != nullptr && PreviousValue != InTarget)
	{
		OutMessage = FText::Format(LOCTEXT("PromoteRebound", "Rebound {0}.{1}: {2} -> {3}")
			, FText::FromString(InBlueprint->GetName()), FText::FromName(VarName)
			, FText::FromString(PreviousValue->GetName()), FText::FromString(InTarget->GetName()));
	}
	else
	{
		OutMessage = FText::Format(LOCTEXT("PromoteSuccess", "{0}.{1} = {2}")
			, FText::FromString(InBlueprint->GetName()), FText::FromName(VarName), FText::FromString(InTarget->GetName()));
	}
	return true;
}

}//namespace LexUIPrefabBehaviourUtils

#undef LOCTEXT_NAMESPACE
