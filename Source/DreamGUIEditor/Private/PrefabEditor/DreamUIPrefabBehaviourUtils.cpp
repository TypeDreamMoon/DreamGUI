// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIPrefabBehaviourUtils.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"

#include "Core/Components/DreamVisual.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamPointerEventData.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "DreamUIBehaviourEditorBackend.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabBehaviourUtils"

namespace DreamUIPrefabBehaviourUtils
{

FString GetCompanionBlueprintName(UDreamUIPrefab* InPrefab)
{
	return InPrefab != nullptr ? FString::Printf(TEXT("BP_%s"), *InPrefab->GetName()) : FString();
}

UDreamUIBehaviour* FindBehaviourComponent(UDreamWidget* InRootWidget, UDreamUIPrefab* InPrefab)
{
	if (InRootWidget == nullptr || InPrefab == nullptr)return nullptr;
	if (UClass* ExplicitClass = InPrefab->GetBehaviourClass())
	{
		UDreamUIBehaviour* Match = nullptr;
		for (UDreamUIBehaviour* Comp : InRootWidget->GetAllComponents())
		{
			if (IsValid(Comp) && Comp->GetClass() == ExplicitClass)
			{
				if (Match != nullptr)
				{
					return nullptr;
				}
				Match = Comp;
			}
		}
		return Match;
	}
	// the companion is the blueprint UDreamUIBehaviour on the root widget whose blueprint asset
	// name matches the convention -- name-matching (not "first blueprint behaviour") so a
	// reusable behaviour attached to the root isn't mistaken for the companion
	const FString CompanionName = GetCompanionBlueprintName(InPrefab);
	for (UDreamUIBehaviour* Comp : InRootWidget->GetAllComponents())
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

UBlueprint* FindBehaviourBlueprint(UDreamWidget* InRootWidget, UDreamUIPrefab* InPrefab)
{
	if (auto Comp = FindBehaviourComponent(InRootWidget, InPrefab))
	{
		return Cast<UBlueprint>(Comp->GetClass()->ClassGeneratedBy);
	}
	return nullptr;
}

UBlueprint* CreateBehaviourBlueprint(UDreamUIPrefab* InPrefab, UDreamWidget* InRootWidget)
{
	if (InPrefab == nullptr || InRootWidget == nullptr)return nullptr;

	const FString PackagePath = FPackageName::GetLongPackagePath(InPrefab->GetOutermost()->GetName());
	const FString BaseName = GetCompanionBlueprintName(InPrefab);

	// re-attach an orphaned companion asset (created before, component lost without saving)
	// instead of minting BP_<PrefabName>1
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(PackagePath / BaseName + TEXT(".") + BaseName));
	if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr
		|| !Blueprint->GeneratedClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		FString PackageName, AssetName;
		FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(PackagePath / BaseName, TEXT(""), PackageName, AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)return nullptr;
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UDreamUIBehaviour::StaticClass(), Package, *AssetName
			, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (Blueprint == nullptr)return nullptr;
		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
	}

	return Blueprint;
}

