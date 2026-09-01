// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "Extensions/DreamUMGWidgetInteraction.h"
#include "Extensions/DreamVisualCustomRaycastExtensions.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/*
 * The four interaction extensions, and the hard part of testing them: almost everything they do
 * needs something this suite does not have.
 *
 * The suite runs -nullrhi with no viewport, no Slate virtual user, no real input device, and the
 * widgets a test builds have no world. That is not a limitation to work around -- most of it is the
 * production case for a dedicated server or an authoring tree, which is precisely why the guards
 * these classes carry are worth pinning. So the assertions here are aimed at three kinds of thing:
 *
 *   1. Pure arithmetic and pure type decisions, which need nothing.  The drag threshold's
 *      geometry, and the pixel raycast's refusal to read pixels from a visual that has none.
 *   2. Configuration and registration, which need at most a world.    Whether a raycaster enrols
 *      itself with the manager, and what a component's tick contract is.
 *   3. Guard placement -- whether an entry point checks before it mutates. A guard put one line
 *      too late is invisible in a code read and fatal at runtime, and it is exactly what a
 *      headless fixture can prove, because headless is the state the guard exists for.
 *
 * A note on what changed here. An earlier revision of this file could not call
 * UDreamUMGWidgetInteraction's hover handlers or UDreamUIRenderTargetInteraction's pointer handlers
 * at all -- both families opened by dereferencing something only a lifecycle callback this fixture
 * never runs had created, so a test that called them would have taken the process down instead of
 * failing. Those dereferences are gone, and the tests that now call them are the ones worth having:
 * headless is not a special case being accommodated, it is the same state a dedicated server and an
 * authoring tree are in, and it is the state the guards exist for.
 *
 * What is deliberately NOT asserted, and why:
 *
 *   UDreamUIRenderTargetInteraction::TickComponent, ::LineTrace and ::Raycast. All three need a
 *   component implementing IDreamUIRenderTargetInteractionSourceInterface, a canvas rendering to a
 *   render target, and a view-projection matrix from it. The interesting half -- projecting a hit
 *   UV back into a ray -- is UDreamScreenSpaceRaycaster::DeprojectViewPointToWorld, which belongs
 *   to the base class and is not this type's decision. There is nothing left here that a fixture
 *   could hold without building a render-target canvas, which needs the RHI.
 *
 *   UDreamUMGWidgetInteraction's Slate side -- SimulatePointerMovement, DetermineWidgetUnderPointer,
 *   the routed pointer and key events. Every one of them requires a real FSlateVirtualUserHandle,
 *   which FindOrCreateVirtualUser only issues in a non-preview world and which changes real focus
 *   when it does. What IS tested is that they all refuse without one, which is the contract that
 *   keeps them from being fatal on a server.
 *
 *   UDreamVisualCustomRaycast_VisiblePixel's actual pixel read. It needs a batch-mesh visual with
 *   built geometry -- OriginVertices, Triangles and an interpolated UV -- and a UTexture2D whose
 *   mip 0 bulk data is still CPU-resident. Geometry is built by the canvas/drawcall pipeline, so a
 *   fixture would have to stand up rendering to reach one triangle of arithmetic. The gate in
 *   front of it is testable and is what is asserted instead.
 *
 *   UUINavigationInputSelectionHandler's three selection branches. All of them go through
 *   UDreamWidget::RenderOpacityTo, and the tween manager returns null with no world (see
 *   DreamAuxControlsAutomationTests, which documents the same wall). SelectNone then chains
 *   ->OnComplete straight off that null return, so exercising it headless is a crash rather than a
 *   failure. Reported as a defect. The guards in front of those branches are tested.
 */

