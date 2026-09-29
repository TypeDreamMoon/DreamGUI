// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamEditableText.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * THE FIELD THAT DRAWS NO BOX -- UMG's EditableText, beside the boxed EditableTextBox.
 *
 * It differs from the boxed field in exactly one push: its face is made fully transparent in every
 * state. That is the whole risk worth testing. A face nobody can see is still the part the text
 * input behaviour lives on and the part the pointer is supposed to land on; if "invisible" ever
 * came to mean "not hit", the borderless field would be a label that cannot be typed into. So each
 * test here is the boxed field's interaction, done to the borderless one.
 */
namespace DreamEditableTextInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamEditableText* MakeObservedField(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener,
		const FVector2D& InPosition = FVector2D::ZeroVector)
	{
		UDreamEditableText* Field = InRig.MakeControl<UDreamEditableText>(TEXT("Borderless"), nullptr, FVector2D(320.0, 40.0), InPosition);
		if (Field != nullptr && InListener != nullptr)
		{
			Field->OnTextChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextChanged);
			Field->OnTextCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextCommitted);
		}
		return Field;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEditableTextTypeTest,
	"DreamGUI.EditableText.ClickingTheInvisibleFaceAndTypingEntersTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEditableTextTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamEditableText* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking in and typing completes"),
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Borderless")))->Type(TEXT("hello")));
	TestEqual(TEXT("The borderless field holds what was typed"), Field->GetText(), FString(TEXT("hello")));
	TestEqual(TEXT("And announced each character"), Listener->TextChangedCount, 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEditableTextEnterTest,
	"DreamGUI.EditableText.EnterCommitsTheBorderlessFieldOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEditableTextEnterTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamEditableText* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Borderless")));
	FieldElement->Type(TEXT("ok"));
	FieldElement->Type(EKeys::Enter);

	TestEqual(TEXT("Enter committed the borderless field once"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("With what was typed"), Listener->LastCommittedText, FString(TEXT("ok")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEditableTextClickAwayTest,
	"DreamGUI.EditableText.ClickingSomewhereElseCommitsTheBorderlessField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEditableTextClickAwayTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamEditableText* Field = MakeObservedField(Rig, Listener.Get(), FVector2D(0.0, 120.0));
	UDreamWidget* Elsewhere = Rig.MakeWidget(TEXT("Elsewhere"), nullptr, FVector2D(300.0, 120.0), FVector2D(0.0, -150.0));
	if (!TestTrue(TEXT("The rig, the field and the other widget came up"), Rig.IsUsable() && Field != nullptr && Elsewhere != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Borderless")))->Type(TEXT("hi"));
	Rig.Driver()->Find(FDreamBy::Name(TEXT("Elsewhere")))->Click();

	// UMG: focus moved by the pointer commits once, with ETextCommit::OnUserMovedFocus.
	TestEqual(TEXT("Moving away committed the borderless field once"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("With what had been typed"), Listener->LastCommittedText, FString(TEXT("hi")));
	TestFalse(TEXT("And it is no longer being edited"),
		Field->InputBehaviour != nullptr && Field->InputBehaviour->IsInputActive());

	return true;
}

#endif
