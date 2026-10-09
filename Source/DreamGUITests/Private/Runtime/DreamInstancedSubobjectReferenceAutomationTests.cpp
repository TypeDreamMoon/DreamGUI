// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Curves/CurveFloat.h"
#include "DreamEventBindingTestTypes.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"
#include "PlayTween/DreamUIPlayTweenComponent.h"
#include "PlayTween/DreamUIPlayTweenSequenceComponent.h"
#include "PlayTween/DreamUIPlayTween_Params.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamInstancedSubobjectEventReferenceTest,
	"DreamGUI.WidgetTree.InstancedSubobjects.ACopiedPlayTweenCallsOnlyItsOwnTreesReceiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamInstancedSubobjectEventReferenceTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("DuplicateSubtree"));
	OutTestCommands.Add(TEXT("Duplicate"));
	OutBeautifiedNames.Add(TEXT("Generated widget instance"));
	OutTestCommands.Add(TEXT("Instance"));
}

bool FDreamInstancedSubobjectEventReferenceTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamWidgetTree> SourceTree(NewObject<UDreamWidgetTree>(GetTransientPackage()));
	UDreamWidget* Root = SourceTree->ConstructWidget<UDreamWidget>();
	Root->SetDisplayName(TEXT("Root"));
	SourceTree->RootWidget = Root;
	UDreamWidget* Receiver = SourceTree->ConstructWidget<UDreamWidget>();
	Receiver->SetDisplayName(TEXT("Receiver"));
	Receiver->SetParentBeforeRegister(Root);
	UDreamEventBindingTestBehaviour* SourceReceiver = Receiver->AddComponent<UDreamEventBindingTestBehaviour>();
	UDreamUIPlayTweenComponent* SourcePlayer = Root->AddComponent<UDreamUIPlayTweenComponent>();
	if (!TestNotNull(TEXT("the source has a receiver"), SourceReceiver)
		|| !TestNotNull(TEXT("the source has a native play-tween component"), SourcePlayer)) return false;

	// Author the native inline object exactly as its Details property does; no test-only subclass
	// or replacement event dispatcher participates in the copying or the eventual playback.
	UDreamUIPlayTween_Float* SourceTween = NewObject<UDreamUIPlayTween_Float>(SourcePlayer);
	const FObjectPropertyBase* TweenProperty = FindFProperty<FObjectPropertyBase>(UDreamUIPlayTweenComponent::StaticClass(), TEXT("PlayTween"));
	const FStructProperty* CompletionProperty = FindFProperty<FStructProperty>(UDreamUIPlayTween::StaticClass(), TEXT("OnComplete"));
	if (!TestNotNull(TEXT("the native component exposes its instanced tween property"), TweenProperty)
		|| !TestNotNull(TEXT("the inline tween exposes its authored completion event"), CompletionProperty)) return false;
	TweenProperty->SetObjectPropertyValue_InContainer(SourcePlayer, SourceTween);
	FDreamUIEventDelegate* Completion = CompletionProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(SourceTween);
	Completion->AddFunctionBinding(Receiver, SourceReceiver, TEXT("Touch"), EDreamUIEventDelegateParameterType::Empty, false);

	TStrongObjectPtr<UDreamWidgetTree> DuplicatedTree;
	TStrongObjectPtr<UDreamUserWidget> Instance;
	UDreamWidgetTree* CopyTree = nullptr;
	if (Parameters == TEXT("Duplicate"))
	{
		DuplicatedTree.Reset(NewObject<UDreamWidgetTree>(TestWorld.World));
		DuplicatedTree->RootWidget = UDreamWidget::DuplicateSubtree(DuplicatedTree.Get(), Root);
		CopyTree = DuplicatedTree.Get();
	}
	else
	{
		Instance.Reset(NewObject<UDreamUserWidget>(TestWorld.World));
		UDreamWidgetGeneratedClass::InitializeWidgetStatic(Instance.Get(), Instance->GetClass(), SourceTree.Get());
		CopyTree = Instance->GetWidgetTree();
	}
	UDreamWidget* CopyRoot = CopyTree != nullptr ? CopyTree->FindWidgetByVariableName(TEXT("Root")) : nullptr;
	UDreamWidget* CopyReceiverWidget = CopyTree != nullptr ? CopyTree->FindWidgetByVariableName(TEXT("Receiver")) : nullptr;
	UDreamUIPlayTweenComponent* CopyPlayer = CopyRoot != nullptr ? CopyRoot->GetComponent<UDreamUIPlayTweenComponent>() : nullptr;
	UDreamEventBindingTestBehaviour* CopyReceiver = CopyReceiverWidget != nullptr
		? CopyReceiverWidget->GetComponent<UDreamEventBindingTestBehaviour>() : nullptr;
	if (!TestNotNull(TEXT("the copied tree has its native player"), CopyPlayer)
		|| !TestNotNull(TEXT("the copied tree has its own receiver"), CopyReceiver)
		|| !TestNotNull(TEXT("the copied component retained its inline tween"), CopyPlayer->GetPlayTween())) return false;
	TestNotEqual(TEXT("the component was independently instanced"), CopyPlayer, SourcePlayer);
	TestNotEqual(TEXT("the inline tween was independently instanced"), CopyPlayer->GetPlayTween(), static_cast<UDreamUIPlayTween*>(SourceTween));
	TestNotEqual(TEXT("the receiver was independently instanced"), CopyReceiver, SourceReceiver);
	TestTrue(TEXT("the copied inline tween belongs to its copied component"), CopyPlayer->GetPlayTween()->IsIn(CopyPlayer));

	// A bare world has no game-instance tween manager. The production Start fallback completes
	// synchronously, so this tests the actual public Play -> OnComplete -> authored route end to end.
	int32 Completions = 0;
	CopyPlayer->GetPlayTween()->OnCompleteCPP.AddLambda([&Completions] { ++Completions; });
	CopyPlayer->Play();
	CopyPlayer->GetPlayTween()->OnCompleteCPP.Clear();
	TestNull(TEXT("the no-manager playback completed synchronously"), CopyPlayer->GetPlayTween()->GetTweener());
	TestEqual(TEXT("the copied tween completed one real run"), Completions, 1);
	TestEqual(TEXT("completion calls the copied tree's receiver once"), CopyReceiver->TouchCount, 1);
	TestEqual(TEXT("completion never calls the source tree's receiver"), SourceReceiver->TouchCount, 0);
	CopyPlayer->Stop();
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamInstancedSequenceEventReferenceTest,
	"DreamGUI.WidgetTree.InstancedSubobjects.ACopiedSequenceKeepsLocalEventsAndItsSharedCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamInstancedSequenceEventReferenceTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("DuplicateSubtree"));
	OutTestCommands.Add(TEXT("Duplicate"));
	OutBeautifiedNames.Add(TEXT("Generated widget instance"));
	OutTestCommands.Add(TEXT("Instance"));
}

bool FDreamInstancedSequenceEventReferenceTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamWidgetTree> SourceTree(NewObject<UDreamWidgetTree>(GetTransientPackage()));
	UDreamWidget* Root = SourceTree->ConstructWidget<UDreamWidget>();
	Root->SetDisplayName(TEXT("Root"));
	SourceTree->RootWidget = Root;
	UDreamWidget* Receiver = SourceTree->ConstructWidget<UDreamWidget>();
	Receiver->SetDisplayName(TEXT("Receiver"));
	Receiver->SetParentBeforeRegister(Root);
	UDreamEventBindingTestBehaviour* SourceReceiver = Receiver->AddComponent<UDreamEventBindingTestBehaviour>();
	UDreamUIPlayTweenSequenceComponent* SourceSequence = Root->AddComponent<UDreamUIPlayTweenSequenceComponent>();
	if (!TestNotNull(TEXT("the source has an event receiver"), SourceReceiver)
		|| !TestNotNull(TEXT("the source has the native sequence component"), SourceSequence)) return false;
	TStrongObjectPtr<UCurveFloat> SharedCurve(NewObject<UCurveFloat>(GetTransientPackage()));
	SharedCurve->FloatCurve.AddKey(0.0f, 0.0f);
	SharedCurve->FloatCurve.AddKey(1.0f, 0.75f);

	const FArrayProperty* TweensProperty = FindFProperty<FArrayProperty>(UDreamUIPlayTweenSequenceComponent::StaticClass(), TEXT("PlayTweenArray"));
	const FStructProperty* CompletionProperty = FindFProperty<FStructProperty>(UDreamUIPlayTween::StaticClass(), TEXT("OnComplete"));
	if (!TestNotNull(TEXT("the native sequence exposes its instanced array"), TweensProperty)
		|| !TestNotNull(TEXT("a native inline tween exposes its completion event"), CompletionProperty)) return false;
	const FObjectPropertyBase* TweenElement = CastField<FObjectPropertyBase>(TweensProperty->Inner);
	if (!TestNotNull(TEXT("the native sequence array contains UObject entries"), TweenElement)) return false;
	FScriptArrayHelper SourceTweens(TweensProperty, TweensProperty->ContainerPtrToValuePtr<void>(SourceSequence));
	TArray<UDreamUIPlayTween_Float*> AuthoredTweens;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UDreamUIPlayTween_Float* Tween = NewObject<UDreamUIPlayTween_Float>(SourceSequence);
		Tween->SetEaseType(EDreamTweenEase::CurveFloat);
		Tween->SetEaseCurve(SharedCurve.Get());
		CompletionProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Tween)->AddFunctionBinding(
			Receiver, SourceReceiver, TEXT("Touch"), EDreamUIEventDelegateParameterType::Empty, false);
		TweenElement->SetObjectPropertyValue(SourceTweens.GetRawPtr(SourceTweens.AddValue()), Tween);
		AuthoredTweens.Add(Tween);
	}

	TStrongObjectPtr<UDreamWidgetTree> DuplicatedTree;
	TStrongObjectPtr<UDreamUserWidget> Instance;
	UDreamWidgetTree* CopyTree = nullptr;
	if (Parameters == TEXT("Duplicate"))
	{
		DuplicatedTree.Reset(NewObject<UDreamWidgetTree>(TestWorld.World));
		DuplicatedTree->RootWidget = UDreamWidget::DuplicateSubtree(DuplicatedTree.Get(), Root);
		CopyTree = DuplicatedTree.Get();
	}
	else
	{
		Instance.Reset(NewObject<UDreamUserWidget>(TestWorld.World));
		UDreamWidgetGeneratedClass::InitializeWidgetStatic(Instance.Get(), Instance->GetClass(), SourceTree.Get());
		CopyTree = Instance->GetWidgetTree();
	}
	UDreamWidget* CopyRoot = CopyTree != nullptr ? CopyTree->FindWidgetByVariableName(TEXT("Root")) : nullptr;
	UDreamWidget* CopyReceiverWidget = CopyTree != nullptr ? CopyTree->FindWidgetByVariableName(TEXT("Receiver")) : nullptr;
	UDreamUIPlayTweenSequenceComponent* CopySequence = CopyRoot != nullptr
		? CopyRoot->GetComponent<UDreamUIPlayTweenSequenceComponent>() : nullptr;
	UDreamEventBindingTestBehaviour* CopyReceiver = CopyReceiverWidget != nullptr
		? CopyReceiverWidget->GetComponent<UDreamEventBindingTestBehaviour>() : nullptr;
	if (!TestNotNull(TEXT("the copied tree has its sequence"), CopySequence)
		|| !TestNotNull(TEXT("the copied tree has its receiver"), CopyReceiver)) return false;
	FScriptArrayHelper CopyTweens(TweensProperty, TweensProperty->ContainerPtrToValuePtr<void>(CopySequence));
	if (!TestEqual(TEXT("the entire inline sequence was copied"), CopyTweens.Num(), AuthoredTweens.Num())) return false;
	for (int32 Index = 0; Index < CopyTweens.Num(); ++Index)
	{
		UDreamUIPlayTween* Tween = Cast<UDreamUIPlayTween>(TweenElement->GetObjectPropertyValue(CopyTweens.GetRawPtr(Index)));
		if (!TestNotNull(TEXT("each copied inline entry is a real play tween"), Tween)) return false;
		TestNotEqual(TEXT("each inline entry is independent of its source"), Tween, static_cast<UDreamUIPlayTween*>(AuthoredTweens[Index]));
		TestTrue(TEXT("each inline entry belongs to the new component"), Tween->IsIn(CopySequence));
		TestEqual(TEXT("an external easing curve retains its shared identity"), Tween->GetEaseCurve(), SharedCurve.Get());
	}
	int32 Completions = 0;
	CopySequence->OnCompleteCPP.AddLambda([&Completions] { ++Completions; });
	CopySequence->Play();
	CopySequence->OnCompleteCPP.Clear();
	TestEqual(TEXT("the real sequence ran both entries and completed once"), Completions, 1);
	TestEqual(TEXT("each inline completion reached this copy's receiver"), CopyReceiver->TouchCount, 2);
	TestEqual(TEXT("no inline completion reached the source receiver"), SourceReceiver->TouchCount, 0);
	TestEqual(TEXT("the shared curve was not rewritten by duplication or playback"), SharedCurve->GetFloatValue(1.0f), 0.75f);
	for (UDreamUIPlayTween_Float* Tween : AuthoredTweens)
	{
		TestEqual(TEXT("the source also retains its original external curve"), Tween->GetEaseCurve(), SharedCurve.Get());
	}
	CopySequence->Stop();
	return true;
}
#endif