FString MakeVariableNameForTarget(UObject* InTarget)
{
	FString Raw;
	if (auto Widget = Cast<UDreamWidget>(InTarget))
	{
		Raw = Widget->GetDisplayName();
	}
	else if (auto Visual = Cast<UDreamVisual>(InTarget))
	{
		Raw = Visual->GetWidget() != nullptr ? Visual->GetWidget()->GetDisplayName() : Visual->GetName();
	}
	else if (auto Behaviour = Cast<UDreamUIBehaviour>(InTarget))
	{
		Raw = Behaviour->GetWidget() != nullptr ? Behaviour->GetWidget()->GetDisplayName() : Behaviour->GetName();
	}
	else if (InTarget != nullptr)
	{
		Raw = InTarget->GetName();
	}

	// Delegated, not reimplemented. The class model's compiler declares variables from this same rule
	// and its runtime resolves bindings with it, so a second copy here -- even one that starts out
	// identical, as this did -- is a naming rule waiting to drift. Which target a name is read off is
	// this function's business; what a name becomes is not.
	return UDreamWidgetTree::SanitizeIdentifier(Raw);
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
			// DreamUI's prefab writer skips CPF_DisableEditOnInstance, which blueprint variables
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

bool PromoteToVariable(UBlueprint* InBlueprint, UDreamWidget* InRootWidget, UObject* InTarget, const FString& InVariableName, FText& OutMessage)
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
	// with the target class ("Text" taken -> "Text_DreamText") instead of dead-ending
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
	UDreamUIBehaviour* BehaviourComp = nullptr;
	for (UDreamUIBehaviour* Comp : InRootWidget->GetAllComponents())
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

void DiscoverEvents(UDreamWidget* InWidget, TArray<FDiscoveredEvent>& OutEvents)
{
	OutEvents.Reset();
	if (InWidget == nullptr)return;
	static const FName EventDelegateStructName = FDreamUIEventDelegate::StaticStruct()->GetFName();
	for (UDreamUIBehaviour* Comp : InWidget->GetAllComponents())
	{
		if (Comp == nullptr)continue;
		for (TFieldIterator<FStructProperty> PropertyIt(Comp->GetClass()); PropertyIt; ++PropertyIt)
		{
			FStructProperty* StructProperty = *PropertyIt;
			if (StructProperty->Struct != nullptr && StructProperty->Struct->GetFName() == EventDelegateStructName)
			{
				FDiscoveredEvent Event;
				Event.Component = Comp;
				Event.EventProperty = StructProperty;
				Event.DisplayName = StructProperty->GetName();
				Event.bIsBound = StructProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Comp)->IsBound();
				OutEvents.Add(Event);
			}
		}
	}
}

namespace
{
	/** Blueprint pin type for a handler parameter matching the event's native parameter. False = generate parameterless. */
	bool MakePinTypeForEventParam(EDreamUIEventDelegateParameterType InType, FEdGraphPinType& OutPinType)
	{
		OutPinType = FEdGraphPinType();
		switch (InType)
		{
		case EDreamUIEventDelegateParameterType::Bool:   OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true;
		case EDreamUIEventDelegateParameterType::Float:  OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real; OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float; return true;
		case EDreamUIEventDelegateParameterType::Double: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real; OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double; return true;
		case EDreamUIEventDelegateParameterType::Int32:  OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int; return true;
		case EDreamUIEventDelegateParameterType::Int64:  OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64; return true;
		case EDreamUIEventDelegateParameterType::UInt8:  OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte; return true;
		case EDreamUIEventDelegateParameterType::String: OutPinType.PinCategory = UEdGraphSchema_K2::PC_String; return true;
		case EDreamUIEventDelegateParameterType::Name:   OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name; return true;
		case EDreamUIEventDelegateParameterType::Text:   OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text; return true;
		case EDreamUIEventDelegateParameterType::Vector2:    OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get(); return true;
		case EDreamUIEventDelegateParameterType::Vector3:    OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get(); return true;
		case EDreamUIEventDelegateParameterType::Vector4:    OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FVector4>::Get(); return true;
		case EDreamUIEventDelegateParameterType::Color:      OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FColor>::Get(); return true;
		case EDreamUIEventDelegateParameterType::LinearColor:OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get(); return true;
		case EDreamUIEventDelegateParameterType::Quaternion: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FQuat>::Get(); return true;
		case EDreamUIEventDelegateParameterType::Rotator:    OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get(); return true;
		case EDreamUIEventDelegateParameterType::Asset:  OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object; OutPinType.PinSubCategoryObject = UObject::StaticClass(); return true;
		case EDreamUIEventDelegateParameterType::DreamWidget: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object; OutPinType.PinSubCategoryObject = UDreamWidget::StaticClass(); return true;
		case EDreamUIEventDelegateParameterType::Class:  OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class; OutPinType.PinSubCategoryObject = UObject::StaticClass(); return true;
		case EDreamUIEventDelegateParameterType::PointerEvent: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object; OutPinType.PinSubCategoryObject = UDreamPointerEventData::StaticClass(); return true;
		default: return false;//Empty / None / unmapped -> parameterless handler
		}
	}
}

