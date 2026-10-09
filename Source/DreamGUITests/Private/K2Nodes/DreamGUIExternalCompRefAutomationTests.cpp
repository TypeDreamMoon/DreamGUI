// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "K2Node_DreamGUICompRef.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "DreamUIComponentReference.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DreamExternalCompRefTest
{
	struct FBlueprintFixture
	{
		TStrongObjectPtr<UPackage> Package;
		UBlueprint* Blueprint;

		explicit FBlueprintFixture(const FString& Name)
			: Package(CreatePackage(*(TEXT("/Temp/DreamExternalCompRef/") + Name)))
			, Blueprint(FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package.Get(),
				FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()))
		{
		}
	};

	FEdGraphPinType ReferenceType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = FDreamUIComponentReference::StaticStruct();
		return Type;
	}

	FEdGraphPinType ObjectType(UClass* Class)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Object;
		Type.PinSubCategoryObject = Class;
		return Type;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamExternalCompRefTest,
	"DreamGUI.K2Node.ExternalComponentReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamExternalCompRefTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names.Add(TEXT("WithoutLocalShadow"));
	Commands.Add(TEXT("WithoutLocalShadow"));
	Names.Add(TEXT("WithUnrelatedLocalShadow"));
	Commands.Add(TEXT("WithUnrelatedLocalShadow"));
}

