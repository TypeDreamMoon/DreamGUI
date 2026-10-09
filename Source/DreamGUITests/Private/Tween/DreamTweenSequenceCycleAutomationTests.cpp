// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamTweenManager.h"
#include "DreamTweenerSequence.h"
#include "DreamScopedGameInstanceWorld.h"
#include "Tweener/DreamTweenerFloat.h"
#include "UObject/UnrealType.h"

namespace DreamTweenSequenceCycleTestLocal
{
	int32 ChildCount(UDreamTweenerSequence* Sequence)
	{
		const FArrayProperty* Property = FindFProperty<FArrayProperty>(Sequence->GetClass(), TEXT("tweenerList"));
		return FScriptArrayHelper(Property, Property->ContainerPtrToValuePtr<void>(Sequence)).Num();
	}

	UDreamTweenerFloat* MakeChild(float& Value)
	{
		UDreamTweenerFloat* Child = NewObject<UDreamTweenerFloat>(GetTransientPackage());
		Child->SetInitialValue(FDreamTweenFloatGetterFunction::CreateLambda([&Value] { return Value; }),
			FDreamTweenFloatSetterFunction::CreateLambda([&Value](float NewValue) { Value = NewValue; }), 10.0f, 1.0f);
		Child->SetEase(EDreamTweenEase::Linear);
		return Child;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenSequenceSelfContainmentTest,
	"DreamGUI.Tween.Sequence.Cycles.SelfContainmentIsRejectedThroughEveryEntryPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceSelfContainmentTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenSequenceCycleTestLocal;
	DreamTests::FScopedGameInstanceWorld World;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager)) return false;
	for (int32 EntryPoint = 0; EntryPoint < 4; ++EntryPoint)
	{
		float Value = 0;
		UDreamTweenerSequence* Sequence = UDreamTweenManager::CreateSequence(World.World);
		Sequence->SetTickType(EDreamTweenTickType::Manual);
		Sequence->Append(nullptr, MakeChild(Value));
		const float Duration = Sequence->GetDuration();
		AddExpectedErrorPlain(TEXT("cyclic sequence"), EAutomationExpectedErrorFlags::Contains, 1);
		switch (EntryPoint)
		{
		case 0: Sequence->Insert(nullptr, 0.0f, Sequence); break;
		case 1: Sequence->Append(nullptr, Sequence); break;
		case 2: Sequence->Prepend(nullptr, Sequence); break;
		case 3: Sequence->Join(nullptr, Sequence); break;
		}
		TestEqual(TEXT("A rejected self-insertion leaves its children intact"), ChildCount(Sequence), 1);
		TestEqual(TEXT("A rejected self-insertion leaves its duration intact"), Sequence->GetDuration(), Duration);
		TestTrue(TEXT("Rejection does not remove the sequence from its manager"), Manager->IsTweening(Sequence));
		// Never tick a cycle on the unfixed implementation: admission itself proves the defect safely.
		Sequence->Kill();
		Manager->ManualTick(0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenSequenceIndirectContainmentTest,
	"DreamGUI.Tween.Sequence.Cycles.IndirectCyclesAreRejectedAndAcyclicNestingStillPlays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceIndirectContainmentTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenSequenceCycleTestLocal;
	for (bool bPrepend : { false, true })
	{
		float Value = 0;
		UDreamTweenerSequence* Outer = NewObject<UDreamTweenerSequence>(GetTransientPackage());
		UDreamTweenerSequence* Middle = NewObject<UDreamTweenerSequence>(GetTransientPackage());
		UDreamTweenerSequence* Inner = NewObject<UDreamTweenerSequence>(GetTransientPackage());
		Inner->Append(nullptr, MakeChild(Value));
		Middle->Append(nullptr, Inner);
		Outer->Append(nullptr, Middle);
		AddExpectedErrorPlain(TEXT("cyclic sequence"), EAutomationExpectedErrorFlags::Contains, 1);
		if (bPrepend) Inner->Prepend(nullptr, Outer);
		else Inner->Insert(nullptr, 0.0f, Outer);
		TestEqual(TEXT("An indirect cycle is rejected before adding the ancestor"), ChildCount(Inner), 1);
		TestEqual(TEXT("The inner sequence keeps its authored length"), Inner->GetDuration(), 1.0f);
		if (ChildCount(Inner) == 1)
		{
			Outer->ToNextWithElapsedTime(0.5f);
			TestEqual(TEXT("Acyclic nested sequences interpolate their leaf"), Value, 5.0f, 0.001f);
			Outer->ToNextWithElapsedTime(1.0f);
			TestEqual(TEXT("Acyclic nested sequences finish their leaf"), Value, 10.0f, 0.001f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenSequenceFinishedAncestorTest,
	"DreamGUI.Tween.Sequence.Cycles.AnAncestorInTheFinishedListCannotBeAddedAsAChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceFinishedAncestorTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenSequenceCycleTestLocal;
	float Value = 0;
	UDreamTweenerSequence* Child = NewObject<UDreamTweenerSequence>(GetTransientPackage());
	UDreamTweenerSequence* Parent = NewObject<UDreamTweenerSequence>(GetTransientPackage());
	Child->Append(nullptr, MakeChild(Value));
	Parent->Append(nullptr, Child);
	Parent->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("The parent's child moved off its active list"), ChildCount(Parent), 0);
	static_cast<UDreamTweener*>(Child)->Restart();
	AddExpectedErrorPlain(TEXT("cyclic sequence"), EAutomationExpectedErrorFlags::Contains, 1);
	Child->Insert(nullptr, 0.0f, Parent);
	TestEqual(TEXT("A parent still owning the child in its finished list is rejected"), ChildCount(Child), 1);
	Child->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("The rejected edit leaves the restarted child playable"), Value, 5.0f, 0.001f);
	return true;
}

#endif