FName AddEventHandler(UBlueprint* InBlueprint, UDreamWidget* InRootWidget, const FDiscoveredEvent& InEvent,
	EDreamUIBehaviourHandlerType InHandlerType, FText& OutMessage)
{
	if (InBlueprint == nullptr || InRootWidget == nullptr || InEvent.Component == nullptr || InEvent.EventProperty == nullptr)
	{
		OutMessage = LOCTEXT("AddEventError_InvalidInput", "Invalid input.");
		return NAME_None;
	}

	// reuse: if this event already has a handler on the companion, jump to it instead of
	// minting an orphan function on every click
	auto LiveEvent = InEvent.EventProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(InEvent.Component);
	UDreamUIBehaviour* Companion = nullptr;
	for (UDreamUIBehaviour* Comp : InRootWidget->GetAllComponents())
	{
		if (Comp != nullptr && Comp->GetClass()->ClassGeneratedBy == InBlueprint) { Companion = Comp; break; }
	}
	if (Companion != nullptr)
	{
		const FName Existing = LiveEvent->FindFunctionBoundToComponent(Companion);
		if (!Existing.IsNone())
		{
			OutMessage = FText::Format(LOCTEXT("AddEventReuse", "{0}.{1} is already handled by {2}.{3}")
				, FText::FromString(InEvent.Component->GetName()), FText::FromString(InEvent.DisplayName)
				, FText::FromString(InBlueprint->GetName()), FText::FromName(Existing));
			return Existing;
		}
	}
	if (LiveEvent->IsBound())
	{
		OutMessage = FText::Format(LOCTEXT("AddEventAlreadyBound", "{0}.{1} already has an event binding."),
			FText::FromString(InEvent.Component->GetName()), FText::FromString(InEvent.DisplayName));
		return NAME_None;
	}

	// event's native parameter type from the CDO (SupportParameterType is set in the constructor)
	auto CDO = InEvent.Component->GetClass()->GetDefaultObject();
	auto DefaultEvent = InEvent.EventProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(CDO);
	const EDreamUIEventDelegateParameterType ParamType = DefaultEvent->GetSupportParameterType();

	// handler name: On<Event>_<WidgetName>, deduped
	FString BaseName = FString::Printf(TEXT("%s_%s"), *InEvent.DisplayName, *MakeVariableNameForTarget(InEvent.Component->GetWidget()));
	const FName HandlerName = FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint, BaseName);

	FEdGraphPinType ParamPinType;
	bool bUseNativeParameter = MakePinTypeForEventParam(ParamType, ParamPinType);
	if (InHandlerType == EDreamUIBehaviourHandlerType::Function)
	{
		UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, HandlerName,
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(InBlueprint, FuncGraph, true, static_cast<UClass*>(nullptr));
		if (bUseNativeParameter)
		{
			TArray<UK2Node_FunctionEntry*> EntryNodes;
			FuncGraph->GetNodesOfClass(EntryNodes);
			if (EntryNodes.Num() > 0)
			{
				EntryNodes[0]->CreateUserDefinedPin(TEXT("Value"), ParamPinType, EGPD_Output);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
			}
			else
			{
				bUseNativeParameter = false;
			}
		}
	}
	else
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(InBlueprint);
		if (EventGraph == nullptr)
		{
			EventGraph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, UEdGraphSchema_K2::GN_EventGraph,
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddUbergraphPage(InBlueprint, EventGraph);
		}

		UK2Node_CustomEvent* CustomEvent = NewObject<UK2Node_CustomEvent>(EventGraph);
		CustomEvent->CustomFunctionName = HandlerName;
		CustomEvent->bIsEditable = true;
		CustomEvent->SetFlags(RF_Transactional);
		CustomEvent->CreateNewGuid();
		CustomEvent->PostPlacedNewNode();
		CustomEvent->AllocateDefaultPins();
		const FVector2D Position = EventGraph->GetGoodPlaceForNewNode();
		CustomEvent->NodePosX = static_cast<int32>(Position.X);
		CustomEvent->NodePosY = static_cast<int32>(Position.Y);
		if (bUseNativeParameter)
		{
			CustomEvent->CreateUserDefinedPin(TEXT("Value"), ParamPinType, EGPD_Output);
		}
		EventGraph->Modify();
		EventGraph->AddNode(CustomEvent, true, false);
		FBlueprintEditorUtils::ValidateBlueprintChildVariables(InBlueprint, HandlerName);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
	}

	FKismetEditorUtilities::CompileBlueprint(InBlueprint);
	if (InBlueprint->Status == BS_Error)
	{
		OutMessage = FText::Format(LOCTEXT("AddEventError_CompileFailed", "{0} failed to compile; fix its errors and add the handler again."), FText::FromString(InBlueprint->GetName()));
		return NAME_None;
	}

	// re-find the companion (compile reinstanced it)
	UDreamUIBehaviour* BehaviourComp = nullptr;
	for (UDreamUIBehaviour* Comp : InRootWidget->GetAllComponents())
	{
		if (Comp != nullptr && Comp->GetClass()->ClassGeneratedBy == InBlueprint) { BehaviourComp = Comp; break; }
	}
	if (BehaviourComp == nullptr)
	{
		OutMessage = FText::Format(LOCTEXT("AddEventError_NoInstance", "No {0} instance found on the prefab root widget."), FText::FromString(InBlueprint->GetName()));
		return NAME_None;
	}

	InEvent.Component->Modify();
	// parameterless fallback stores ParamType=Empty so IsStillSupported accepts it at runtime
	const EDreamUIEventDelegateParameterType BindingParamType = bUseNativeParameter ? ParamType : EDreamUIEventDelegateParameterType::Empty;
	LiveEvent->AddFunctionBinding(InRootWidget, BehaviourComp, HandlerName, BindingParamType, bUseNativeParameter);
	// Modify() records the value for undo, not for the writer. An object inside a sub-prefab
	// instance serializes nothing but its collected overrides, and the helper collects one only
	// from a property-changed notification -- the same one the details panel raises when a
	// designer wires this event by hand. Without it the node survives the save and the binding
	// does not.
	FDreamUIUtils::NotifyPropertyChanged(InEvent.Component, InEvent.EventProperty);

	OutMessage = FText::Format(LOCTEXT("AddEventSuccess", "{0}.{1} -> {2}.{3} ({4})")
		, FText::FromString(InEvent.Component->GetName()), FText::FromString(InEvent.DisplayName)
		, FText::FromString(InBlueprint->GetName()), FText::FromName(HandlerName)
		, InHandlerType == EDreamUIBehaviourHandlerType::Function ? LOCTEXT("HandlerTypeFunction", "Function") : LOCTEXT("HandlerTypeEvent", "Event"));
	return HandlerName;
}

