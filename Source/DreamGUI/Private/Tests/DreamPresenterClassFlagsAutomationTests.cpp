// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamWidgetPresenterComponentBase.h"
#include "PrefabSystem/DreamUIPrefabPresenterComponent.h"
#include "XMLSupport/DreamUIMLPresenterComponent.h"
#include "UObject/UObjectIterator.h"

/*
 * UDreamWidgetPresenterComponentBase must stay Abstract.
 *
 * Its LoadWidget is PURE_VIRTUAL, which is not `= 0` -- the macro expands to a body that calls
 * LowLevelFatalError. So the class is concrete as far as C++ is concerned, and without CLASS_Abstract
 * the Add Component list offers it like any other component. Picking it there is a hard crash on the
 * next register, not a message. That is what this pins.
 *
 * It names its class rather than sweeping every spawnable component because PURE_VIRTUAL leaves no
 * reflection signal to sweep for. A new presenter base wants its own line here.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresenterBaseIsAbstractTest,
	"DreamGUI.Presenter.ClassFlags.BaseIsAbstract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPresenterBaseIsAbstractTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("presenter base is Abstract, so Add Component cannot offer it"),
		UDreamWidgetPresenterComponentBase::StaticClass()->HasAnyClassFlags(CLASS_Abstract));

	// The point of hiding the base is that the usable ones stay usable.
	TestFalse(TEXT("prefab presenter is spawnable"),
		UDreamUIPrefabPresenterComponent::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestFalse(TEXT("markup presenter is spawnable"),
		UDreamUIMLPresenterComponent::StaticClass()->HasAnyClassFlags(CLASS_Abstract));

	return true;
}

/*
 * The concrete presenters are exactly the two that implement LoadWidget.
 *
 * Hiding the base does nothing for a third native subclass that forgets the override: it inherits the
 * PURE_VIRTUAL body and crashes the same way, under its own name. Whether a virtual was overridden is
 * not a question C++ can be asked at runtime -- a pointer-to-member of a virtual is a vtable-slot
 * thunk, equal for base and derived alike -- so this pins the roster instead. Adding a presenter
 * fails here, which is the moment to check it implements LoadWidget.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPresenterConcreteRosterTest,
	"DreamGUI.Presenter.ClassFlags.ConcreteRosterIsKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPresenterConcreteRosterTest::RunTest(const FString& Parameters)
{
	// Blueprint subclasses cannot override a plain virtual, so they are not the risk and not counted.
	TArray<FString> Spawnable;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (Candidate->IsChildOf(UDreamWidgetPresenterComponentBase::StaticClass())
			&& Candidate->IsNative()
			&& !Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			Spawnable.Add(Candidate->GetName());
		}
	}
	Spawnable.Sort();

	const TArray<FString> Expected = {
		TEXT("DreamUIMLPresenterComponent"),
		TEXT("DreamUIPrefabPresenterComponent"),
	};
	TestEqual(FString::Printf(TEXT("spawnable presenters, got [%s]"), *FString::Join(Spawnable, TEXT(", "))),
		Spawnable, Expected);

	return true;
}

#endif
