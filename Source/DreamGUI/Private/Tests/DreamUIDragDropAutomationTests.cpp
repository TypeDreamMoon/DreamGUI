// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Tests/DreamDragDropTestTypes.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDragCancelOutlivesTheSourceTest,
	"DreamGUI.DragDrop.ADragWhoseSourceDiesIsStillCancelledExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDragCancelOutlivesTheSourceTest::RunTest(const FString& Parameters)
{
	/*
	 * The cancel used to belong to the SOURCE: only UDreamUIDragSource::OnPointerEndDrag broadcast
	 * it, and the pointer pipeline ran that path only while the source widget was still valid. A
	 * source destroyed mid-drag -- a recycled inventory row, a screen closed under the finger -- took
	 * the cancel with it, so the handler that puts the item back in its slot simply never ran and the
	 * item stayed nowhere. The cancel now belongs to the OPERATION, which outlives the widget, and is
	 * latched so the two callers cannot deliver it twice.
	 */
	UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(GetTransientPackage());
	UDreamDragDropCallProbe* Probe = NewObject<UDreamDragDropCallProbe>(GetTransientPackage());
	Operation->OnDragCancelled.AddDynamic(Probe, &UDreamDragDropCallProbe::OnOperation);

	// What the pipeline now does for a drag whose source is already gone.
	Operation->NotifyDragCancelled();
	TestEqual(TEXT("a drag with no source left to ask is still cancelled"), Probe->CallCount, 1);
	TestEqual(TEXT("...and the cancel carries the operation that was being dragged"),
		Probe->LastOperation.Get(), Operation);

	// And the source, arriving late or not at all, cannot make it a second cancel.
	UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
	UDreamUIDragSource* Source = NewObject<UDreamUIDragSource>(Widget);
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());
	EventData->DragOperation = Operation;
	IDreamPointerDragInterface::Execute_OnPointerEndDrag(Source, EventData);
	TestEqual(TEXT("the source's own end-of-drag does not cancel it twice"), Probe->CallCount, 1);

	// A drop that landed is not a cancel, whichever side notices the drag ending.
	UDreamDragDropOperation* Handled = NewObject<UDreamDragDropOperation>(GetTransientPackage());
	UDreamDragDropCallProbe* HandledProbe = NewObject<UDreamDragDropCallProbe>(GetTransientPackage());
	Handled->OnDragCancelled.AddDynamic(HandledProbe, &UDreamDragDropCallProbe::OnOperation);
	Handled->bDropWasHandled = true;
	Handled->NotifyDragCancelled();
	TestEqual(TEXT("a drop that was accepted is never reported as a cancel"), HandledProbe->CallCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDropHoverResolutionTest,
	"DreamGUI.DragDrop.TheHoveredTargetIsTheNearestAncestorThatWouldAcceptTheDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDropHoverResolutionTest::RunTest(const FString& Parameters)
{
	/*
	 * Drop-hover feedback needs the same answer the drop itself computes, one frame earlier:
	 * CanAcceptDrop used to be asked once, at the moment the player let go, so nothing on screen
	 * could say in advance whether a slot would take what was being carried.
	 */
	UDreamWidget* Panel = NewObject<UDreamWidget>(GetTransientPackage());
	UDreamWidget* Slot = NewObject<UDreamWidget>(GetTransientPackage());
	UDreamWidget* Icon = NewObject<UDreamWidget>(GetTransientPackage());
	Slot->SetParentBeforeRegister(Panel);
	Icon->SetParentBeforeRegister(Slot);

	UDreamUIDropTarget* PanelTarget = Panel->AddComponent<UDreamUIDropTarget>();
	UDreamUIDropTarget* SlotTarget = Slot->AddComponent<UDreamUIDropTarget>();
	if (!TestNotNull(TEXT("the panel carries a drop target"), PanelTarget)
		|| !TestNotNull(TEXT("the slot carries a drop target"), SlotTarget))
	{
		return false;
	}

	UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(GetTransientPackage());
	Operation->Tag = TEXT("Item");

	// The pointer is over the icon, which is not a target at all; the innermost ancestor that is one
	// answers -- the same climb the event system's bubbling does on the drop.
	TestEqual(TEXT("hovering a non-target resolves to the innermost target above it"),
		DreamUIDragDropPolicy::ResolveDropTarget(Icon, Operation), SlotTarget);

	// A target that cannot take this payload is transparent, exactly as a refused drop is.
	SlotTarget->RequiredTag = TEXT("Skill");
	TestEqual(TEXT("a target that would refuse is skipped for the one around it"),
		DreamUIDragDropPolicy::ResolveDropTarget(Icon, Operation), PanelTarget);

	PanelTarget->RequiredTag = TEXT("Skill");
	TestNull(TEXT("nothing on the path would take it, so nothing is hovered"),
		DreamUIDragDropPolicy::ResolveDropTarget(Icon, Operation));
	TestNull(TEXT("a drag with no meaning hovers nothing either"),
		DreamUIDragDropPolicy::ResolveDropTarget(Icon, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDropHoverEdgesTest,
	"DreamGUI.DragDrop.HoverEntersAndLeavesOnceEachWhileOverFiresThroughout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDropHoverEdgesTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
	UDreamUIDropTarget* Target = Widget->AddComponent<UDreamUIDropTarget>();
	if (!TestNotNull(TEXT("the widget carries a drop target"), Target))
	{
		return false;
	}
	UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(GetTransientPackage());

	UDreamDragDropCallProbe* Entered = NewObject<UDreamDragDropCallProbe>(GetTransientPackage());
	UDreamDragDropCallProbe* Over = NewObject<UDreamDragDropCallProbe>(GetTransientPackage());
	UDreamDragDropCallProbe* Left = NewObject<UDreamDragDropCallProbe>(GetTransientPackage());
	Target->OnDragEnter.AddDynamic(Entered, &UDreamDragDropCallProbe::OnOperation);
	Target->OnDragOver.AddDynamic(Over, &UDreamDragDropCallProbe::OnOperation);
	Target->OnDragLeave.AddDynamic(Left, &UDreamDragDropCallProbe::OnOperation);

	// Over before enter is not a hover at all: the subsystem asks every frame, and a target the drag
	// has not reached must not light up because a frame went by.
	Target->NotifyDragOver(Operation);
	TestEqual(TEXT("Over on a target that was never entered fires nothing"), Over->CallCount, 0);
	TestFalse(TEXT("...and leaves it unhovered"), Target->IsDragHovered());

	Target->NotifyDragEnter(Operation);
	Target->NotifyDragOver(Operation);
	Target->NotifyDragOver(Operation);
	TestEqual(TEXT("entering fires once"), Entered->CallCount, 1);
	TestEqual(TEXT("...and every frame after it fires Over"), Over->CallCount, 2);
	TestTrue(TEXT("...with the target reporting itself hovered"), Target->IsDragHovered());

	// A second enter is the subsystem re-asserting the same hover, not a new one.
	Target->NotifyDragEnter(Operation);
	TestEqual(TEXT("re-entering a target already hovered fires nothing"), Entered->CallCount, 1);

	Target->NotifyDragLeave(Operation);
	TestEqual(TEXT("leaving fires once"), Left->CallCount, 1);
	TestFalse(TEXT("...and the target is no longer hovered"), Target->IsDragHovered());
	Target->NotifyDragLeave(Operation);
	TestEqual(TEXT("leaving again fires nothing, so a drop and a leave cannot double up"), Left->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDragPerPointerTest,
	"DreamGUI.DragDrop.TwoFingersDraggingAtOnceKeepTheirOwnVisualAndTheirOwnHover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDragPerPointerTest::RunTest(const FString& Parameters)
{
	/*
	 * There used to be one of everything -- one followed pointer, one operation, one drag visual, one
	 * hovered target -- shared by whichever pointer sent the last event. On a touch screen that meant a
	 * second finger moving anywhere at all dragged the FIRST finger's visual to it and lit up whatever
	 * the second finger was over as the first one's drop target. Two fingers rearranging an inventory
	 * is not exotic on a phone; it is how an inventory gets rearranged.
	 */
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	} TestWorld;

	UDreamUIDragDropSubsystem* DragDrop = TestWorld.World->GetSubsystem<UDreamUIDragDropSubsystem>();
	if (!TestNotNull(TEXT("the drag-drop subsystem exists in a game world"), DragDrop))
	{
		return false;
	}
	// The subsystem follows drags by listening to the event system, so there has to be one. Ticking
	// once is what makes it subscribe -- the subscription is deliberately lazy, because the event
	// system is usually spawned after the subsystem.
	ADreamStandaloneInputEventSystemActor* EventActor =
		TestWorld.World->SpawnActor<ADreamStandaloneInputEventSystemActor>();
	UDreamEventSystem* EventSystem = EventActor != nullptr ? EventActor->GetEventSystem() : nullptr;
	if (!TestNotNull(TEXT("an event system to broadcast through"), EventSystem))
	{
		return false;
	}
	// Registered by hand rather than by BeginPlay: a bare test world has not begun play, and driving
	// the actor through BeginPlay would have it warn about the InputComponent no PlayerController
	// created for it. The subsystem only needs to be able to FIND an event system for user 0.
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("...and a manager it can register with"), Manager))
	{
		return false;
	}
	Manager->AddEventSystem(EventSystem);
	TestEqual(TEXT("the event system is the one for user 0"),
		UDreamEventSystem::GetDreamEventSystemInstance(TestWorld.World, 0), EventSystem);
	DragDrop->Tick(0.0f);

	// Two slots that accept different tags, so "which target is lit" is a question with two answers.
	UDreamWidget* SlotA = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamWidget* SlotB = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamUIDropTarget* TargetA = SlotA->AddComponent<UDreamUIDropTarget>();
	UDreamUIDropTarget* TargetB = SlotB->AddComponent<UDreamUIDropTarget>();
	if (!TestNotNull(TEXT("slot A is a drop target"), TargetA) || !TestNotNull(TEXT("slot B is one too"), TargetB))
	{
		return false;
	}

	auto MakeDrag = [&](int32 PointerID, UDreamWidget* EnterWidget) -> UDreamPointerEventData*
	{
		UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(TestWorld.World);
		EventData->PointerID = PointerID;
		EventData->bIsDragging = true;
		EventData->EnterWidget = EnterWidget;
		EventData->EventType = EDreamUIPointerEventType::BeginDrag;
		UDreamDragDropOperation* Operation = NewObject<UDreamDragDropOperation>(TestWorld.World);
		Operation->Tag = FName(*FString::Printf(TEXT("Pointer%d"), PointerID));
		EventData->DragOperation = Operation;
		return EventData;
	};

	UDreamPointerEventData* FirstFinger = MakeDrag(0, SlotA);
	UDreamPointerEventData* SecondFinger = MakeDrag(1, SlotB);

	EventSystem->CallOnPointerBeginDrag(SlotA, FirstFinger);
	TestEqual(TEXT("one drag is being followed"), DragDrop->GetDragCount(), 1);

	EventSystem->CallOnPointerBeginDrag(SlotB, SecondFinger);
	TestEqual(TEXT("the second finger's drag is followed as well, not instead"), DragDrop->GetDragCount(), 2);

	// Each pointer carries its own operation. Before this, the second BeginDrag replaced the first.
	TestEqual(TEXT("the first finger still carries its own operation"),
		DragDrop->GetDragOperationForPointer(0), FirstFinger->DragOperation.Get());
	TestEqual(TEXT("...and the second carries a different one"),
		DragDrop->GetDragOperationForPointer(1), SecondFinger->DragOperation.Get());

	// And its own hover. This is the assertion the shared state could never pass: both fingers are
	// over different slots at the same time.
	TestEqual(TEXT("the first finger hovers the slot it is over"),
		DragDrop->GetHoveredTargetForPointer(0), TargetA);
	TestEqual(TEXT("the second hovers the slot IT is over"),
		DragDrop->GetHoveredTargetForPointer(1), TargetB);
	TestTrue(TEXT("both targets are lit at once"), TargetA->IsDragHovered() && TargetB->IsDragHovered());

	// The second finger moving must not touch the first finger's bookkeeping -- the exact shape of
	// the defect, spelled as one event.
	SecondFinger->EventType = EDreamUIPointerEventType::Drag;
	SecondFinger->EnterWidget = SlotA;
	EventSystem->CallOnPointerDrag(SlotA, SecondFinger);
	TestEqual(TEXT("the moving finger's hover follows it"),
		DragDrop->GetHoveredTargetForPointer(1), TargetA);
	TestEqual(TEXT("...and the other finger's hover is untouched"),
		DragDrop->GetHoveredTargetForPointer(0), TargetA);
	TestEqual(TEXT("...with both drags still being followed"), DragDrop->GetDragCount(), 2);

	// One finger lifting ends one drag, not both.
	FirstFinger->EventType = EDreamUIPointerEventType::EndDrag;
	FirstFinger->bIsDragging = false;
	EventSystem->CallOnPointerEndDrag(SlotA, FirstFinger);
	TestEqual(TEXT("one finger lifting leaves the other drag alone"), DragDrop->GetDragCount(), 1);
	TestNull(TEXT("...and the lifted finger carries nothing"), DragDrop->GetDragOperationForPointer(0));
	TestEqual(TEXT("...while the other still does"),
		DragDrop->GetDragOperationForPointer(1), SecondFinger->DragOperation.Get());

	SlotA->DestroyWidget();
	SlotB->DestroyWidget();
	return true;
}

#endif
