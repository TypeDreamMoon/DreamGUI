// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamEventBindingTestTypes.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"
#include "Event/DreamUIEventDelegate.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DreamWidgetInstancingEventTestLocal
{
	void* EventEntry(FDreamUIEventDelegate& InEvent)
	{
		const FArrayProperty* ListProperty = FindFProperty<FArrayProperty>(
			FDreamUIEventDelegate::StaticStruct(), TEXT("EventList"));
		check(ListProperty != nullptr);
		FScriptArrayHelper List(ListProperty, ListProperty->ContainerPtrToValuePtr<void>(&InEvent));
		check(List.Num() == 1);
		return List.GetRawPtr(0);
	}

	const FObjectPropertyBase* EventReferenceProperty(FName InName)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
			FDreamUIEventDelegateData::StaticStruct(), InName);
		check(Property != nullptr);
		return Property;
	}

	UObject* EventReference(FDreamUIEventDelegate& InEvent, FName InName)
	{
		return EventReferenceProperty(InName)->GetObjectPropertyValue_InContainer(EventEntry(InEvent));
	}

	UDreamWidgetTree* BuildTemplate(bool bInWidgetParameter)
	{
		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
		UDreamWidget* Root = Tree->ConstructWidget<UDreamWidget>();
		Root->SetDisplayName(TEXT("Root"));
		Tree->RootWidget = Root;
		auto AddChild = [Tree, Root](const TCHAR* InName)
		{
			UDreamWidget* Child = Tree->ConstructWidget<UDreamWidget>();
			Child->SetDisplayName(InName);
			Child->SetParentBeforeRegister(Root);
			return Child;
		};
		UDreamWidget* SenderWidget = AddChild(TEXT("Sender"));
		UDreamWidget* ReceiverWidget = AddChild(TEXT("Receiver"));
		UDreamWidget* ArgumentWidget = AddChild(TEXT("Argument"));
		UDreamEventBindingTestBehaviour* Sender = SenderWidget->AddComponent<UDreamEventBindingTestBehaviour>();
		UDreamEventBindingTestBehaviour* Receiver = ReceiverWidget->AddComponent<UDreamEventBindingTestBehaviour>();
		Sender->AuthoredEvent.AddFunctionBinding(ReceiverWidget, Receiver,
			bInWidgetParameter ? FName(TEXT("TakeWidget")) : FName(TEXT("Touch")),
			bInWidgetParameter ? EDreamUIEventDelegateParameterType::DreamWidget : EDreamUIEventDelegateParameterType::Empty,
			/*bInUseNativeParameter*/ false);
		// A saved authored entry carries HelperWidget and ReferenceObject, with no transient target
		// cache. Force that shape instead of letting AddFunctionBinding's live cache hide a bad resolve.
		void* Entry = EventEntry(Sender->AuthoredEvent);
		EventReferenceProperty(TEXT("TargetObject"))->SetObjectPropertyValue_InContainer(Entry, nullptr);
		if (bInWidgetParameter)
		{
			EventReferenceProperty(TEXT("ReferenceObject"))->SetObjectPropertyValue_InContainer(Entry, ArgumentWidget);
		}
		return Tree;
	}

	UDreamEventBindingTestBehaviour* Behaviour(UDreamWidgetTree* InTree, FName InName)
	{
		UDreamWidget* Widget = InTree != nullptr ? InTree->FindWidgetByVariableName(InName) : nullptr;
		return Widget != nullptr ? Widget->GetComponent<UDreamEventBindingTestBehaviour>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetInstancingRetargetsAuthoredEventTest,
	"DreamGUI.UserWidget.AuthoredEventTargetsBelongToEachClassInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetInstancingRetargetsAuthoredEventTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetInstancingEventTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(false));
	TStrongObjectPtr<UDreamUserWidget> First(NewObject<UDreamUserWidget>(TestWorld.World));
	TStrongObjectPtr<UDreamUserWidget> Second(NewObject<UDreamUserWidget>(TestWorld.World));
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(First.Get(), First->GetClass(), Template.Get());
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(Second.Get(), Second->GetClass(), Template.Get());
	UDreamEventBindingTestBehaviour* FirstSender = Behaviour(First->GetWidgetTree(), TEXT("Sender"));
	UDreamEventBindingTestBehaviour* SecondSender = Behaviour(Second->GetWidgetTree(), TEXT("Sender"));
	UDreamEventBindingTestBehaviour* FirstReceiver = Behaviour(First->GetWidgetTree(), TEXT("Receiver"));
	UDreamEventBindingTestBehaviour* SecondReceiver = Behaviour(Second->GetWidgetTree(), TEXT("Receiver"));
	if (!TestNotNull(TEXT("the first instance has its sender"), FirstSender)
		|| !TestNotNull(TEXT("the second instance has its sender"), SecondSender)
		|| !TestNotNull(TEXT("the first instance has its receiver"), FirstReceiver)
		|| !TestNotNull(TEXT("the second instance has its receiver"), SecondReceiver))
	{
		return false;
	}

	TestTrue(TEXT("the first event names its own receiver"),
		EventReference(FirstSender->AuthoredEvent, TEXT("HelperWidget")) == FirstReceiver->GetWidget());
	TestTrue(TEXT("the second event names its own receiver"),
		EventReference(SecondSender->AuthoredEvent, TEXT("HelperWidget")) == SecondReceiver->GetWidget());
	FirstSender->AuthoredEvent.FireEvent();
	TestEqual(TEXT("the first event calls the first receiver"), FirstReceiver->TouchCount, 1);
	TestEqual(TEXT("without calling the second receiver"), SecondReceiver->TouchCount, 0);
	SecondSender->AuthoredEvent.FireEvent();
	TestEqual(TEXT("the second event calls the second receiver"), SecondReceiver->TouchCount, 1);
	TestEqual(TEXT("without calling the first receiver again"), FirstReceiver->TouchCount, 1);
	TestEqual(TEXT("neither event calls the archetype's receiver"), Behaviour(Template.Get(), TEXT("Receiver"))->TouchCount, 0);
	TestTrue(TEXT("the archetype's authored target is unchanged"),
		EventReference(Behaviour(Template.Get(), TEXT("Sender"))->AuthoredEvent, TEXT("HelperWidget"))
		== Behaviour(Template.Get(), TEXT("Receiver"))->GetWidget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetInstancingRetargetsAuthoredWidgetArgumentTest,
	"DreamGUI.UserWidget.AuthoredEventWidgetArgumentsBelongToEachClassInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetInstancingRetargetsAuthoredWidgetArgumentTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetInstancingEventTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(true));
	TStrongObjectPtr<UDreamUserWidget> First(NewObject<UDreamUserWidget>(TestWorld.World));
	TStrongObjectPtr<UDreamUserWidget> Second(NewObject<UDreamUserWidget>(TestWorld.World));
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(First.Get(), First->GetClass(), Template.Get());
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(Second.Get(), Second->GetClass(), Template.Get());
	UDreamEventBindingTestBehaviour* FirstSender = Behaviour(First->GetWidgetTree(), TEXT("Sender"));
	UDreamEventBindingTestBehaviour* SecondSender = Behaviour(Second->GetWidgetTree(), TEXT("Sender"));
	UDreamEventBindingTestBehaviour* FirstReceiver = Behaviour(First->GetWidgetTree(), TEXT("Receiver"));
	UDreamEventBindingTestBehaviour* SecondReceiver = Behaviour(Second->GetWidgetTree(), TEXT("Receiver"));
	if (!TestNotNull(TEXT("the first instance has its sender"), FirstSender)
		|| !TestNotNull(TEXT("the second instance has its sender"), SecondSender)
		|| !TestNotNull(TEXT("the first instance has its receiver"), FirstReceiver)
		|| !TestNotNull(TEXT("the second instance has its receiver"), SecondReceiver))
	{
		return false;
	}
	UDreamWidget* FirstArgument = First->GetWidgetTree()->FindWidgetByVariableName(TEXT("Argument"));
	UDreamWidget* SecondArgument = Second->GetWidgetTree()->FindWidgetByVariableName(TEXT("Argument"));
	if (!TestNotNull(TEXT("the first instance has its argument"), FirstArgument)
		|| !TestNotNull(TEXT("the second instance has its argument"), SecondArgument))
	{
		return false;
	}
	TestTrue(TEXT("the first authored parameter names the first instance's widget"),
		EventReference(FirstSender->AuthoredEvent, TEXT("ReferenceObject")) == FirstArgument);
	TestTrue(TEXT("the second authored parameter names the second instance's widget"),
		EventReference(SecondSender->AuthoredEvent, TEXT("ReferenceObject")) == SecondArgument);

	FirstSender->AuthoredEvent.FireEvent();
	SecondSender->AuthoredEvent.FireEvent();
	TestEqual(TEXT("the first handler received one call"), FirstReceiver->WidgetCallCount, 1);
	TestEqual(TEXT("the second handler received one call"), SecondReceiver->WidgetCallCount, 1);
	TestTrue(TEXT("the first handler received its own widget"), FirstReceiver->LastWidget == FirstArgument);
	TestTrue(TEXT("the second handler received its own widget"), SecondReceiver->LastWidget == SecondArgument);
	TestNotEqual(TEXT("the two instance arguments are independent"), FirstArgument, SecondArgument);
	TestEqual(TEXT("the archetype's handler was never called"), Behaviour(Template.Get(), TEXT("Receiver"))->WidgetCallCount, 0);
	TestTrue(TEXT("the archetype's authored argument is unchanged"),
		EventReference(Behaviour(Template.Get(), TEXT("Sender"))->AuthoredEvent, TEXT("ReferenceObject"))
		== Template->FindWidgetByVariableName(TEXT("Argument")));
	return true;
}

#endif
