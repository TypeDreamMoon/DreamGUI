// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DetailCustomization/Widget/ComponentTransformDetails.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"

// The transform panel composes one vector per keystroke, out of SelectedObjects[0]'s current values with
// the edited axis substituted, and then hands that whole vector to OnSetTransform. OnSetTransform used to
// write it to every selected widget verbatim, so editing Scale X across a selection also replaced each
// widget's Y and Z with the first widget's -- and for a parented widget the replacement is serialized,
// because SetRelativeLocation/SetRelativeScale run CalculateAnchorFromTransform. These tests pin the
// per-object recomposition: only the axes the user actually edited may come from the incoming vector.
struct FDreamTransformDetailsTestAccess
{
	static void SetTransform(FComponentTransformDetails& Details, ETransformField::Type TransformField, EAxisList::Type Axis, const FVector& NewValue)
	{
		Details.OnSetTransform(TransformField, Axis, NewValue, true);
	}

	static FVector FilterAxes(FComponentTransformDetails& Details, EAxisList::Type Axis, const FVector& NewValue, const FVector& OldValue)
	{
		return Details.GetAxisFilteredVector(Axis, NewValue, OldValue);
	}

	/** Read from the editor ini in the constructor, so a user with it checked would otherwise see different numbers. */
	static void SetPreserveScaleRatio(FComponentTransformDetails& Details, bool bValue)
	{
		Details.bPreserveScaleRatio = bValue;
	}
};