bool FDreamExternalCompRefTest::RunTest(const FString& Parameters)
{
	using namespace DreamExternalCompRefTest;
	const bool bShadow = Parameters == TEXT("WithUnrelatedLocalShadow");
	const FName ReferenceName(TEXT("ComponentRef"));
	const FName ProviderName(TEXT("Provider"));
	const FName StoredName(TEXT("Stored"));
	FBlueprintFixture Provider(TEXT("BP_Provider_") + Parameters);
	FBlueprintFixture Consumer(TEXT("BP_Consumer_") + Parameters);
	if (!TestNotNull(TEXT("provider blueprint"), Provider.Blueprint)
		|| !TestNotNull(TEXT("consumer blueprint"), Consumer.Blueprint))
	{
		return false;
	}
	FBlueprintEditorUtils::AddMemberVariable(Provider.Blueprint, ReferenceName, ReferenceType());
	FKismetEditorUtilities::CompileBlueprint(Provider.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	const FStructProperty* ProviderReference = FindFProperty<FStructProperty>(Provider.Blueprint->GeneratedClass, ReferenceName);
	if (!TestNotNull(TEXT("provider declares a component reference"), ProviderReference))
	{
		return false;
	}
	*ProviderReference->ContainerPtrToValuePtr<FDreamUIComponentReference>(Provider.Blueprint->GeneratedClass->GetDefaultObject())
		= FDreamUIComponentReference(USceneComponent::StaticClass());
	FBlueprintEditorUtils::AddMemberVariable(Consumer.Blueprint, ProviderName, ObjectType(Provider.Blueprint->GeneratedClass));
	FBlueprintEditorUtils::AddMemberVariable(Consumer.Blueprint, StoredName, ObjectType(UActorComponent::StaticClass()));
	if (bShadow)
	{
		FBlueprintEditorUtils::AddMemberVariable(Consumer.Blueprint, ReferenceName, ReferenceType());
	}
	FKismetEditorUtilities::CompileBlueprint(Consumer.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	if (bShadow)
	{
		const FStructProperty* Shadow = FindFProperty<FStructProperty>(Consumer.Blueprint->GeneratedClass, ReferenceName);
		if (!TestNotNull(TEXT("consumer has the unrelated shadow"), Shadow))
		{
			return false;
		}
		*Shadow->ContainerPtrToValuePtr<FDreamUIComponentReference>(Consumer.Blueprint->GeneratedClass->GetDefaultObject())
			= FDreamUIComponentReference(UInputComponent::StaticClass());
	}
	UEdGraph* Graph = Consumer.Blueprint->UbergraphPages.IsEmpty() ? nullptr : Consumer.Blueprint->UbergraphPages[0];
	if (!TestNotNull(TEXT("consumer event graph"), Graph))
	{
		return false;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	auto Getter = [Graph](FName Name, UClass* ExternalClass = nullptr)
	{
		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
		Graph->AddNode(Node, false, false);
		if (ExternalClass)
		{
			Node->VariableReference.SetExternalMember(Name, ExternalClass);
		}
		else
		{
			Node->VariableReference.SetSelfMember(Name);
		}
		Node->AllocateDefaultPins();
		return Node;
	};
	UK2Node_VariableGet* ProviderGet = Getter(ProviderName);
	UK2Node_VariableGet* ExternalGet = Getter(ReferenceName, Provider.Blueprint->GeneratedClass);
	UK2Node_DreamGUICompRef_GetComponent* Resolve = NewObject<UK2Node_DreamGUICompRef_GetComponent>(Graph);
	Graph->AddNode(Resolve, false, false);
	Resolve->AllocateDefaultPins();
	UEdGraphPin* Target = Schema->FindSelfPin(*ExternalGet, EGPD_Input);
	if (!TestNotNull(TEXT("external reference getter has a provider target"), Target))
	{
		return false;
	}
	ProviderGet->FindPinChecked(ProviderName, EGPD_Output)->MakeLinkTo(Target);
	ExternalGet->FindPinChecked(ReferenceName, EGPD_Output)->MakeLinkTo(Resolve->Pins[0]);
	Resolve->NodeConnectionListChanged();
	TestEqual(TEXT("output type comes from the external provider's default"),
		Resolve->Pins[1]->PinType.PinSubCategoryObject.Get(), static_cast<UObject*>(USceneComponent::StaticClass()));

	FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*Graph);
	UK2Node_CustomEvent* Event = EventCreator.CreateNode(false);
	EventCreator.Finalize();
	Event->CustomFunctionName = TEXT("ResolveExternal");
	UK2Node_VariableSet* Store = NewObject<UK2Node_VariableSet>(Graph);
	Graph->AddNode(Store, false, false);
	Store->VariableReference.SetSelfMember(StoredName);
	Store->AllocateDefaultPins();
	Event->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Store->GetExecPin());
	Resolve->Pins[1]->MakeLinkTo(Store->FindPinChecked(StoredName, EGPD_Input));
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Consumer.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	if (!TestEqual(TEXT("external-reference graph compiles"), Results.NumErrors, 0))
	{
		return false;
	}
	TStrongObjectPtr<UObject> ProviderInstance(NewObject<UObject>(GetTransientPackage(), Provider.Blueprint->GeneratedClass));
	TStrongObjectPtr<UObject> ConsumerInstance(NewObject<UObject>(GetTransientPackage(), Consumer.Blueprint->GeneratedClass));
	TStrongObjectPtr<USceneComponent> Component(NewObject<USceneComponent>());
	*ProviderReference->ContainerPtrToValuePtr<FDreamUIComponentReference>(ProviderInstance.Get())
		= FDreamUIComponentReference(Component.Get());
	const FObjectProperty* ProviderProperty = FindFProperty<FObjectProperty>(Consumer.Blueprint->GeneratedClass, ProviderName);
	const FObjectProperty* StoredProperty = FindFProperty<FObjectProperty>(Consumer.Blueprint->GeneratedClass, StoredName);
	UFunction* Function = ConsumerInstance->FindFunction(TEXT("ResolveExternal"));
	if (!TestTrue(TEXT("compiled consumer contains its properties and event"), ProviderProperty && StoredProperty && Function))
	{
		return false;
	}
	ProviderProperty->SetObjectPropertyValue_InContainer(ConsumerInstance.Get(), ProviderInstance.Get());
	TestTrue(TEXT("provider reference resolves before calling the node"),
		ProviderReference->ContainerPtrToValuePtr<FDreamUIComponentReference>(ProviderInstance.Get())->GetComponent() == Component.Get());
	ConsumerInstance->ProcessEvent(Function, nullptr);
	TestTrue(TEXT("compiled node returns the provider's component despite consumer variable names"),
		StoredProperty->GetObjectPropertyValue_InContainer(ConsumerInstance.Get()) == Component.Get());
	return true;
}

#endif