namespace DreamInteractionExtensionTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Reach a protected UPROPERTY, and say so loudly if a rename has moved it out from under us. */
	template<typename T>
	T* FieldPtr(FAutomationTestBase& Test, UObject* Object, const TCHAR* PropertyName)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(
				TEXT("%s no longer has a property named '%s', so this test is asserting against nothing."),
				*Object->GetClass()->GetName(), PropertyName));
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<T>(Object);
	}

	/** Length of a protected array UPROPERTY without naming its element type. */
	int32 ArrayNum(FAutomationTestBase& Test, UObject* Object, const TCHAR* PropertyName)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(
			Object->GetClass()->FindPropertyByName(FName(PropertyName)));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(
				TEXT("%s no longer has an array property named '%s'."),
				*Object->GetClass()->GetName(), PropertyName));
			return INDEX_NONE;
		}
		FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
		return Helper.Num();
	}

	/** A pointer that was pressed at one place and is now at another. */
	UDreamPointerEventData* MakeDrag(const FVector& PressAt, const FVector& NowAt)
	{
		UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());
		EventData->PressPointerPosition = PressAt;
		EventData->PointerPosition = NowAt;
		return EventData;
	}

	/**
	 * Reaches UDreamVisualCustomRaycast's protected static from outside the hierarchy.
	 *
	 * "Protected in C++ and BlueprintCallable" is not a contradiction: it means a Blueprint deriving
	 * from UDreamVisualCustomRaycast may call it from its own graph, which is exactly who can leave
	 * the object pin unconnected. A derived class is therefore also the only thing that can reach it
	 * from here, and this one exists for the using-declaration alone -- nothing is ever constructed
	 * from it, which is why it needs none of the machinery a real UCLASS would.
	 */
	struct FPixelReadAccess : public UDreamVisualCustomRaycast
	{
		using UDreamVisualCustomRaycast::GetRaycastPixelFromUIBatchMeshVisual;
	};

	/** Manager is non-const because GetAllRaycasterArray is, even though it only reads. */
	bool IsEnrolled(UDreamUIManagerWorldSubsystem* Manager, const UDreamBaseRaycaster* Raycaster)
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& Enrolled : Manager->GetAllRaycasterArray())
		{
			if (Enrolled.Get() == Raycaster)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTargetDragThresholdTest,
	"DreamGUI.Interaction.RenderTarget.APointerExactlyAtTheDragThresholdIsNotYetADrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTargetDragThresholdTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// No world and no owner, and ShouldStartDrag now answers from the event data alone in BOTH of
	// its branches: the hold-to-drag one asks for a clock, finds no world holding one, and declines
	// to call the hold elapsed instead of dereferencing the world that is not there.
	UDreamUIRenderTargetInteraction* Interaction =
		NewObject<UDreamUIRenderTargetInteraction>(GetTransientPackage());

	// The render-target component does NOT override ShouldStartDrag. It used to, with a body copied
	// character for character from the base class, which meant every fix to the drag rule reached
	// every raycaster except this one. The call is still written through a base pointer -- that is
	// where the member is public -- and it is still a virtual dispatch, so if an override ever
	// returns this test goes on testing whatever the override does.
	UDreamScreenSpaceRaycaster* AsRaycaster = Interaction;

	Interaction->SetDragThreshold(5.0f);
	TestEqual(TEXT("the squared threshold is kept in step with the threshold"),
		Interaction->GetDragThresholdSquare(), 25.0f);

	// A press that has not moved is a click in progress, and calling it a drag steals the click.
	TestFalse(TEXT("a pointer that has not moved is not dragging"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector::ZeroVector)));

	// Inside the threshold is the hand shaking, which is the whole reason a threshold exists.
	TestFalse(TEXT("a small jitter inside the threshold is not dragging"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 3.0, 0.0))));

	// The comparison is strictly greater, so the threshold distance itself still counts as held. An
	// inclusive test here would make the boundary a coin flip on float rounding for anyone who
	// authored a threshold their input happens to land on exactly.
	TestFalse(TEXT("exactly at the threshold is still not dragging"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 5.0, 0.0))));
	TestTrue(TEXT("a hair past the threshold is dragging"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 5.1, 0.0))));

	// Distance is measured across both axes together rather than per-axis, so a diagonal move that
	// clears neither axis on its own still clears the threshold. 4 and 4 make 32, which beats 25.
	TestTrue(TEXT("a diagonal move is measured as one distance, not two"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(4.0, 4.0, 0.0))));

	// PointerPosition is an FVector whose own documentation says "X&Y for mouse position", and the
	// conversion to FVector2D takes exactly those two. Z is the spare component -- on a render-target
	// canvas the position has already been flattened into the target's pixel space -- so a value left
	// there must not be able to start a drag on its own.
	TestFalse(TEXT("movement in the unused third component cannot start a drag"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 0.0, 1000.0))));

	// A threshold of zero means any movement at all drags, and no movement still does not. This is
	// the setting a "drag anywhere" surface uses and it must not degenerate into "always dragging".
	Interaction->SetDragThreshold(0.0f);
	TestEqual(TEXT("a zero threshold squares to zero"), Interaction->GetDragThresholdSquare(), 0.0f);
	TestFalse(TEXT("with no threshold, a stationary pointer still is not dragging"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector::ZeroVector)));
	TestTrue(TEXT("with no threshold, the smallest movement drags"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 0.01, 0.0))));

	// A widened threshold takes effect immediately, because the setter is what recomputes the
	// square. Writing DragThreshold directly does not: only the constructor, BeginPlay, this setter
	// and PostEditChangeProperty ever recompute it, so the setter is the only supported way to
	// change it while the game runs.
	Interaction->SetDragThreshold(50.0f);
	TestEqual(TEXT("a new threshold is squared straight away"), Interaction->GetDragThresholdSquare(), 2500.0f);
	TestFalse(TEXT("a movement that dragged at the old threshold does not at the new one"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 5.1, 0.0))));

	// And the other branch, which is the one that used to be unreachable from here. Hold-to-drag
	// turns a press that has not moved into a drag once it has been held long enough, which means
	// asking the world what time it is -- and there is no world, exactly as there is none on a
	// widget in a Blueprint's authoring tree. The answer has to be "not yet a drag", arrived at
	// rather than crashed into: a hold cannot have elapsed against a clock that does not exist.
	Interaction->SetDragThreshold(5.0f);
	Interaction->SetHoldToDrag(true);
	Interaction->SetHoldToDragTime(0.5f);
	TestTrue(TEXT("hold-to-drag is on"), Interaction->GetHoldToDrag());
	TestFalse(TEXT("with no clock to measure it, a hold has not elapsed"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector::ZeroVector)));

	// Falling through is not the same as returning false. The distance test still runs and still
	// decides, so a pointer that has genuinely moved past the threshold drags on a held button even
	// though the hold itself could not be measured.
	TestTrue(TEXT("...and the distance test still gets to answer"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector(0.0, 5.1, 0.0))));

	// A hold time of zero is the "any press is a drag" setting, and it must not become that here by
	// accident: without a clock there is still nothing to compare, so the answer is unchanged.
	Interaction->SetHoldToDragTime(0.0f);
	TestFalse(TEXT("a zero hold time does not conjure a clock either"),
		AsRaycaster->ShouldStartDrag(MakeDrag(FVector::ZeroVector, FVector::ZeroVector)));

	// The value the hold branch subtracts, on an event data that has never been pressed. It has to
	// be a defined number rather than whatever was in the memory: a hover or a scroll carries an
	// event data no button ever went down on, and PressTime, ClickTime and ReleaseTime were the only
	// fields on that class with no initialiser. Indeterminate bytes here would make hold-to-drag
	// begin a drag, or refuse to, at random on a pointer that had not been pressed at all.
	UDreamPointerEventData* Fresh = NewObject<UDreamPointerEventData>(GetTransientPackage());
	TestEqual(TEXT("a fresh pointer has not been pressed"), Fresh->PressTime, 0.0);
	TestEqual(TEXT("...nor clicked"), Fresh->ClickTime, 0.0);
	TestEqual(TEXT("...nor released"), Fresh->ReleaseTime, 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScreenSpaceDragThresholdEditTest,
	"DreamGUI.Interaction.ScreenSpace.ADragThresholdTypedIntoTheDetailsPanelTakesEffectBeforePlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScreenSpaceDragThresholdEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// DragThresholdSquare is a derived value, and the Details panel writes the value it is derived
	// FROM. Every other writer of DragThreshold recomputes the square on the spot; the property
	// editor cannot, because it writes the field through reflection and then tells the object about
	// it afterwards. Without a PostEditChangeProperty to hear that, an author who types a new
	// threshold and drags in the viewport is measured against the old one until something calls
	// BeginPlay -- so the staleness lasts exactly as long as the editing session that would notice.
	UDreamScreenSpaceRaycaster* Raycaster = NewObject<UDreamScreenSpaceRaycaster>(GetTransientPackage());

	// Everything below is expressed against the authored default rather than against the number that
	// default happens to be today, so retuning the default does not turn this test into a liar.
	const float AuthoredDefault = Raycaster->GetDragThreshold();
	const float SquareBeforeEdit = Raycaster->GetDragThresholdSquare();
	TestEqual(TEXT("the constructor squared the authored default"),
		SquareBeforeEdit, AuthoredDefault * AuthoredDefault);

	float* DragThreshold = FieldPtr<float>(*this, Raycaster, TEXT("DragThreshold"));
	if (DragThreshold == nullptr)
	{
		return false;
	}

	// Written the way the property editor writes it: straight into the field, with nothing told.
	const float Edited = AuthoredDefault + 15.0f;
	*DragThreshold = Edited;
	TestEqual(TEXT("writing the field alone leaves the square stale, which is the whole problem"),
		Raycaster->GetDragThresholdSquare(), SquareBeforeEdit);

	FProperty* Property = Raycaster->GetClass()->FindPropertyByName(TEXT("DragThreshold"));
	FProperty* Unrelated = Raycaster->GetClass()->FindPropertyByName(TEXT("RayLength"));
	if (!TestNotNull(TEXT("the raycaster still has a DragThreshold property"), Property)
		|| !TestNotNull(TEXT("...and a RayLength one to stand in for an unrelated edit"), Unrelated))
	{
		return false;
	}

	FPropertyChangedEvent PropertyChangedEvent(Property);
	Raycaster->PostEditChangeProperty(PropertyChangedEvent);
	TestEqual(TEXT("and being told is what squares it"),
		Raycaster->GetDragThresholdSquare(), Edited * Edited);

	// The recompute is deliberately not conditional on WHICH property changed. A name-matched branch
	// is one rename away from going quietly dead again, and this multiply costs nothing next to
	// everything else an editor property change sets off -- so any property change refreshes it.
	const float EditedAgain = 3.0f;
	*DragThreshold = EditedAgain;
	FPropertyChangedEvent UnrelatedChangedEvent(Unrelated);
	Raycaster->PostEditChangeProperty(UnrelatedChangedEvent);
	TestEqual(TEXT("a change to any property brings the square back into step"),
		Raycaster->GetDragThresholdSquare(), EditedAgain * EditedAgain);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTargetSelfDrivenTest,
	"DreamGUI.Interaction.RenderTarget.ThisRaycasterDrivesItselfInsteadOfEnrollingWithTheManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTargetSelfDrivenTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;
	FScopedGameWorld TestWorld;

	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("the world has a DreamUI manager"), Manager))
	{
		return false;
	}
	AActor* Host = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor to hang the raycasters on"), Host))
	{
		return false;
	}

	// The negative control comes first and it is doing real work here: ActivateRaycaster on the base
	// class is what puts a raycaster into the manager's list. Without this assertion, "the
	// render-target one is absent" would be satisfied just as well by a broken fixture in which
	// nothing enrols at all.
	UDreamScreenSpaceRaycaster* Ordinary = NewObject<UDreamScreenSpaceRaycaster>(Host);
	Ordinary->RegisterComponent();
	Ordinary->ActivateRaycaster();
	TestTrue(TEXT("an ordinary raycaster enrols itself with the manager"), IsEnrolled(Manager, Ordinary));

	// And the claim. This component processes input inside its own TickComponent instead of being
	// pumped by the manager's raycaster sweep, so enrolling would have it handle every pointer
	// twice -- once through the sweep and once through its own tick.
	UDreamUIRenderTargetInteraction* SelfDriven = NewObject<UDreamUIRenderTargetInteraction>(Host);
	SelfDriven->RegisterComponent();
	SelfDriven->ActivateRaycaster();
	TestFalse(TEXT("the render-target interaction stays out of the manager's list"),
		IsEnrolled(Manager, SelfDriven));

	// The other half of the same bargain, and the reason opting out is safe: it ticks. A change that
	// made ActivateRaycaster call Super without also turning the tick off would double-drive it, and
	// a change that turned the tick off without enrolling would make it deaf. Asserting both sides
	// together is what catches either half moving on its own.
	TestTrue(TEXT("...because it is ticking to drive itself"),
		SelfDriven->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("whereas a manager-pumped raycaster does not tick"),
		Ordinary->PrimaryComponentTick.bCanEverTick);

	// Deactivation is overridden to nothing for the same reason, and "nothing" has to mean nothing:
	// a DeactivateRaycaster that fell through to Super would call RemoveRaycaster, which searches
	// the shared list. Harmless here, but it is the same list every other raycaster lives in.
	SelfDriven->DeactivateRaycaster();
	TestFalse(TEXT("deactivating it changes nothing about its own absence"),
		IsEnrolled(Manager, SelfDriven));
	TestTrue(TEXT("...and leaves the ordinary raycaster enrolled"), IsEnrolled(Manager, Ordinary));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTargetPointerBeforeBeginPlayTest,
	"DreamGUI.Interaction.RenderTarget.APointerArrivingBeforeBeginPlayIsHandledRatherThanFatal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTargetPointerBeforeBeginPlayTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// This component synthesises a SECOND pointer, the one the UI drawn into the render target sees,
	// and that object used to be built by BeginPlay alone. That quietly made "BeginPlay has run" a
	// precondition of three handlers the EVENT SYSTEM calls -- nothing orders the two against each
	// other, and an editor path that drives this component never runs BeginPlay at all. The
	// un-BeginPlayed component below is that state, on purpose.
	UDreamUIRenderTargetInteraction* Interaction =
		NewObject<UDreamUIRenderTargetInteraction>(GetTransientPackage());

	TObjectPtr<UDreamPointerEventData>* Synthesised =
		FieldPtr<TObjectPtr<UDreamPointerEventData>>(*this, Interaction, TEXT("PointerEventData"));
	if (Synthesised == nullptr)
	{
		return false;
	}
	TestNull(TEXT("nothing has built the synthesised pointer yet"), Synthesised->Get());

	UDreamPointerEventData* Incoming = NewObject<UDreamPointerEventData>(GetTransientPackage());
	Incoming->MouseButtonType = EDreamUIMouseButtonType::Right;

	// The press is HANDLED rather than dropped, and the difference matters: dropping the press while
	// still delivering the release would leave the synthesised pointer believing a button it never
	// saw go down had come back up.
	TestFalse(TEXT("a press before BeginPlay is consumed"),
		IDreamPointerDownUpInterface::Execute_OnPointerDown(Interaction, Incoming));
	if (!TestNotNull(TEXT("...because the synthesised pointer is built on demand"), Synthesised->Get()))
	{
		return false;
	}
	TestTrue(TEXT("the press was recorded"), (*Synthesised)->bNowIsTriggerPressed);
	TestEqual(TEXT("...along with which button it was"),
		(int32)(*Synthesised)->MouseButtonType, (int32)EDreamUIMouseButtonType::Right);

	// The identity BeginPlay used to be responsible for. It is not decoration: -1 is what tells this
	// pointer apart from the ones the event system hands out, so building the object on demand has
	// to produce the same object BeginPlay would have.
	TestEqual(TEXT("and it is marked as this component's own, not the event system's"),
		(*Synthesised)->PointerID, -1);

	// The timestamp, taken where there is no world holding a clock. Zero is not a plausible moment
	// so much as a defined one -- the only reader is the base class's hold-to-drag test, which
	// declines to measure a hold at all without a world -- and defined is the entire requirement,
	// because the line that produces it used to be GetWorld()->TimeSeconds with nothing in front.
	TestEqual(TEXT("a press outside a world stamps a defined time rather than dereferencing null"),
		(*Synthesised)->PressTime, 0.0);

	// The release has to land on the object the press created rather than on a fresh one, or the
	// press and the release would be describing two different pointers.
	UDreamPointerEventData* PressedPointer = Synthesised->Get();
	TestFalse(TEXT("a release before BeginPlay is consumed"),
		IDreamPointerDownUpInterface::Execute_OnPointerUp(Interaction, Incoming));
	TestTrue(TEXT("...on the same synthesised pointer the press used"), Synthesised->Get() == PressedPointer);
	TestFalse(TEXT("and the press is over"), (*Synthesised)->bNowIsTriggerPressed);
	TestEqual(TEXT("with a defined release time as well"), (*Synthesised)->ReleaseTime, 0.0);

	// Scroll is the third of the family. With nothing hovered there is nobody to forward the wheel
	// to, so it is swallowed -- but reaching even that decision means reading EnterWidget off the
	// synthesised pointer, which is the dereference this test exists for.
	Incoming->ScrollAxisValue = FVector2D(0.0, 1.0);
	TestFalse(TEXT("a scroll before BeginPlay is consumed"),
		IDreamPointerScrollInterface::Execute_OnPointerScroll(Interaction, Incoming));
	TestEqual(TEXT("...and goes nowhere, because nothing is hovered to receive it"),
		(*Synthesised)->ScrollAxisValue, FVector2D::ZeroVector);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGInteractionNoVirtualUserTest,
	"DreamGUI.Interaction.UMG.EveryInputEntryPointRefusesWhenThereIsNoVirtualUser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUMGInteractionNoVirtualUserTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// A component that never Awoke, which is the same state a dedicated server, a preview world and
	// an authoring tree leave it in: no virtual Slate user, no widget component. Every public entry
	// point routes through CanSendInput, and CanSendInput requires both. What this test is really
	// about is WHERE that check sits -- before the state is touched, or after. Put it one line later
	// in PressPointerKey and the key still lands in PressedKeys; put it after the
	// DetermineWidgetUnderPointer call and it is a null dereference on WidgetComponent.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Surface"));
	if (!TestNotNull(TEXT("a widget to host the interaction"), Widget))
	{
		return false;
	}
	Tree->RootWidget = Widget;
	Widget->SetWidth(200.0f);
	Widget->SetHeight(100.0f);
	UDreamUMGWidgetInteraction* Interaction = Widget->AddComponent<UDreamUMGWidgetInteraction>();
	if (!TestNotNull(TEXT("the widget carries a UMG interaction"), Interaction))
	{
		return false;
	}

	// The keyboard entry points answer with a bool, so their refusal is directly observable.
	TestFalse(TEXT("a key press is refused"), Interaction->PressKey(EKeys::A, false));
	TestFalse(TEXT("a key release is refused"), Interaction->ReleaseKey(EKeys::A));
	TestFalse(TEXT("a press-and-release is refused"), Interaction->PressAndReleaseKey(EKeys::A));
	TestFalse(TEXT("a string of characters is refused"), Interaction->SendKeyChar(TEXT("hello"), false));

	// The pointer entry points answer with nothing, so the refusal has to be read from the state
	// they would have written. All four of these are set inside DetermineWidgetUnderPointer, which
	// is downstream of the guard and would have dereferenced a null widget component to get there.
	Interaction->PressPointerKey(EKeys::LeftMouseButton);
	Interaction->ReleasePointerKey(EKeys::LeftMouseButton);
	Interaction->ScrollWheel(1.0f);

	TestFalse(TEXT("no interactable widget was ever looked for"), Interaction->IsOverInteractableWidget());
	TestFalse(TEXT("nor a focusable one"), Interaction->IsOverFocusableWidget());
	TestFalse(TEXT("nor a hit-test-visible one"), Interaction->IsOverHitTestVisibleWidget());
	TestFalse(TEXT("and no widget path was cached"), Interaction->GetHoveredWidgetPath().IsValid());
	TestEqual(TEXT("and no hit location was recorded"), Interaction->Get2DHitLocation(), FVector2D::ZeroVector);

	// SetFocus guards on the virtual user BEFORE it touches its argument, which is the only reason
	// clearing focus with a null widget is survivable. It is worth pinning because the order is
	// load-bearing and reads as incidental: the very next statement is FocusWidget->GetCachedWidget().
	Interaction->SetFocus(nullptr);
	TestFalse(TEXT("clearing focus with nothing to focus is inert rather than fatal"),
		Interaction->IsOverFocusableWidget());

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGInteractionBubbleTest,
	"DreamGUI.Interaction.UMG.APointerEventIsConsumedUnlessBubblingIsAskedFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUMGInteractionBubbleTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// The return value of these handlers is the only thing they say to the outside world: false
	// means the event stops here, and it stops here because a UMG surface has just been handed the
	// input and something behind it must not also act on it. A default of true would let a click on
	// a button drawn into a world-space UMG panel also press whatever DreamGUI widget is underneath.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Surface"));
	if (!TestNotNull(TEXT("a widget to host the interaction"), Widget))
	{
		return false;
	}
	Tree->RootWidget = Widget;
	UDreamUMGWidgetInteraction* Interaction = Widget->AddComponent<UDreamUMGWidgetInteraction>();
	if (!TestNotNull(TEXT("the widget carries a UMG interaction"), Interaction))
	{
		return false;
	}

	// Down, Up and Scroll are asserted here. Enter and Exit answer the same way and have their own
	// test, because what they RETURN is the least interesting thing about them.
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());
	EventData->ScrollAxisValue = FVector2D(0.0, 1.0);

	TestFalse(TEXT("a press is consumed by default"),
		IDreamPointerDownUpInterface::Execute_OnPointerDown(Interaction, EventData));
	TestFalse(TEXT("a release is consumed by default"),
		IDreamPointerDownUpInterface::Execute_OnPointerUp(Interaction, EventData));
	TestFalse(TEXT("a scroll is consumed by default"),
		IDreamPointerScrollInterface::Execute_OnPointerScroll(Interaction, EventData));

	// And the single knob that changes it, which is a Details-panel checkbox in production.
	bool* AllowBubbleUp = FieldPtr<bool>(*this, Interaction, TEXT("bAllowEventBubbleUp"));
	if (AllowBubbleUp == nullptr)
	{
		return false;
	}
	*AllowBubbleUp = true;

	TestTrue(TEXT("a press bubbles once the component is asked to let it"),
		IDreamPointerDownUpInterface::Execute_OnPointerDown(Interaction, EventData));
	TestTrue(TEXT("so does a release"),
		IDreamPointerDownUpInterface::Execute_OnPointerUp(Interaction, EventData));
	TestTrue(TEXT("and so does a scroll"),
		IDreamPointerScrollInterface::Execute_OnPointerScroll(Interaction, EventData));

	// The answer is the same whether the underlying Slate call went anywhere or not. That is the
	// point: bubbling describes what this component claims about the event, not whether the UMG
	// widget behind it did something with it, and a version that returned "did Slate handle it"
	// would make an unhandled click fall through to the DreamGUI widget underneath.
	TestFalse(TEXT("and nothing was actually sent to Slate on the way"),
		Interaction->GetHoveredWidgetPath().IsValid());

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGInteractionHoverWithoutVirtualUserTest,
	"DreamGUI.Interaction.UMG.AHoverOnAComponentThatNeverEnrolledIsInertRatherThanFatal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUMGInteractionHoverWithoutVirtualUserTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// The test an earlier revision of this file could not write. Both handlers used to open by
	// dereferencing UDreamUMGWidgetInteractionManager::Instance and then indexing a TMap with
	// operator[], which checks on a missing key rather than reporting it -- so calling either one
	// from here ended the process instead of failing an assertion. Nor was that state exotic: Awake
	// created the Instance unconditionally but added the map entry only where Slate was initialised
	// and the world was not a preview one, so a build with no Slate application had the static and
	// not the entry, and went down on the first hover a player ever made.
	UDreamUMGWidgetInteractionManager* const ManagerBefore = UDreamUMGWidgetInteractionManager::Instance;

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Surface"));
	if (!TestNotNull(TEXT("a widget to host the interaction"), Widget))
	{
		return false;
	}
	Tree->RootWidget = Widget;
	UDreamUMGWidgetInteraction* Interaction = Widget->AddComponent<UDreamUMGWidgetInteraction>();
	if (!TestNotNull(TEXT("the widget carries a UMG interaction"), Interaction))
	{
		return false;
	}

	UDreamPointerEventData* Pointer = NewObject<UDreamPointerEventData>(GetTransientPackage());
	TestFalse(TEXT("a hover is consumed"),
		IDreamPointerEnterExitInterface::Execute_OnPointerEnter(Interaction, Pointer));
	TestFalse(TEXT("and so is the hover ending"),
		IDreamPointerEnterExitInterface::Execute_OnPointerExit(Interaction, Pointer));

	// Leaving with a pointer that never arrived, and leaving twice. Both are ordinary -- exit is
	// delivered along paths enter did not take, and a pointer can be torn down mid-hover -- and both
	// used to reach the same two dereferences as the well-formed case.
	UDreamPointerEventData* Stranger = NewObject<UDreamPointerEventData>(GetTransientPackage());
	TestFalse(TEXT("an exit from a pointer that never entered is consumed"),
		IDreamPointerEnterExitInterface::Execute_OnPointerExit(Interaction, Stranger));
	TestFalse(TEXT("and a second exit changes nothing"),
		IDreamPointerEnterExitInterface::Execute_OnPointerExit(Interaction, Pointer));

	// Re-entering has to work as well, because the arbitration the handler skips here is what would
	// otherwise have left the component believing it still held a cursor it never had.
	TestFalse(TEXT("and the same component can be hovered again afterwards"),
		IDreamPointerEnterExitInterface::Execute_OnPointerEnter(Interaction, Pointer));

	// The claim that ties both halves of the fix together: creating the manager IS enrolling in it.
	// A component with no virtual user has no claim on a shared cursor to arbitrate, so it must not
	// bring a manager into existence -- and while it did, the first such component to be destroyed
	// found the map empty, concluded nobody was left and destroyed the Instance out from under every
	// component that HAD enrolled, leaving their next hover to dereference a dangling static.
	//
	// Written as "unchanged" rather than "null" on purpose. In this suite nothing ever enrols, so
	// this is null both before and after; comparing against what was there keeps the assertion
	// honest in a process where something else did.
	TestTrue(TEXT("hovering a component that never enrolled conjures no manager"),
		UDreamUMGWidgetInteractionManager::Instance == ManagerBefore);

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisiblePixelRaycastGateTest,
	"DreamGUI.Interaction.VisiblePixel.AVisualItCannotReadPixelsFromIsAMissRatherThanAGuess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisiblePixelRaycastGateTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	UDreamVisualCustomRaycast_VisiblePixel* PixelRaycast =
		NewObject<UDreamVisualCustomRaycast_VisiblePixel>(GetTransientPackage());

	// Sentinels, so a "miss" that quietly wrote something can be told from one that wrote nothing.
	// The caller only reads these on a true return, but a false return that scribbled would show up
	// later as a hit point belonging to the wrong element.
	const FVector Sentinel(-777.0, -777.0, -777.0);
	FVector HitPoint = Sentinel;
	FVector HitNormal = Sentinel;
	const FVector RayStart(-100.0, 0.0, 0.0);
	const FVector RayEnd(100.0, 0.0, 0.0);

	// Nothing to test against. A custom raycast is a UObject an author assigns in a Details panel
	// and it is called from the raycast loop, so it is asked about whatever visual it is attached
	// to -- including, on a half-configured widget, none.
	TestFalse(TEXT("a null visual is a miss"), PixelRaycast->Raycast(nullptr, RayStart, RayEnd, HitPoint, HitNormal));
	TestEqual(TEXT("...and nothing was written to the hit point"), HitPoint, Sentinel);
	TestEqual(TEXT("...nor to the hit normal"), HitNormal, Sentinel);

	// A visual of a type whose pixels are not readable. This is the documented fallback -- text with
	// a dynamic font, an atlas-packed sprite, a post-process element -- and getting it wrong is not
	// a crash, it is a UI element that either cannot be clicked at all or can be clicked through its
	// transparent parts. The class default object is used rather than a constructed one because the
	// entire decision under test is the type check; nothing is asked of the visual itself.
	TestFalse(TEXT("a visual that is not a batch mesh is a miss"),
		PixelRaycast->Raycast(GetDefault<UDreamVisualDirectMesh>(), RayStart, RayEnd, HitPoint, HitNormal));
	TestEqual(TEXT("...and still nothing was written"), HitPoint, Sentinel);

	// The right kind of visual with nothing built into it yet. A widget is in this state for the
	// whole window between being constructed and being arranged, and a pointer can arrive inside it.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Face"));
	if (!TestNotNull(TEXT("a widget to carry a visual"), Widget))
	{
		return false;
	}
	Tree->RootWidget = Widget;
	Widget->SetWidth(200.0f);
	Widget->SetHeight(100.0f);
	UDreamVisual* Empty = Widget->CreateNewVisual(UDreamVisualEmpty::StaticClass());
	if (!TestNotNull(TEXT("the widget has a batch-mesh visual"), Empty))
	{
		return false;
	}
	TestFalse(TEXT("a batch mesh with no geometry to intersect is a miss"),
		PixelRaycast->Raycast(Empty, RayStart, RayEnd, HitPoint, HitNormal));
	TestEqual(TEXT("...and still nothing was written"), HitPoint, Sentinel);

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisiblePixelStaticNullVisualTest,
	"DreamGUI.Interaction.VisiblePixel.TheSharedPixelReadTreatsANullVisualAsInputRatherThanAsAMistake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisiblePixelStaticNullVisualTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// The three misses in the test above all enter through UDreamVisualCustomRaycast_VisiblePixel,
	// which Casts before it reads and so can never hand a null down. This is the OTHER door into the
	// same arithmetic and it has no such guard in front of it: GetRaycastPixelFromUIBatchMeshVisual
	// is static and BlueprintCallable, so a Blueprint deriving from UDreamVisualCustomRaycast calls
	// it straight from its graph -- and an object pin nobody connected is a null. That makes null a
	// legal INPUT to this function rather than a caller's mistake, and the crash it used to produce
	// took the editor down with no node named anywhere in it.
	const FVector Sentinel(-777.0, -777.0, -777.0);
	const FVector2D UVSentinel(-777.0, -777.0);
	const FColor PixelSentinel(1, 2, 3, 4);
	FVector2D UV = UVSentinel;
	FColor Pixel = PixelSentinel;
	FVector HitPoint = Sentinel;
	FVector HitNormal = Sentinel;

	TestFalse(TEXT("a null visual is a miss"),
		FPixelReadAccess::GetRaycastPixelFromUIBatchMeshVisual(
			nullptr, FVector(-100.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0), UV, Pixel, HitPoint, HitNormal));

	// Four sentinels rather than one, because a miss that scribbled through an out parameter is
	// worse than a crash. The caller only reads these on a true return, so a stray write does not
	// show up here -- it shows up later, as a hit point or a pixel belonging to some other element.
	TestEqual(TEXT("...and nothing was written to the UV"), UV, UVSentinel);
	TestEqual(TEXT("...nor to the pixel"), Pixel, PixelSentinel);
	TestEqual(TEXT("...nor to the hit point"), HitPoint, Sentinel);
	TestEqual(TEXT("...nor to the hit normal"), HitNormal, Sentinel);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisiblePixelRaycastSettingsTest,
	"DreamGUI.Interaction.VisiblePixel.TheChannelAndThresholdAreStoredExactlyAsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisiblePixelRaycastSettingsTest::RunTest(const FString& Parameters)
{
	UDreamVisualCustomRaycast_VisiblePixel* PixelRaycast =
		NewObject<UDreamVisualCustomRaycast_VisiblePixel>(GetTransientPackage());

	// The defaults are load-bearing: alpha, and a threshold low enough that a faint edge still
	// counts. An author who drops this component on a sprite and changes nothing gets "click the
	// visible pixels", which is the entire reason the class exists.
	TestEqual(TEXT("the default channel is alpha"), (int32)PixelRaycast->GetPixelChannel(), 3);
	TestEqual(TEXT("the default threshold is a faint one"), PixelRaycast->GetVisibilityThreshold(), 0.1f);

	PixelRaycast->SetPixelChannel(0);
	TestEqual(TEXT("the red channel can be chosen"), (int32)PixelRaycast->GetPixelChannel(), 0);
	PixelRaycast->SetVisibilityThreshold(0.5f);
	TestEqual(TEXT("and a stricter threshold"), PixelRaycast->GetVisibilityThreshold(), 0.5f);

	// Neither setter clamps, and neither should be assumed to. The channel is documented as "0123 as
	// rgba" and there is no fifth answer, but the switch that reads it has red on its default label
	// -- so an out-of-range channel silently tests red rather than failing or falling back to alpha.
	// The value is stored verbatim, which is what lets a reader see what was authored rather than
	// what was survivable; anyone adding a clamp should expect this to fail and should say so.
	PixelRaycast->SetPixelChannel(9);
	TestEqual(TEXT("an out-of-range channel is stored as authored rather than clamped"),
		(int32)PixelRaycast->GetPixelChannel(), 9);

	// The threshold's UIMin/UIMax are Details-panel hints, not constraints. A threshold above 1
	// means nothing can ever pass -- a deliberate way to disable a raycast without removing it --
	// and a negative one means everything passes including fully transparent pixels.
	PixelRaycast->SetVisibilityThreshold(2.0f);
	TestEqual(TEXT("a threshold past one is stored rather than clamped"),
		PixelRaycast->GetVisibilityThreshold(), 2.0f);
	PixelRaycast->SetVisibilityThreshold(-1.0f);
	TestEqual(TEXT("and so is a negative one"), PixelRaycast->GetVisibilityThreshold(), -1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationSelectionGuardTest,
	"DreamGUI.Interaction.NavigationSelection.AHandlerWithNothingToActOnStopsBeforeItAnimates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationSelectionGuardTest::RunTest(const FString& Parameters)
{
	using namespace DreamInteractionExtensionTestLocal;

	// The selection cursor -- the highlight that flies between the widget a gamepad has focused and
	// the next one. Everything it does past its guards goes through the tween manager, so what is
	// pinned here is the guards: the two states in which it must decline, and the fact that
	// declining leaves no half-started animation behind.

	// First, the gate that decides whether any of this runs natively at all. A Blueprint subclass
	// gets the whole thing routed to a BlueprintImplementableEvent instead, so a native fixture
	// asserting on native behaviour has to establish that it is on the native side of that fork --
	// otherwise every assertion below would be describing an empty event.
	UUINavigationInputSelectionHandler* Orphan =
		NewObject<UUINavigationInputSelectionHandler>(GetTransientPackage());
	TestTrue(TEXT("a C++ handler takes the native path rather than the Blueprint event"),
		Orphan->GetClass()->HasAnyClassFlags(CLASS_Native)
		&& !Orphan->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint));

	// A behaviour with no owning widget. This is not contrived: the handler is spawned from a class
	// reference on the presenter, and a reference that has been retargeted or a spawn that failed
	// leaves exactly this. Both entry points have to survive it, because the first thing either
	// would otherwise do is reparent a null widget.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Target = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Target"));
	if (!TestNotNull(TEXT("a widget for the cursor to select"), Target))
	{
		return false;
	}
	Tree->RootWidget = Target;
	Target->SetWidth(200.0f);
	Target->SetHeight(100.0f);
	TestNull(TEXT("the orphan handler really has no widget"), Orphan->GetWidget());
	Orphan->SelectWidget(Target);
	Orphan->SelectNone();
	TestEqual(TEXT("an orphan handler records no tweener for either call"),
		ArrayNum(*this, Orphan, TEXT("TweenerCollection")), 0);

	// Now a handler that does have a widget, and the guard that matters most. SelectNone returns
	// early when nothing is selected, and that early return is the only thing standing between an
	// idle cursor and the fade-out path -- which chains ->OnComplete directly off RenderOpacityTo's
	// return value. Deselecting twice, or deselecting on a screen that never selected anything, is
	// completely ordinary input.
	UDreamWidget* CursorWidget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Cursor"));
	UUINavigationInputSelectionHandler* Cursor =
		CursorWidget->AddComponent<UUINavigationInputSelectionHandler>();
	if (!TestNotNull(TEXT("the cursor widget carries a selection handler"), Cursor))
	{
		return false;
	}
	TWeakObjectPtr<UDreamWidget>* CurrentSelected =
		FieldPtr<TWeakObjectPtr<UDreamWidget>>(*this, Cursor, TEXT("CurrentSelected"));
	if (CurrentSelected == nullptr)
	{
		return false;
	}
	TestFalse(TEXT("a fresh handler has nothing selected"), CurrentSelected->IsValid());

	Cursor->SelectNone();
	TestFalse(TEXT("deselecting when nothing is selected leaves it that way"), CurrentSelected->IsValid());
	TestEqual(TEXT("...and starts no animation"),
		ArrayNum(*this, Cursor, TEXT("TweenerCollection")), 0);

	// Selecting nothing, from nothing. All three of SelectWidget's branches ask for either an
	// incoming selection or a previous one, so with neither in hand every branch declines and the
	// call is a no-op apart from clearing the record. A fourth branch that "helpfully" did something
	// here would be animating a cursor between two widgets that do not exist.
	Cursor->SelectWidget(nullptr);
	TestFalse(TEXT("selecting nothing from nothing selects nothing"), CurrentSelected->IsValid());
	TestEqual(TEXT("...and starts no animation either"),
		ArrayNum(*this, Cursor, TEXT("TweenerCollection")), 0);
	TestNull(TEXT("and the cursor widget is left unparented rather than attached to nothing"),
		CursorWidget->GetParent());

	CursorWidget->DestroyWidget();
	Target->DestroyWidget();
	return true;
}

#endif