namespace DreamTransformDetailsTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, const TCHAR* Name)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		return Widget;
	}

	/** First is the archetype the panel reads its displayed values from, which is the whole point of the ordering. */
	TSharedRef<FComponentTransformDetails> MakeDetails(UDreamWidget* First, UDreamWidget* Second)
	{
		TArray<TWeakObjectPtr<UDreamWidget>> Selection;
		Selection.Add(First);
		Selection.Add(Second);
		TSharedRef<FComponentTransformDetails> Details = MakeShared<FComponentTransformDetails>(Selection, FSelectedActorInfo(), (FNotifyHook*)nullptr);
		FDreamTransformDetailsTestAccess::SetPreserveScaleRatio(*Details, false);
		return Details;
	}

	FVector EulerOf(const UDreamWidget* Widget)
	{
		return Widget->GetRelativeRotation().Euler();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTransformDetailsScaleKeepsOtherAxesTest,
	"DreamGUI.Editor.TransformDetails.MultiSelectScaleKeepsEachWidgetsOtherAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTransformDetailsScaleKeepsOtherAxesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTransformDetailsTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* First = MakeWidget(TestWorld.World, TEXT("First"));
	UDreamWidget* Second = MakeWidget(TestWorld.World, TEXT("Second"));
	First->SetRelativeScale(FVector(1.0, 1.0, 1.0));
	Second->SetRelativeScale(FVector(2.0, 2.0, 2.0));

	TSharedRef<FComponentTransformDetails> Details = MakeDetails(First, Second);

	// Exactly what typing 3 into Scale X sends: OnSetTransformAxis fills the untouched axes from the
	// first selected widget, because it has no other object to read them from.
	FDreamTransformDetailsTestAccess::SetTransform(*Details, ETransformField::Scale, EAxisList::X, FVector(3.0, 1.0, 1.0));

	TestEqual(TEXT("the first widget takes the typed X"), First->GetRelativeScale(), FVector(3.0, 1.0, 1.0));
	TestEqual(TEXT("the second widget keeps its own Y and Z"), Second->GetRelativeScale(), FVector(3.0, 2.0, 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTransformDetailsLocationKeepsOtherAxesTest,
	"DreamGUI.Editor.TransformDetails.MultiSelectLocationKeepsEachWidgetsOtherAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTransformDetailsLocationKeepsOtherAxesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTransformDetailsTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* First = MakeWidget(TestWorld.World, TEXT("First"));
	UDreamWidget* Second = MakeWidget(TestWorld.World, TEXT("Second"));
	First->SetRelativeLocation(FVector(0.0, 0.0, 0.0));
	Second->SetRelativeLocation(FVector(0.0, 50.0, 80.0));

	TSharedRef<FComponentTransformDetails> Details = MakeDetails(First, Second);

	FDreamTransformDetailsTestAccess::SetTransform(*Details, ETransformField::Location, EAxisList::Y, FVector(0.0, 10.0, 0.0));

	TestEqual(TEXT("the first widget takes the typed Y"), First->GetRelativeLocation(), FVector(0.0, 10.0, 0.0));
	TestEqual(TEXT("the second widget is not dragged to the first widget's Z"), Second->GetRelativeLocation(), FVector(0.0, 10.0, 80.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTransformDetailsRotationKeepsOtherAxesTest,
	"DreamGUI.Editor.TransformDetails.MultiSelectRotationKeepsEachWidgetsOtherAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTransformDetailsRotationKeepsOtherAxesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTransformDetailsTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* First = MakeWidget(TestWorld.World, TEXT("First"));
	UDreamWidget* Second = MakeWidget(TestWorld.World, TEXT("Second"));
	First->SetRelativeRotation(FRotator(0.0, 0.0, 0.0).Quaternion());
	Second->SetRelativeRotation(FRotator(0.0, 45.0, 0.0).Quaternion());

	TSharedRef<FComponentTransformDetails> Details = MakeDetails(First, Second);

	// Euler order here is (Roll, Pitch, Yaw), which is what OnSetTransform feeds FRotator::MakeFromEuler.
	FDreamTransformDetailsTestAccess::SetTransform(*Details, ETransformField::Rotation, EAxisList::X, FVector(30.0, 0.0, 0.0));

	TestEqual(TEXT("the first widget takes the typed roll"), EulerOf(First), FVector(30.0, 0.0, 0.0), 0.1f);
	TestEqual(TEXT("the second widget keeps its own yaw"), EulerOf(Second), FVector(30.0, 0.0, 45.0), 0.1f);
	return true;
}

// A guard, not a detector. With EAxisList::All the filter is the identity on its second argument,
// so the SetTransform half of this passes with or without the fix. It exists to catch the opposite
// mistake -- a fix written with a hard-coded narrower mask, which would break paste and the three
// reset handlers. The argument-contract assertions above it do discriminate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTransformDetailsAllAxesStillWritesWholeVectorTest,
	"DreamGUI.Editor.TransformDetails.PasteAndResetStillWriteEveryAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTransformDetailsAllAxesStillWritesWholeVectorTest::RunTest(const FString& Parameters)
{
	using namespace DreamTransformDetailsTestLocal;
	FScopedTestWorld TestWorld;

	UDreamWidget* First = MakeWidget(TestWorld.World, TEXT("First"));
	UDreamWidget* Second = MakeWidget(TestWorld.World, TEXT("Second"));
	First->SetRelativeScale(FVector(1.0, 1.0, 1.0));
	Second->SetRelativeScale(FVector(2.0, 2.0, 2.0));

	TSharedRef<FComponentTransformDetails> Details = MakeDetails(First, Second);

	// Which argument each axis comes from is the whole contract of the filter, and paste/reset depend
	// on EAxisList::All meaning "all of them are edited".
	TestEqual(TEXT("unedited axes come from the old value"),
		FDreamTransformDetailsTestAccess::FilterAxes(*Details, EAxisList::X, FVector(3.0, 1.0, 1.0), FVector(2.0, 2.0, 2.0)),
		FVector(3.0, 2.0, 2.0));
	TestEqual(TEXT("all axes edited means nothing is kept"),
		FDreamTransformDetailsTestAccess::FilterAxes(*Details, EAxisList::All, FVector(5.0, 6.0, 7.0), FVector(2.0, 2.0, 2.0)),
		FVector(5.0, 6.0, 7.0));

	// Paste and the reset buttons go through the same entry point with every axis flagged, so the
	// per-object filter must not hold any of their values back.
	FDreamTransformDetailsTestAccess::SetTransform(*Details, ETransformField::Scale, EAxisList::All, FVector(5.0, 6.0, 7.0));

	TestEqual(TEXT("the first widget takes the whole pasted vector"), First->GetRelativeScale(), FVector(5.0, 6.0, 7.0));
	TestEqual(TEXT("so does the second"), Second->GetRelativeScale(), FVector(5.0, 6.0, 7.0));
	return true;
}

#endif