// The descendant widget a bound value belongs to (for the "still in this prefab" check):
// a widget is itself, a behaviour/visual reports the widget it lives on.
static UDreamWidget* OwnerWidgetOfBoundValue(UObject* InValue)
{
	if (InValue == nullptr) return nullptr;
	if (auto Widget = Cast<UDreamWidget>(InValue)) return Widget;
	if (auto Behaviour = Cast<UDreamUIBehaviour>(InValue)) return Behaviour->GetWidget();
	if (auto Visual = Cast<UDreamVisual>(InValue)) return Visual->GetTypedOuter<UDreamWidget>();
	return nullptr;
}

void CollectSubPrefabWidgets(UDreamUIPrefab* InPrefab, TSet<const UDreamWidget*>& OutSubPrefabWidgets)
{
	OutSubPrefabWidgets.Reset();
	if (!IsValid(InPrefab)) return;
	UDreamUIPrefabHelperObject* Helper = InPrefab->GetPrefabHelperObject();
	if (Helper == nullptr) return;
	for (const auto& SubPrefabPair : Helper->SubPrefabMap)
	{
		for (const auto& GuidToObjectPair : SubPrefabPair.Value.MapGuidToObject)
		{
			if (const UDreamWidget* SubPrefabWidget = Cast<UDreamWidget>(GuidToObjectPair.Value))
			{
				OutSubPrefabWidgets.Add(SubPrefabWidget);
			}
		}
	}
}

