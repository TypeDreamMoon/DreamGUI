// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamTextCommitFanoutTestTypes.h"
#include "Core/Components/DreamWidget.h"
#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Event/DreamUIEventDelegate.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamTextInputCommitFanoutTest,
	"DreamGUI.TextInput.EverySubmitRouteRetainsTheCommittedValueAcrossListenerEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamTextInputCommitFanoutTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("A native submit listener prepares the next value"));
	OutTestCommands.Add(TEXT("native"));
	OutBeautifiedNames.Add(TEXT("A Blueprint submit listener prepares the next value"));
	OutTestCommands.Add(TEXT("blueprint"));
}

bool FDreamTextInputCommitFanoutTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the input rig is usable"), Rig.IsUsable()))return false;
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("CommitFanoutField"), nullptr, FVector2D(320.0, 40.0));
	if (!TestNotNull(TEXT("the real text field exists"), Field) ||
		!TestNotNull(TEXT("the field has its native input behaviour"), Field->InputBehaviour.Get()))return false;
	UUITextInput* Behaviour = Field->InputBehaviour;
	UDreamTextCommitFanoutProbe* Probe = Field->AddComponent<UDreamTextCommitFanoutProbe>();
	if (!TestNotNull(TEXT("the authored event has a real behaviour receiver"), Probe))return false;
	Probe->Field = Field;
	Probe->bReplaceFromBlueprint = Parameters == TEXT("blueprint");

	// Reflection supplies the public Blueprint/event-details routes without a test subclass
	// replacing Submit or bypassing the keyboard input that reaches it.
	const FMulticastDelegateProperty* BlueprintProperty = FindFProperty<FMulticastDelegateProperty>(UUITextInput::StaticClass(), TEXT("OnSubmitBP"));
	const FStructProperty* AuthoredProperty = FindFProperty<FStructProperty>(UUITextInput::StaticClass(), TEXT("OnSubmit"));
	if (!TestNotNull(TEXT("the Blueprint submit event exists"), BlueprintProperty) ||
		!TestNotNull(TEXT("the authored submit event exists"), AuthoredProperty))return false;
	FScriptDelegate BlueprintBinding;
	BlueprintBinding.BindUFunction(Probe, TEXT("HandleBlueprint"));
	BlueprintProperty->AddDelegate(BlueprintBinding, Behaviour);
	AuthoredProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Behaviour)->AddFunctionBinding(
		Field, Probe, TEXT("HandleAuthored"), EDreamUIEventDelegateParameterType::String, true);
	const FDelegateHandle NativeHandle = Behaviour->GetOnSubmitEvent().AddLambda([Probe](const FString& InText)
	{
		Probe->NativeValues.Add(InText);
		if (!Probe->bReplaceFromBlueprint)Probe->Field->SetText(TEXT("next"));
	});
	ON_SCOPE_EXIT { Behaviour->GetOnSubmitEvent().Remove(NativeHandle); };
	Rig.PumpFrames(1);

	FDreamElementRef Element = Rig.Driver()->Find(FDreamBy::Name(TEXT("CommitFanoutField")));
	Element->Type(TEXT("hello"));
	if (!TestEqual(TEXT("the player really entered the value to commit"), Field->GetText(), FString(TEXT("hello"))))return false;
	Element->Type(EKeys::Enter);

	auto CheckRoute = [this](const TCHAR* Route, const TArray<FString>& Values)
	{
		TestEqual(FString::Printf(TEXT("%s is called once"), Route), Values.Num(), 1);
		if (Values.Num() == 1)TestEqual(FString::Printf(TEXT("%s retains the submitted value"), Route), Values[0], FString(TEXT("hello")));
	};
	CheckRoute(TEXT("native submit"), Probe->NativeValues);
	CheckRoute(TEXT("Blueprint submit"), Probe->BlueprintValues);
	CheckRoute(TEXT("authored function binding"), Probe->AuthoredValues);
	TestEqual(TEXT("the listener's next value stays in the field"), Field->GetText(), FString(TEXT("next")));
	return true;
}

#endif
