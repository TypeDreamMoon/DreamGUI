// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "DreamDragDropTestTypes.h"
#include "DreamScopedWorld.h"
#include "DreamUIDragDropReentryTestTypes.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

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
	DreamTests::FScopedGameWorld TestWorld;

	UDreamUIDragDropSubsystem* DragDrop = TestWorld.World->GetSubsystem<UDreamUIDragDropSubsystem>();
	if (!TestNotNull(TEXT("the drag-drop subsystem exists in a game world"), DragDrop))
	{
		return false;
	}
	// The subsystem follows drags by listening to every player's events, which it does from the moment
	// the world has input. The event system is what the events are dispatched through here.
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
	UDreamUIInputSubsystem* InputSubsystem = UDreamUIInputSubsystem::Get(TestWorld.World);
	if (!TestNotNull(TEXT("...and an input subsystem it can register with"), InputSubsystem))
	{
		return false;
	}
	InputSubsystem->AddEventSystem(EventSystem);
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

	UDreamDragDropCallProbe* EnteredA = NewObject<UDreamDragDropCallProbe>(TestWorld.World);
	UDreamDragDropCallProbe* OverA = NewObject<UDreamDragDropCallProbe>(TestWorld.World);
	UDreamDragDropCallProbe* LeftA = NewObject<UDreamDragDropCallProbe>(TestWorld.World);
	TargetA->OnDragEnter.AddDynamic(EnteredA, &UDreamDragDropCallProbe::OnOperation);
	TargetA->OnDragOver.AddDynamic(OverA, &UDreamDragDropCallProbe::OnOperation);
	TargetA->OnDragLeave.AddDynamic(LeftA, &UDreamDragDropCallProbe::OnOperation);

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

	UDreamDragDropOperation* FirstOperation = FirstFinger->DragOperation.Get();
	UDreamDragDropOperation* SecondOperation = SecondFinger->DragOperation.Get();
	TestEqual(TEXT("the first operation entered A once"), EnteredA->CountFor(FirstOperation), 1);
	TestEqual(TEXT("the second operation gets its own enter on the shared target"), EnteredA->CountFor(SecondOperation), 1);
	TestTrue(TEXT("both operations receive over on the shared target"),
		OverA->CountFor(FirstOperation) > 0 && OverA->CountFor(SecondOperation) > 0);

	// Moving either finger away leaves only its membership, even while both drags keep running.
	FirstFinger->EventType = EDreamUIPointerEventType::Drag;
	FirstFinger->EnterWidget = SlotB;
	EventSystem->CallOnPointerDrag(SlotB, FirstFinger);
	TestEqual(TEXT("only the moving operation leaves A"), LeftA->CountFor(FirstOperation), 1);
	TestEqual(TEXT("the stationary operation has never left A"), LeftA->CountFor(SecondOperation), 0);
	TestTrue(TEXT("A stays hovered for the stationary operation"), TargetA->IsDragHovered());
	int32 SecondOverCount = OverA->CountFor(SecondOperation);
	DragDrop->Tick(0.0f);
	TestTrue(TEXT("the stationary operation still gets over after the other leaves"), OverA->CountFor(SecondOperation) > SecondOverCount);
	TestEqual(TEXT("its unchanged target does not need another enter"), EnteredA->CountFor(SecondOperation), 1);
	FirstFinger->EnterWidget = SlotA;
	EventSystem->CallOnPointerDrag(SlotA, FirstFinger);
	TestEqual(TEXT("the returning operation enters A again"), EnteredA->CountFor(FirstOperation), 2);

	// The cancellation path tells the operation, then dispatches EndDrag without a drop.
	FirstOperation->NotifyDragCancelled();
	FirstFinger->EventType = EDreamUIPointerEventType::EndDrag;
	FirstFinger->bIsDragging = false;
	EventSystem->CallOnPointerEndDrag(SlotA, FirstFinger);
	TestEqual(TEXT("one finger lifting leaves the other drag alone"), DragDrop->GetDragCount(), 1);
	TestNull(TEXT("...and the lifted finger carries nothing"), DragDrop->GetDragOperationForPointer(0));
	TestEqual(TEXT("...while the other still does"),
		DragDrop->GetDragOperationForPointer(1), SecondFinger->DragOperation.Get());
	TestEqual(TEXT("cancel ends only that operation's second hover"), LeftA->CountFor(FirstOperation), 2);
	TestTrue(TEXT("cancel leaves A hovered for the remaining finger"), TargetA->IsDragHovered());
	SecondOverCount = OverA->CountFor(SecondOperation);
	DragDrop->Tick(0.0f);
	TestTrue(TEXT("the remaining finger still receives over after cancellation"), OverA->CountFor(SecondOperation) > SecondOverCount);

	// A new operation on the first pointer can land without ending the second pointer's hover.
	UDreamPointerEventData* LandingFinger = MakeDrag(0, SlotA);
	UDreamDragDropOperation* LandingOperation = LandingFinger->DragOperation.Get();
	EventSystem->CallOnPointerBeginDrag(SlotA, LandingFinger);
	LandingFinger->bIsDragging = false;
	EventSystem->CallOnPointerDragDrop(SlotA, LandingFinger);
	LandingFinger->EventType = EDreamUIPointerEventType::EndDrag;
	EventSystem->CallOnPointerEndDrag(SlotA, LandingFinger);
	TestTrue(TEXT("the new operation lands"), LandingOperation->bDropWasHandled);
	TestEqual(TEXT("drop and EndDrag together leave it once"), LeftA->CountFor(LandingOperation), 1);
	TestTrue(TEXT("the other operation keeps A hovered after the drop"), TargetA->IsDragHovered());
	SecondOverCount = OverA->CountFor(SecondOperation);
	DragDrop->Tick(0.0f);
	TestTrue(TEXT("the other operation receives over after the drop"), OverA->CountFor(SecondOperation) > SecondOverCount);
	TestEqual(TEXT("it still has not received any leave"), LeftA->CountFor(SecondOperation), 0);

	SecondFinger->bIsDragging = false;
	EventSystem->CallOnPointerDragDrop(SlotA, SecondFinger);
	SecondFinger->EventType = EDreamUIPointerEventType::EndDrag;
	EventSystem->CallOnPointerEndDrag(SlotA, SecondFinger);
	TestTrue(TEXT("the last operation lands too"), SecondOperation->bDropWasHandled);
	TestEqual(TEXT("the last operation receives one leave"), LeftA->CountFor(SecondOperation), 1);
	TestFalse(TEXT("the final drop clears the aggregate hover"), TargetA->IsDragHovered());
	TestEqual(TEXT("neither drag is still followed"), DragDrop->GetDragCount(), 0);

	// Escape's cancellation also removes every membership, once per operation.
	UDreamPointerEventData* CancelledFirst = MakeDrag(0, SlotA);
	UDreamPointerEventData* CancelledSecond = MakeDrag(1, SlotA);
	EventSystem->CallOnPointerBeginDrag(SlotA, CancelledFirst);
	EventSystem->CallOnPointerBeginDrag(SlotA, CancelledSecond);
	TestTrue(TEXT("both operations light A before cancellation"), TargetA->IsDragHovered());
	TestTrue(TEXT("the subsystem cancels the shared-target drags"), DragDrop->CancelActiveDrag());
	TestFalse(TEXT("cancelling the last drags clears the aggregate hover"), TargetA->IsDragHovered());
	TestEqual(TEXT("cancel leaves the first operation once"), LeftA->CountFor(CancelledFirst->DragOperation.Get()), 1);
	TestEqual(TEXT("cancel leaves the second operation once"), LeftA->CountFor(CancelledSecond->DragOperation.Get()), 1);
	TestEqual(TEXT("no drags remain after cancellation"), DragDrop->GetDragCount(), 0);

	// Enter can synchronously cancel the entering finger, with another finger already on the target.
	UDreamPointerEventData* ResidentFinger = MakeDrag(0, SlotA);
	EventSystem->CallOnPointerBeginDrag(SlotA, ResidentFinger);
	UDreamPointerEventData* IncomingFinger = MakeDrag(1, SlotA);
	UDreamDragDropOperation* IncomingOperation = IncomingFinger->DragOperation.Get();
	UDreamDragDropReentryProbe* CancelsIncoming = NewObject<UDreamDragDropReentryProbe>(TestWorld.World);
	CancelsIncoming->Action = [EventSystem, IncomingFinger, SlotA]()
	{
		IncomingFinger->DragOperation->NotifyDragCancelled();
		IncomingFinger->bIsDragging = false;
		IncomingFinger->EventType = EDreamUIPointerEventType::EndDrag;
		EventSystem->CallOnPointerEndDrag(SlotA, IncomingFinger);
	};
	TargetA->OnDragEnter.AddDynamic(CancelsIncoming, &UDreamDragDropReentryProbe::OnOperation);
	EventSystem->CallOnPointerBeginDrag(SlotA, IncomingFinger);
	TestEqual(TEXT("the entering operation receives enter before its callback cancels it"), EnteredA->CountFor(IncomingOperation), 1);
	TestEqual(TEXT("the cancelled entering operation receives leave exactly once"), LeftA->CountFor(IncomingOperation), 1);
	TestEqual(TEXT("it receives no over after the enter callback cancels it"), OverA->CountFor(IncomingOperation), 0);
	TestTrue(TEXT("the resident operation keeps the target hovered"), TargetA->IsDragHovered());
	TestEqual(TEXT("only the resident drag is still followed"), DragDrop->GetDragCount(), 1);

	// Leave can destroy a target. The next target still receives this live drag, and a drop whose
	// leave destroys its own target must not continue through acceptance on that dead component.
	UDreamDragDropReentryProbe* DestroysA = NewObject<UDreamDragDropReentryProbe>(TestWorld.World);
	DestroysA->Action = [SlotA]() { SlotA->DestroyWidget(); };
	TargetA->OnDragLeave.AddDynamic(DestroysA, &UDreamDragDropReentryProbe::OnOperation);
	ResidentFinger->EventType = EDreamUIPointerEventType::Drag;
	ResidentFinger->EnterWidget = SlotB;
	EventSystem->CallOnPointerDrag(SlotB, ResidentFinger);
	TestFalse(TEXT("A was destroyed by its leave callback"), IsValid(TargetA));
	TestFalse(TEXT("a destroyed target has no aggregate hover"), TargetA->IsDragHovered());
	TestEqual(TEXT("the live operation continues to B"), DragDrop->GetHoveredTargetForPointer(0), TargetB);
	UDreamDragDropReentryProbe* DestroysB = NewObject<UDreamDragDropReentryProbe>(TestWorld.World);
	DestroysB->Action = [SlotB]() { SlotB->DestroyWidget(); };
	TargetB->OnDragLeave.AddDynamic(DestroysB, &UDreamDragDropReentryProbe::OnOperation);
	ResidentFinger->bIsDragging = false;
	EventSystem->CallOnPointerDragDrop(SlotB, ResidentFinger);
	TestFalse(TEXT("B was destroyed by the drop's leave callback"), IsValid(TargetB));
	TestFalse(TEXT("the destroyed target does not accept the operation afterward"), ResidentFinger->DragOperation->bDropWasHandled);
	// A destroyed drag source cannot receive EndDrag. Match the pointer input module's release
	// fallback: cancel the operation, clear it from the pointer, and let the next tick prune the drag.
	ResidentFinger->DragOperation->NotifyDragCancelled();
	ResidentFinger->DragOperation = nullptr;
	DragDrop->Tick(0.0f);
	TestEqual(TEXT("release clears the bookkeeping when the source and target were destroyed"), DragDrop->GetDragCount(), 0);

	UDreamWidget* SlotC = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamUIDropTarget* TargetC = SlotC->AddComponent<UDreamUIDropTarget>();
	if (!TestNotNull(TEXT("a new target can be destroyed from enter"), TargetC))return false;
	UDreamDragDropCallProbe* OverC = NewObject<UDreamDragDropCallProbe>(TestWorld.World);
	UDreamDragDropReentryProbe* DestroysC = NewObject<UDreamDragDropReentryProbe>(TestWorld.World);
	DestroysC->Action = [SlotC]() { SlotC->DestroyWidget(); };
	TargetC->OnDragEnter.AddDynamic(DestroysC, &UDreamDragDropReentryProbe::OnOperation);
	TargetC->OnDragOver.AddDynamic(OverC, &UDreamDragDropCallProbe::OnOperation);
	UDreamPointerEventData* LastFinger = MakeDrag(0, SlotC);
	EventSystem->CallOnPointerBeginDrag(SlotC, LastFinger);
	TestFalse(TEXT("enter can destroy its target"), IsValid(TargetC));
	TestNull(TEXT("the subsystem does not retain a destroyed hover target"), DragDrop->GetHoveredTargetForPointer(0));
	TestEqual(TEXT("the destroyed target receives no over after enter"), OverC->CallCount, 0);
	LastFinger->bIsDragging = false;
	LastFinger->DragOperation->NotifyDragCancelled();
	LastFinger->DragOperation = nullptr;
	DragDrop->Tick(0.0f);
	TestEqual(TEXT("release also clears an operation whose source was destroyed on enter"), DragDrop->GetDragCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDragHoverHandlerEndsADragTest,
	"DreamGUI.DragDrop.ATargetsHandlerEndingADragMidHoverLeavesNoTargetLitForADragNoLongerFollowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A drop target's enter and leave reach game code, and game code ends drags: the leave that cancels the drag, the enter
 * that ends another finger's. The hover was brought up to date by walking the map of followed drags, through a reference
 * into it, so a handler ending a drag changed the map under the walk and the reference wrote into an entry no longer
 * there -- and the target entered after its drag had ended stayed lit for good, with no leave ever to come. Drags are now
 * walked by key and looked up again after every handler, and one a handler ended goes no further. Checked with two
 * fingers' drags: a target's leave ending the other finger's drag in the middle of a tick, then a target's leave ending
 * the very drag that was leaving it.
 */
bool FDreamUIDragHoverHandlerEndsADragTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;

	UDreamUIDragDropSubsystem* DragDrop = TestWorld.World->GetSubsystem<UDreamUIDragDropSubsystem>();
	ADreamStandaloneInputEventSystemActor* EventActor =
		DragDrop != nullptr ? TestWorld.World->SpawnActor<ADreamStandaloneInputEventSystemActor>() : nullptr;
	UDreamEventSystem* EventSystem = EventActor != nullptr ? EventActor->GetEventSystem() : nullptr;
	UDreamUIInputSubsystem* InputSubsystem = UDreamUIInputSubsystem::Get(TestWorld.World);
	if (!TestTrue(TEXT("A drag-drop subsystem, and an event system its events come through"),
		DragDrop != nullptr && EventSystem != nullptr && InputSubsystem != nullptr))
	{
		return false;
	}
	// Registered by hand, as the two-finger test above does and for its reasons.
	InputSubsystem->AddEventSystem(EventSystem);
	DragDrop->Tick(0.0f);

	UDreamWidget* SlotA = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamWidget* SlotB = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamWidget* SlotC = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamUIDropTarget* TargetA = SlotA->AddComponent<UDreamUIDropTarget>();
	UDreamUIDropTarget* TargetB = SlotB->AddComponent<UDreamUIDropTarget>();
	UDreamUIDropTarget* TargetC = SlotC->AddComponent<UDreamUIDropTarget>();
	if (!TestTrue(TEXT("Three slots, each a drop target"), TargetA != nullptr && TargetB != nullptr && TargetC != nullptr))
	{
		return false;
	}
	auto MakeDrag = [&TestWorld](int32 InPointerID, UDreamWidget* InEnterWidget)
	{
		UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(TestWorld.World);
		EventData->PointerID = InPointerID;
		EventData->bIsDragging = true;
		EventData->EnterWidget = InEnterWidget;
		EventData->EventType = EDreamUIPointerEventType::BeginDrag;
		EventData->DragOperation = NewObject<UDreamDragDropOperation>(TestWorld.World);
		return EventData;
	};
	auto EndDragOf = [EventSystem](UDreamPointerEventData* InFinger, UDreamWidget* InSource)
	{
		InFinger->EventType = EDreamUIPointerEventType::EndDrag;
		InFinger->bIsDragging = false;
		EventSystem->CallOnPointerEndDrag(InSource, InFinger);
	};
	UDreamPointerEventData* FirstFinger = MakeDrag(0, SlotA);
	UDreamPointerEventData* SecondFinger = MakeDrag(1, SlotC);
	EventSystem->CallOnPointerBeginDrag(SlotA, FirstFinger);
	EventSystem->CallOnPointerBeginDrag(SlotC, SecondFinger);
	if (!TestEqual(TEXT("Two drags are followed"), DragDrop->GetDragCount(), 2)
		|| !TestTrue(TEXT("...each lighting the slot its finger is over"), TargetA->IsDragHovered() && TargetC->IsDragHovered()))
	{
		return false;
	}

	// The first finger has moved on to slot B, and slot A's leave ends the second finger's drag -- inside the tick.
	UDreamDragDropReentryProbe* EndsTheOther = NewObject<UDreamDragDropReentryProbe>(TestWorld.World);
	EndsTheOther->Action = [EndDragOf, SecondFinger, SlotC]() { EndDragOf(SecondFinger, SlotC); };
	TargetA->OnDragLeave.AddDynamic(EndsTheOther, &UDreamDragDropReentryProbe::OnOperation);
	FirstFinger->EnterWidget = SlotB;
	DragDrop->Tick(0.0f);
	TestEqual(TEXT("Slot A's leave ran its handler"), EndsTheOther->CallCount, 1);
	TestEqual(TEXT("...which ended the other finger's drag"), DragDrop->GetDragCount(), 1);
	TestFalse(TEXT("...leaving the slot that drag lit unlit"), TargetC->IsDragHovered());
	TestEqual(TEXT("The first finger's drag went on to light the slot it is over"), DragDrop->GetHoveredTargetForPointer(0), TargetB);
	TestTrue(TEXT("...lit"), TargetB->IsDragHovered());
	TestFalse(TEXT("...having left the one it was over"), TargetA->IsDragHovered());

	// The first finger moves back to slot A, and slot B's leave ends that very drag.
	UDreamDragDropReentryProbe* EndsItsOwn = NewObject<UDreamDragDropReentryProbe>(TestWorld.World);
	EndsItsOwn->Action = [EndDragOf, FirstFinger, SlotB]() { EndDragOf(FirstFinger, SlotB); };
	TargetB->OnDragLeave.AddDynamic(EndsItsOwn, &UDreamDragDropReentryProbe::OnOperation);
	FirstFinger->EventType = EDreamUIPointerEventType::Drag;
	FirstFinger->EnterWidget = SlotA;
	EventSystem->CallOnPointerDrag(SlotA, FirstFinger);
	TestEqual(TEXT("Slot B's leave ran its handler"), EndsItsOwn->CallCount, 1);
	TestEqual(TEXT("...which ended the drag leaving it"), DragDrop->GetDragCount(), 0);
	TestFalse(TEXT("The slot that drag was moving to is not lit for a drag no longer followed"), TargetA->IsDragHovered());
	TestFalse(TEXT("...and neither is the one it left"), TargetB->IsDragHovered());

	SlotA->DestroyWidget();
	SlotB->DestroyWidget();
	SlotC->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDropExpiredHoverTest,
	"DreamGUI.DragDrop.ExpiredOperationsDoNotKeepADropTargetHovered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDropExpiredHoverTest::RunTest(const FString& Parameters)
{
	UDreamUIDropTarget* Target = NewObject<UDreamUIDropTarget>(GetTransientPackage());
	UDreamDragDropOperation* Expired = NewObject<UDreamDragDropOperation>(GetTransientPackage());
	Target->NotifyDragEnter(Expired);
	TestTrue(TEXT("A live operation hovers the target"), Target->IsDragHovered());
	Expired->MarkAsGarbage();
	TestFalse(TEXT("An expired weak member cannot keep it hovered"), Target->IsDragHovered());
	UDreamDragDropOperation* Current = NewObject<UDreamDragDropOperation>(GetTransientPackage());
	UDreamDragDropCallProbe* Over = NewObject<UDreamDragDropCallProbe>(GetTransientPackage());
	Target->OnDragOver.AddDynamic(Over, &UDreamDragDropCallProbe::OnOperation);
	Target->NotifyDragEnter(Current);
	Target->NotifyDragOver(Expired);
	Target->NotifyDragLeave(nullptr);
	TestTrue(TEXT("Cleanup of an expired member preserves the new operation"), Target->IsDragHovered());
	Target->NotifyDragOver(Current);
	TestEqual(TEXT("Only the current member receives over"), Over->CallCount, 1);
	TestEqual(TEXT("Over carries the current operation"), Over->LastOperation.Get(), Current);
	Target->NotifyDragLeave(Current);
	TestFalse(TEXT("The final live member leaving clears the aggregate hover"), Target->IsDragHovered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDragAcceptanceReentryTest,
	"DreamGUI.DragDrop.AcceptanceQueriesCanCancelReplaceAndGrowFollowedDrags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDragAcceptanceReentryTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIDragDropSubsystem* DragDrop = TestWorld.World->GetSubsystem<UDreamUIDragDropSubsystem>();
	ADreamStandaloneInputEventSystemActor* EventActor = TestWorld.World->SpawnActor<ADreamStandaloneInputEventSystemActor>();
	UDreamEventSystem* EventSystem = EventActor != nullptr ? EventActor->GetEventSystem() : nullptr;
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(TestWorld.World);
	if (!TestTrue(TEXT("the drag-drop service and its real input event system exist"),
		DragDrop != nullptr && EventSystem != nullptr && Input != nullptr))return false;
	Input->AddEventSystem(EventSystem);

	UDreamWidget* QuerySlot = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamWidget* OtherSlot = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamDropAcceptanceReentryTarget* QueryTarget = QuerySlot->AddComponent<UDreamDropAcceptanceReentryTarget>();
	UDreamUIDropTarget* OtherTarget = OtherSlot->AddComponent<UDreamUIDropTarget>();
	if (!TestNotNull(TEXT("the acceptance query target was created"), QueryTarget)
		|| !TestNotNull(TEXT("the other target was created"), OtherTarget))return false;
	UDreamDragDropCallProbe* Entered = NewObject<UDreamDragDropCallProbe>(TestWorld.World);
	UDreamDragDropCallProbe* Over = NewObject<UDreamDragDropCallProbe>(TestWorld.World);
	QueryTarget->OnDragEnter.AddDynamic(Entered, &UDreamDragDropCallProbe::OnOperation);
	QueryTarget->OnDragOver.AddDynamic(Over, &UDreamDragDropCallProbe::OnOperation);
	auto MakeDrag = [&TestWorld](int32 InPointerID, UDreamWidget* InEnterWidget)
	{
		UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(TestWorld.World);
		EventData->UserIndex = 0;
		EventData->PointerID = InPointerID;
		EventData->bIsDragging = true;
		EventData->EnterWidget = InEnterWidget;
		EventData->EventType = EDreamUIPointerEventType::BeginDrag;
		EventData->DragOperation = NewObject<UDreamDragDropOperation>(TestWorld.World);
		return EventData;
	};

	bool bCancelledInsideQuery = false;
	QueryTarget->Action = [DragDrop, &bCancelledInsideQuery]()
	{
		bCancelledInsideQuery = DragDrop->CancelActiveDragForUser(0);
	};
	UDreamPointerEventData* Cancelled = MakeDrag(0, QuerySlot);
	EventSystem->CallOnPointerBeginDrag(QuerySlot, Cancelled);
	TestEqual(TEXT("CanAcceptDrop actually ran its callback"), QueryTarget->QueryCount, 1);
	TestTrue(TEXT("that callback cancelled the entering drag"), bCancelledInsideQuery);
	TestEqual(TEXT("cancelled drag bookkeeping was removed"), DragDrop->GetDragCount(), 0);
	TestEqual(TEXT("the cancelled query never enters its target"), Entered->CallCount, 0);
	TestEqual(TEXT("and never sends over"), Over->CallCount, 0);
	TestFalse(TEXT("no cancelled operation leaves the target lit"), QueryTarget->IsDragHovered());

	int32 DragsStartedInsideQuery = 0;
	QueryTarget->Action = [EventSystem, OtherSlot, &MakeDrag, &DragsStartedInsideQuery]()
	{
		// Enough distinct pointers to grow the map while the original query is on the stack.
		for (int32 PointerID = 1; PointerID <= 64; ++PointerID)
		{
			UDreamPointerEventData* Finger = MakeDrag(PointerID, OtherSlot);
			EventSystem->CallOnPointerBeginDrag(OtherSlot, Finger);
			++DragsStartedInsideQuery;
		}
	};
	UDreamPointerEventData* Surviving = MakeDrag(0, QuerySlot);
	EventSystem->CallOnPointerBeginDrag(QuerySlot, Surviving);
	TestEqual(TEXT("the second acceptance callback actually ran"), QueryTarget->QueryCount, 2);
	TestEqual(TEXT("it started every additional drag"), DragsStartedInsideQuery, 64);
	TestEqual(TEXT("all new entries and the original drag survive map growth"), DragDrop->GetDragCount(), 65);
	TestEqual(TEXT("the reacquired entry stores the original drag's hover"), DragDrop->GetHoveredTargetForUserPointer(0, 0),
		static_cast<UDreamUIDropTarget*>(QueryTarget));
	TestEqual(TEXT("the original operation entered once after the query"), Entered->CountFor(Surviving->DragOperation.Get()), 1);
	TestEqual(TEXT("and received its own over"), Over->CountFor(Surviving->DragOperation.Get()), 1);
	TestTrue(TEXT("the additional pointer's hover was kept"), OtherTarget->IsDragHovered());
	DragDrop->CancelActiveDragForUser(0);
	TestEqual(TEXT("all grew entries can subsequently be cancelled"), DragDrop->GetDragCount(), 0);

	UDreamPointerEventData* Replacement = MakeDrag(0, OtherSlot);
	QueryTarget->Action = [EventSystem, OtherSlot, Replacement]()
	{
		EventSystem->CallOnPointerBeginDrag(OtherSlot, Replacement);
	};
	UDreamPointerEventData* Replaced = MakeDrag(0, QuerySlot);
	EventSystem->CallOnPointerBeginDrag(QuerySlot, Replaced);
	TestEqual(TEXT("the replacing acceptance callback actually ran"), QueryTarget->QueryCount, 3);
	TestEqual(TEXT("only the new operation is still followed on this pointer"), DragDrop->GetDragOperationForUserPointer(0, 0),
		Replacement->DragOperation.Get());
	TestEqual(TEXT("the old query never overwrites its replacement's hover"), DragDrop->GetHoveredTargetForUserPointer(0, 0), OtherTarget);
	TestEqual(TEXT("the replaced operation never receives enter"), Entered->CountFor(Replaced->DragOperation.Get()), 0);
	TestEqual(TEXT("or over"), Over->CountFor(Replaced->DragOperation.Get()), 0);
	TestFalse(TEXT("the replaced query target stays unlit"), QueryTarget->IsDragHovered());
	DragDrop->CancelActiveDragForUser(0);

	bool bDestroyedInsideQuery = false;
	QueryTarget->Action = [QuerySlot, &bDestroyedInsideQuery]()
	{
		bDestroyedInsideQuery = true;
		QuerySlot->DestroyWidget();
	};
	UDreamPointerEventData* DestroyedTargetDrag = MakeDrag(0, QuerySlot);
	EventSystem->CallOnPointerBeginDrag(QuerySlot, DestroyedTargetDrag);
	TestTrue(TEXT("the final acceptance callback actually destroyed its widget"), bDestroyedInsideQuery);
	TestFalse(TEXT("its target is no longer valid"), IsValid(QueryTarget));
	TestNull(TEXT("the query returns no destroyed hover target"), DragDrop->GetHoveredTargetForUserPointer(0, 0));
	TestEqual(TEXT("the destroyed target receives no new enter"), Entered->CountFor(DestroyedTargetDrag->DragOperation.Get()), 0);
	TestEqual(TEXT("or over"), Over->CountFor(DestroyedTargetDrag->DragOperation.Get()), 0);
	DragDrop->CancelActiveDragForUser(0);
	TestEqual(TEXT("the final drag can also be released"), DragDrop->GetDragCount(), 0);

	// Removal does not garbage-mark a behaviour. Acceptance must also check it still belongs to
	// the component array, rather than returning a valid but detached candidate from the snapshot.
	UDreamWidget* RemovingSlot = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamDropAcceptanceReentryTarget* RemovingTarget = RemovingSlot->AddComponent<UDreamDropAcceptanceReentryTarget>();
	if (!TestNotNull(TEXT("the self-removing target was created"), RemovingTarget))return false;
	RemovingTarget->OnDragEnter.AddDynamic(Entered, &UDreamDragDropCallProbe::OnOperation);
	RemovingTarget->OnDragOver.AddDynamic(Over, &UDreamDragDropCallProbe::OnOperation);
	bool bRemovedInsideQuery = false;
	RemovingTarget->Action = [RemovingSlot, RemovingTarget, &bRemovedInsideQuery]()
	{
		bRemovedInsideQuery = true;
		RemovingSlot->RemoveComponent(RemovingTarget);
	};
	UDreamPointerEventData* RemovedTargetDrag = MakeDrag(0, RemovingSlot);
	EventSystem->CallOnPointerBeginDrag(RemovingSlot, RemovedTargetDrag);
	TestEqual(TEXT("the self-removing candidate was queried once"), RemovingTarget->QueryCount, 1);
	TestTrue(TEXT("its acceptance callback removed the component"), bRemovedInsideQuery);
	TestTrue(TEXT("the removed object is still live, so validity alone would not reject it"), IsValid(RemovingTarget));
	TestFalse(TEXT("the component no longer belongs to that slot"), RemovingSlot->GetAllComponents().Contains(RemovingTarget));
	TestNull(TEXT("the detached candidate is not returned as a hover target"), DragDrop->GetHoveredTargetForUserPointer(0, 0));
	TestEqual(TEXT("the detached target receives no enter"), Entered->CountFor(RemovedTargetDrag->DragOperation.Get()), 0);
	TestEqual(TEXT("or over"), Over->CountFor(RemovedTargetDrag->DragOperation.Get()), 0);
	DragDrop->CancelActiveDragForUser(0);
	RemovingSlot->DestroyWidget();
	OtherSlot->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDragVisualInitializationReentryTest,
	"DreamGUI.DragDrop.VisualInitializationCanCancelReplaceAndGrowFollowedDrags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDragVisualInitializationReentryTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIDragDropSubsystem* DragDrop = TestWorld.World->GetSubsystem<UDreamUIDragDropSubsystem>();
	UDreamScreenUISubsystem* Screen = TestWorld.World->GetSubsystem<UDreamScreenUISubsystem>();
	ADreamStandaloneInputEventSystemActor* EventActor = TestWorld.World->SpawnActor<ADreamStandaloneInputEventSystemActor>();
	UDreamEventSystem* EventSystem = EventActor != nullptr ? EventActor->GetEventSystem() : nullptr;
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(TestWorld.World);
	if (!TestTrue(TEXT("the real drag, screen and input services exist"), DragDrop != nullptr
		&& Screen != nullptr && EventSystem != nullptr && Input != nullptr))return false;
	Input->AddEventSystem(EventSystem);
	UDreamWidget* ScreenRoot = Screen->GetOrCreateScreenRootForUserIndex(0);
	if (!TestNotNull(TEXT("a screen root can own the visual"), ScreenRoot))return false;
	TStrongObjectPtr<UDreamDragVisualReentryProbe> Probe(NewObject<UDreamDragVisualReentryProbe>(TestWorld.World));
	Probe->ScreenRoot = ScreenRoot;
	UDreamDragVisualReentryWidget::ActiveProbe = Probe.Get();
	ON_SCOPE_EXIT { UDreamDragVisualReentryWidget::ActiveProbe.Reset(); };
	UDreamWidget* Source = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	auto MakeDrag = [&TestWorld, Source](int32 InPointerID, bool bInHasVisual)
	{
		UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(TestWorld.World);
		EventData->UserIndex = 0;
		EventData->PointerID = InPointerID;
		EventData->bIsDragging = true;
		EventData->EnterWidget = Source;
		EventData->EventType = EDreamUIPointerEventType::BeginDrag;
		EventData->DragOperation = NewObject<UDreamDragDropOperation>(TestWorld.World);
		if (bInHasVisual)EventData->DragOperation->DragVisualClass = UDreamDragVisualReentryWidget::StaticClass();
		return EventData;
	};
	auto CountLiveHolders = [ScreenRoot]()
	{
		int32 Count = 0;
		for (UDreamWidget* Child : ScreenRoot->GetChildren())
		{
			if (IsValid(Child) && Child->GetDisplayName() == TEXT("DreamUIDragVisual"))++Count;
		}
		return Count;
	};

	bool bCancelledInsideInitialization = false;
	bool bVisualSurvivedTheCallback = false;
	Probe->Action = [DragDrop, Observer = Probe.Get(), &bCancelledInsideInitialization, &bVisualSurvivedTheCallback](UDreamUserWidget* InVisual)
	{
		bCancelledInsideInitialization = DragDrop->CancelActiveDragForUser(0);
		bVisualSurvivedTheCallback = IsValid(InVisual) && Observer->InitialHolders.Last().IsValid();
	};
	UDreamPointerEventData* Cancelled = MakeDrag(0, true);
	EventSystem->CallOnPointerBeginDrag(Source, Cancelled);
	if (!TestEqual(TEXT("the visual's actual initialization hook ran"), Probe->InitializedVisuals.Num(), 1)
		|| !TestEqual(TEXT("and ran the cancellation action once"), Probe->MutationCount, 1))return false;
	TestTrue(TEXT("the initialization action cancelled its own drag"), bCancelledInsideInitialization);
	TestTrue(TEXT("construction was allowed to finish before destroying the cancelled visual"), bVisualSurvivedTheCallback);
	TestEqual(TEXT("no cancelled drag remains"), DragDrop->GetDragCount(), 0);
	TestFalse(TEXT("its orphan visual was destroyed after initialization returned"), Probe->InitializedVisuals[0].IsValid());
	TestFalse(TEXT("the local holder was also destroyed"), Probe->InitialHolders[0].IsValid());
	TestEqual(TEXT("the screen contains no cancelled visual holder"), CountLiveHolders(), 0);

	int32 DragsStartedInsideInitialization = 0;
	Probe->Action = [EventSystem, Source, &MakeDrag, &DragsStartedInsideInitialization](UDreamUserWidget* InVisual)
	{
		for (int32 PointerID = 1; PointerID <= 64; ++PointerID)
		{
			EventSystem->CallOnPointerBeginDrag(Source, MakeDrag(PointerID, false));
			++DragsStartedInsideInitialization;
		}
	};
	UDreamPointerEventData* Surviving = MakeDrag(0, true);
	EventSystem->CallOnPointerBeginDrag(Source, Surviving);
	if (!TestEqual(TEXT("the next visual initialized once"), Probe->InitializedVisuals.Num(), 2))return false;
	TestEqual(TEXT("its initialization action really started 64 other drags"), DragsStartedInsideInitialization, 64);
	TestEqual(TEXT("both initialization actions ran"), Probe->MutationCount, 2);
	TestEqual(TEXT("the map grew while retaining the original drag"), DragDrop->GetDragCount(), 65);
	TestTrue(TEXT("the original visual is still alive after map growth"), Probe->InitializedVisuals[1].IsValid());
	TestTrue(TEXT("and so is its holder"), Probe->InitialHolders[1].IsValid());
	TestEqual(TEXT("the screen contains only the one visual that was requested"), CountLiveHolders(), 1);
	DragDrop->CancelActiveDragForUser(0);
	TestEqual(TEXT("cancelling all pointers removes the grown entries"), DragDrop->GetDragCount(), 0);
	TestFalse(TEXT("the original drag owns and destroys its visual after map growth"), Probe->InitializedVisuals[1].IsValid());
	TestFalse(TEXT("and its holder"), Probe->InitialHolders[1].IsValid());
	TestEqual(TEXT("no visual holder leaked from an obsolete map entry"), CountLiveHolders(), 0);

	UDreamPointerEventData* Replacement = MakeDrag(0, true);
	Probe->Action = [EventSystem, Source, Replacement](UDreamUserWidget* InVisual)
	{
		EventSystem->CallOnPointerBeginDrag(Source, Replacement);
	};
	UDreamPointerEventData* Replaced = MakeDrag(0, true);
	EventSystem->CallOnPointerBeginDrag(Source, Replaced);
	if (!TestEqual(TEXT("both the replaced and replacement visual initialized"), Probe->InitializedVisuals.Num(), 4))return false;
	TestEqual(TEXT("the replace action ran only from the old visual"), Probe->MutationCount, 3);
	TestEqual(TEXT("the replacement is the only followed drag"), DragDrop->GetDragCount(), 1);
	TestEqual(TEXT("the pointer follows the replacement operation"), DragDrop->GetDragOperationForUserPointer(0, 0),
		Replacement->DragOperation.Get());
	TestFalse(TEXT("the superseded visual was destroyed"), Probe->InitializedVisuals[2].IsValid());
	TestFalse(TEXT("and its unpublished holder"), Probe->InitialHolders[2].IsValid());
	TestTrue(TEXT("the replacement's visual remains alive"), Probe->InitializedVisuals[3].IsValid());
	TestTrue(TEXT("and has its own live holder"), Probe->InitialHolders[3].IsValid());
	TestEqual(TEXT("only the replacement's holder remains on screen"), CountLiveHolders(), 1);
	DragDrop->CancelActiveDragForUser(0);
	TestFalse(TEXT("cancelling the replacement destroys its own visual"), Probe->InitializedVisuals[3].IsValid());
	TestFalse(TEXT("and its holder"), Probe->InitialHolders[3].IsValid());
	TestEqual(TEXT("all visual holders were released"), CountLiveHolders(), 0);
	Source->DestroyWidget();
	return true;
}

#endif