void AutoBindAndValidate(UDreamWidget* InRootWidget, UDreamUIPrefab* InPrefab, TArray<FString>& OutBoundDetails,
	TArray<FString>& OutProblems, bool bPerformAutoBind)
{
	OutBoundDetails.Reset();
	OutProblems.Reset();
	if (InRootWidget == nullptr) return;

	// Flatten the subtree and index widgets by their sanitized display name (the same key
	// PromoteToVariable / MakeVariableNameForTarget mint variables from). Names collect into
	// arrays so duplicates surface as ambiguity instead of an arbitrary silent pick.
	// Sub-prefab widgets are kept out of the candidate index and reported instead: the writer
	// cannot reference them, so binding one would report success and come back null after save.
	TSet<const UDreamWidget*> SubPrefabWidgets;
	CollectSubPrefabWidgets(InPrefab, SubPrefabWidgets);

	TArray<UDreamWidget*> Subtree;
	TMap<FString, TArray<UDreamWidget*>> NameToWidgets;
	TMap<FString, int32> SubPrefabNameCounts;
	{
		TArray<UDreamWidget*> Stack;
		TSet<const UDreamWidget*> VisitedWidgets;
		Stack.Add(InRootWidget);
		while (Stack.Num() > 0)
		{
			UDreamWidget* Widget = Stack.Pop();
			if (!IsValid(Widget) || VisitedWidgets.Contains(Widget)) continue;
			VisitedWidgets.Add(Widget);
			Subtree.Add(Widget);
			if (SubPrefabWidgets.Contains(Widget))
			{
				SubPrefabNameCounts.FindOrAdd(MakeVariableNameForTarget(Widget))++;
			}
			else
			{
				NameToWidgets.FindOrAdd(MakeVariableNameForTarget(Widget)).Add(Widget);
			}
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				if (IsValid(Child)) Stack.Add(Child);
			}
		}
	}
	const TSet<UDreamWidget*> SubtreeSet(Subtree);

	// Only the prefab's OWN companion behaviour takes part in BindWidget. A designer may also
	// attach reusable behaviours to the root (exactly why FindBehaviourComponent matches by
	// BP_<PrefabName>); their Instance-Editable variables stay under the designer's control and
	// must not be silently rebound to same-named descendants on every save.
	UDreamUIBehaviour* Companion = FindBehaviourComponent(InRootWidget, InPrefab);
	if (Companion != nullptr)
	{
		UClass* ComponentClass = Companion->GetClass();

		// Hard object references only. FObjectProperty excludes weak/soft/lazy refs, which DreamUI
		// does not serialize as hard references -- auto-binding one would report success yet come
		// back null after save/load, the very failure this pass exists to catch. Requiring CPF_Edit
		// keeps this backend-neutral: Blueprint, native and AngelScript-declared instance properties
		// all participate while private/transient runtime caches remain excluded.
		for (TFieldIterator<FObjectProperty> It(ComponentClass); It; ++It)
		{
			FObjectProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient)) continue;
			UClass* TargetClass = Prop->PropertyClass;
			if (TargetClass == nullptr) continue;
			const bool bBindable =
				TargetClass->IsChildOf(UDreamWidget::StaticClass())
				|| TargetClass->IsChildOf(UDreamVisual::StaticClass())
				|| TargetClass->IsChildOf(UDreamUIBehaviour::StaticClass());
			if (!bBindable) continue;

			const FString VarName = Prop->GetName();
			// DreamUI's prefab writer drops CPF_DisableEditOnInstance -- a non-Instance-Editable
			// reference silently comes back null after save/load, so never rely on one.
			const bool bSavable = (Prop->PropertyFlags & CPF_DisableEditOnInstance) == 0;
			UObject* Value = Prop->GetObjectPropertyValue_InContainer(Companion);

			if (Value != nullptr)
			{
				// Validate an existing binding: still inside this prefab, and actually savable.
				UDreamWidget* OwnerWidget = OwnerWidgetOfBoundValue(Value);
				if (!IsValid(Value) || OwnerWidget == nullptr || !SubtreeSet.Contains(OwnerWidget))
				{
					OutProblems.Add(FString::Printf(TEXT("'%s' points to '%s', which is not in this prefab -- the reference will not save.")
						, *VarName, *GetNameSafe(Value)));
				}
				else if (SubPrefabWidgets.Contains(OwnerWidget))
				{
					OutProblems.Add(FString::Printf(TEXT("'%s' points to '%s' inside a sub-prefab instance -- the reference will not save. Bind it from that sub-prefab's own behaviour instead.")
						, *VarName, *MakeVariableNameForTarget(OwnerWidget)));
				}
				else if (!bSavable)
				{
					OutProblems.Add(FString::Printf(TEXT("'%s' is bound but not Instance Editable -- it will come back empty after save. Promote via the menu to fix.")
						, *VarName));
				}
				continue;
			}

			if (!bSavable) continue;//can't persist a bind here; leave it to Promote to fix the flag

			// Null + savable + name matches a descendant -> UMG BindWidget: wire it up.
			TArray<UDreamWidget*>* Matches = NameToWidgets.Find(VarName);
			if (Matches == nullptr || Matches->Num() == 0)
			{
				if (const int32* SubPrefabMatches = SubPrefabNameCounts.Find(VarName))
				{
					OutProblems.Add(FString::Printf(TEXT("'%s' matches %d widget(s) inside a sub-prefab instance, which cannot be referenced from this prefab. Bind it from that sub-prefab's own behaviour instead.")
						, *VarName, *SubPrefabMatches));
				}
				continue;
			}
			if (Matches->Num() > 1)
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' matches %d widgets by name -- rename to disambiguate, or bind it by hand.")
					, *VarName, Matches->Num()));
				continue;
			}

			UDreamWidget* MatchWidget = (*Matches)[0];
			UObject* NewValue = nullptr;
			FString BoundKind;
			if (TargetClass->IsChildOf(UDreamWidget::StaticClass()))
			{
				if (MatchWidget->IsA(TargetClass)) { NewValue = MatchWidget; BoundKind = TEXT("widget"); }
			}
			else if (TargetClass->IsChildOf(UDreamVisual::StaticClass()))
			{
				UDreamVisual* Visual = MatchWidget->GetVisual();
				if (Visual && Visual->IsA(TargetClass)) { NewValue = Visual; BoundKind = TEXT("visual"); }
			}
			else //UDreamUIBehaviour
			{
				UDreamUIBehaviour* Behaviour = MatchWidget->GetComponent(TargetClass);
				if (Behaviour) { NewValue = Behaviour; BoundKind = TEXT("behaviour"); }
			}

			if (NewValue == nullptr)
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' matches widget '%s' by name, but its %s is not a '%s'.")
					, *VarName, *MakeVariableNameForTarget(MatchWidget)
					, TargetClass->IsChildOf(UDreamVisual::StaticClass()) ? TEXT("visual") : TEXT("component")
					, *TargetClass->GetName()));
				continue;
			}

			if (bPerformAutoBind)
			{
				Companion->Modify();
				Prop->SetObjectPropertyValue_InContainer(Companion, NewValue);
				OutBoundDetails.Add(FString::Printf(TEXT("%s -> %s (%s)"), *VarName, *MakeVariableNameForTarget(MatchWidget), *BoundKind));
			}
		}
	}
}

}//namespace DreamUIPrefabBehaviourUtils

#undef LOCTEXT_NAMESPACE
