// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"

/*
 * The drag-drop framework's decisions: what a source writes onto the drag, what a target accepts,
 * and what a refused drop does. All exercised through the interface entry points the event system
 * itself calls, with hand-made event data -- no world, no pointer pipeline.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDropTargetFilterTest,
	"DreamGUI.DragDrop.TargetFiltersByTagAndPayloadClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDropTargetFilterTest::RunTest(const FString& Parameters)
{
	UDreamUIDropTarget* Target = NewObject<UDreamUIDropTarget>(GetTransientPackage());
	UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(GetTransientPackage());

	TestFalse(TEXT("No operation is never acceptable"), Target->CanAcceptDrop(nullptr));
	TestTrue(TEXT("An unconstrained target takes anything"), Target->CanAcceptDrop(Operation));

	Target->RequiredTag = TEXT("Item");
	TestFalse(TEXT("A tag requirement refuses the untagged"), Target->CanAcceptDrop(Operation));
	Operation->Tag = TEXT("Item");
	TestTrue(TEXT("...and takes the matching tag"), Target->CanAcceptDrop(Operation));

	Target->RequiredPayloadClass = UDreamWidget::StaticClass();
	TestFalse(TEXT("A class requirement refuses a missing payload"), Target->CanAcceptDrop(Operation));
	Operation->Payload = NewObject<UDreamWidget>(GetTransientPackage());
	TestTrue(TEXT("...and takes a payload of that class"), Target->CanAcceptDrop(Operation));
	Target->RequiredPayloadClass = UDreamDragDropOperation::StaticClass();
	TestFalse(TEXT("...but not one of another class"), Target->CanAcceptDrop(Operation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDropRefusalBubblesTest,
	"DreamGUI.DragDrop.RefusedDropsBubbleAndAcceptedOnesStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDropRefusalBubblesTest::RunTest(const FString& Parameters)
{
	UDreamUIDropTarget* Target = NewObject<UDreamUIDropTarget>(GetTransientPackage());
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());
	UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(GetTransientPackage());

	// A drop with no operation is geometry, not meaning; the target is transparent to it.
	TestTrue(TEXT("A meaningless drop keeps bubbling"),
		IDreamPointerDragDropInterface::Execute_OnPointerDragDrop(Target, EventData));

	EventData->DragOperation = Operation;
	Target->RequiredTag = TEXT("Skill");
	Operation->Tag = TEXT("Item");
	TestTrue(TEXT("A refused drop keeps bubbling to the target around this one"),
		IDreamPointerDragDropInterface::Execute_OnPointerDragDrop(Target, EventData));
	TestFalse(TEXT("...and is not marked handled"), Operation->bDropWasHandled);

	Operation->Tag = TEXT("Skill");
	TestFalse(TEXT("An accepted drop stops bubbling"),
		IDreamPointerDragDropInterface::Execute_OnPointerDragDrop(Target, EventData));
	TestTrue(TEXT("...and is marked handled for the source's end-of-drag"), Operation->bDropWasHandled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDragSourceWritesMeaningTest,
	"DreamGUI.DragDrop.SourceWritesTheOperationOntoTheDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDragSourceWritesMeaningTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
	// A behaviour reaches its widget through its outer; making the widget the outer is exactly how
	// AddComponent creates one.
	UDreamUIDragSource* Source = NewObject<UDreamUIDragSource>(Widget);
	Source->Tag = TEXT("Item");
	Source->Payload = Widget;

	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());
	const bool bBubble = IDreamPointerDragInterface::Execute_OnPointerBeginDrag(Source, EventData);

	TestFalse(TEXT("A drag with meaning is consumed at the source by default"), bBubble);
	UDreamDragDropOperation* Operation = EventData->DragOperation.Get();
	if (!TestTrue(TEXT("BeginDrag wrote an operation onto the event data"), IsValid(Operation)))
	{
		return false;
	}
	TestEqual(TEXT("The tag came from the source"), Operation->Tag, FName(TEXT("Item")));
	TestEqual(TEXT("The payload came from the source"), Operation->Payload.Get(), Cast<UObject>(Widget));
	TestFalse(TEXT("A fresh operation is not yet handled"), Operation->bDropWasHandled);

	// Drag frames stay consumed while the operation rides; EndDrag with nothing handled is the
	// cancel path (its broadcast is a dynamic delegate, observed by the e2e pass rather than here).
	TestFalse(TEXT("Drag frames stay consumed"), IDreamPointerDragInterface::Execute_OnPointerDrag(Source, EventData));
	TestFalse(TEXT("EndDrag stays consumed"), IDreamPointerDragInterface::Execute_OnPointerEndDrag(Source, EventData));
	return true;
}

#endif
