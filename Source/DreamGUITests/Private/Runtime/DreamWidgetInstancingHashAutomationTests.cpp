// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetInstancingHashTestTypes.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetInstancingRetargetsAfterHashedKeysTest,
	"DreamGUI.UserWidget.HashedKeysDoNotPreventRetargetingMapValuesAndFollowingReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The property value iterator visits a map's key before its value. GetPropertyChain appends to the
 * caller's array, so reusing that array without resetting it made every reference after the first
 * hashed key look like another key. Keep a real map and set searchable by their unchanged keys,
 * while their neighbouring writable references must name each instance's own widget.
 */
bool FDreamWidgetInstancingRetargetsAfterHashedKeysTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamWidgetTree> Template(NewObject<UDreamWidgetTree>(GetTransientPackage()));
	UDreamWidget* Root = Template->ConstructWidget<UDreamWidget>();
	Root->SetDisplayName(TEXT("Root"));
	Template->RootWidget = Root;
	UDreamWidget* SourceKey = Template->ConstructWidget<UDreamWidget>();
	SourceKey->SetDisplayName(TEXT("Key"));
	SourceKey->SetParentBeforeRegister(Root);
	UDreamWidget* SourceValue = Template->ConstructWidget<UDreamWidget>();
	SourceValue->SetDisplayName(TEXT("Value"));
	SourceValue->SetParentBeforeRegister(Root);
	UDreamWidgetInstancingHashTestBehaviour* Source = Root->AddComponent<UDreamWidgetInstancingHashTestBehaviour>();
	if (!TestNotNull(TEXT("the archetype has the hash-container behaviour"), Source))
	{
		return false;
	}
	Source->ReferencesByWidget.Add(SourceKey, SourceValue);
	Source->WidgetSet.Add(SourceKey);
	Source->FollowingReference = SourceValue;

	TStrongObjectPtr<UDreamUserWidget> First(NewObject<UDreamUserWidget>(TestWorld.World));
	TStrongObjectPtr<UDreamUserWidget> Second(NewObject<UDreamUserWidget>(TestWorld.World));
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(First.Get(), First->GetClass(), Template.Get());
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(Second.Get(), Second->GetClass(), Template.Get());
	for (UDreamUserWidget* Instance : {First.Get(), Second.Get()})
	{
		UDreamWidgetTree* Tree = Instance->GetWidgetTree();
		UDreamWidget* InstanceRoot = Tree != nullptr ? Tree->FindWidgetByVariableName(TEXT("Root")) : nullptr;
		UDreamWidgetInstancingHashTestBehaviour* Behaviour = InstanceRoot != nullptr
			? InstanceRoot->GetComponent<UDreamWidgetInstancingHashTestBehaviour>() : nullptr;
		UDreamWidget* InstanceKey = Tree != nullptr ? Tree->FindWidgetByVariableName(TEXT("Key")) : nullptr;
		UDreamWidget* InstanceValue = Tree != nullptr ? Tree->FindWidgetByVariableName(TEXT("Value")) : nullptr;
		if (!TestNotNull(TEXT("each instance has the hash-container behaviour"), Behaviour)
			|| !TestNotNull(TEXT("each instance has a key counterpart"), InstanceKey)
			|| !TestNotNull(TEXT("each instance has a value counterpart"), InstanceValue))
		{
			return false;
		}
		TestNotEqual(TEXT("the behaviour is an instance rather than the archetype"), Behaviour, Source);
		TestNotEqual(TEXT("the writable reference has an instanced counterpart"), InstanceValue, SourceValue);
		TestEqual(TEXT("the map still has one entry"), Behaviour->ReferencesByWidget.Num(), 1);
		const TObjectPtr<UDreamWidget>* MappedValue = Behaviour->ReferencesByWidget.Find(SourceKey);
		if (!TestNotNull(TEXT("the unchanged map key remains searchable"), MappedValue))
		{
			return false;
		}
		TestEqual(TEXT("the map value after the hashed key names this instance's widget"), MappedValue->Get(), InstanceValue);
		TestFalse(TEXT("the map key was not rewritten to the instance key"), Behaviour->ReferencesByWidget.Contains(InstanceKey));
		TestEqual(TEXT("the set still has one entry"), Behaviour->WidgetSet.Num(), 1);
		TestTrue(TEXT("the unchanged set key remains searchable"), Behaviour->WidgetSet.Contains(SourceKey));
		TestFalse(TEXT("the set key was not rewritten to the instance key"), Behaviour->WidgetSet.Contains(InstanceKey));
		TestEqual(TEXT("the ordinary reference following hash containers names this instance's widget"),
			Behaviour->FollowingReference.Get(), InstanceValue);
	}

	TestNotEqual(TEXT("the two value counterparts are independent"),
		First->GetWidgetTree()->FindWidgetByVariableName(TEXT("Value")),
		Second->GetWidgetTree()->FindWidgetByVariableName(TEXT("Value")));
	TestEqual(TEXT("the archetype's map value is unchanged"), Source->ReferencesByWidget.FindChecked(SourceKey).Get(), SourceValue);
	TestTrue(TEXT("the archetype's set key is unchanged"), Source->WidgetSet.Contains(SourceKey));
	TestEqual(TEXT("the archetype's ordinary reference is unchanged"), Source->FollowingReference.Get(), SourceValue);
	return true;
}

#endif
