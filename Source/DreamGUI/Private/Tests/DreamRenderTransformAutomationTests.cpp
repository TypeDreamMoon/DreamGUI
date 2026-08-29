// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

/*
 * The render transform: moving where a widget is DRAWN without telling layout.
 *
 * This exists because RelativeLocation cannot do it. Its setter calls
 * CalculateAnchorFromTransform and then MarkLayoutForRebuild, so animating the position of a widget
 * inside a panel asks the layout to redo itself on every key -- the animation and the layout take
 * turns and the widget never moves. Sliding a panel child in from the right, which is an ordinary
 * thing to want, was impossible without opting the widget out of layout entirely.
 *
 * So the invariant that matters is not "the widget moved". It is "the widget moved AND layout did
 * not notice": same authored position, same anchors, same arrangement, siblings untouched.
 */

namespace DreamRenderTransformTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformIsInvisibleToLayoutTest,
	"DreamGUI.Widget.RenderTransform.LayoutNeverSeesItAndNeverUndoesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformIsInvisibleToLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTransformTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 400.0f, 400.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Panel, TEXT("First"), 100.0f, 40.0f);
	UDreamWidget* Sliding = MakeWidget(TestWorld.World, Panel, TEXT("Sliding"), 100.0f, 40.0f);
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamWidget::MarkLayoutForRebuild(Panel);
	UDreamWidget::RebuildLayoutImmediately(Panel);

	const FVector LayoutLocationBefore = Sliding->GetRelativeLocation();
	const FVector2D AnchoredPositionBefore = Sliding->GetAnchorData().AnchoredPosition;
	const FTransform LayoutWorldBefore = Sliding->GetLayoutWorldTransform();
	const FTransform SiblingWorldBefore = First->GetWorldTransform();

	TestFalse(TEXT("Nothing is transformed to begin with"), Sliding->HasRenderTransform());

	// Slide it 500 units to the right of wherever the layout put it -- the entrance offset.
	Sliding->SetRenderTranslation(FVector(0.0, 500.0, 0.0));
	// And let the layout run again, which is the step that used to undo everything.
	UDreamWidget::MarkLayoutForRebuild(Panel);
	UDreamWidget::RebuildLayoutImmediately(Panel);

	TestTrue(TEXT("The widget reports a render transform"), Sliding->HasRenderTransform());
	// It is drawn 500 to the right. Local +Y is right, +Z is up, +X is the canvas normal.
	TestTrue(TEXT("It is drawn 500 to the right"),
		FMath::IsNearlyEqual(Sliding->GetWorldTransform().GetLocation().Y,
			LayoutWorldBefore.GetLocation().Y + 500.0, 0.01));

	// ...and every piece of layout state is exactly as it was. These are the assertions that
	// distinguish a render transform from moving the widget.
	TestTrue(TEXT("The authored position is untouched"),
		Sliding->GetRelativeLocation().Equals(LayoutLocationBefore, 0.001));
	TestTrue(TEXT("The anchors are untouched"),
		Sliding->GetAnchorData().AnchoredPosition.Equals(AnchoredPositionBefore, 0.001));
	TestTrue(TEXT("Layout still believes it is where it put it"),
		Sliding->GetLayoutWorldTransform().GetLocation().Equals(LayoutWorldBefore.GetLocation(), 0.001));
	TestTrue(TEXT("The sibling did not shift to make room"),
		First->GetWorldTransform().GetLocation().Equals(SiblingWorldBefore.GetLocation(), 0.001));

	// Clearing puts it back exactly, with no residue.
	Sliding->ClearRenderTransform();
	TestFalse(TEXT("Cleared"), Sliding->HasRenderTransform());
	TestTrue(TEXT("Drawn where layout put it again"),
		Sliding->GetWorldTransform().GetLocation().Equals(LayoutWorldBefore.GetLocation(), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformPropagatesTest,
	"DreamGUI.Widget.RenderTransform.MovesTheWholeSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformPropagatesTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTransformTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 400.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 40.0f);
	UDreamWidget* Grandchild = MakeWidget(TestWorld.World, Child, TEXT("Grandchild"), 50.0f, 20.0f);

	const FVector ChildBefore = Child->GetWorldTransform().GetLocation();
	const FVector GrandchildBefore = Grandchild->GetWorldTransform().GetLocation();

	// A card sliding in carries its label with it; anything else would be useless for animation.
	Root->SetRenderTranslation(FVector(0.0, 120.0, 0.0));

	TestTrue(TEXT("The child came along"),
		FMath::IsNearlyEqual(Child->GetWorldTransform().GetLocation().Y, ChildBefore.Y + 120.0, 0.01));
	TestTrue(TEXT("And so did the grandchild"),
		FMath::IsNearlyEqual(Grandchild->GetWorldTransform().GetLocation().Y, GrandchildBefore.Y + 120.0, 0.01));
	// The descendants are moved without being transformed themselves -- they inherit through the
	// parent's world transform, which is what keeps this to one property on one widget.
	TestFalse(TEXT("The child has no render transform of its own"), Child->HasRenderTransform());
	TestTrue(TEXT("And layout still believes the child is where it put it"),
		Child->GetLayoutWorldTransform().GetLocation().Equals(ChildBefore, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformPivotTest,
	"DreamGUI.Widget.RenderTransform.ScaleAndAngleTurnAboutThePivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformPivotTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTransformTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 400.0f);
	UDreamWidget* Popping = MakeWidget(TestWorld.World, Root, TEXT("Popping"), 100.0f, 40.0f);
	// A child at the widget's own right edge, so a scale about the centre visibly moves it.
	UDreamWidget* Edge = MakeWidget(TestWorld.World, Popping, TEXT("Edge"), 10.0f, 10.0f);
	Edge->SetRelativeLocation(FVector(0.0, 50.0, 0.0));

	const FVector PoppingCentre = Popping->GetWorldTransform().GetLocation();
	const FVector EdgeBefore = Edge->GetWorldTransform().GetLocation();

	// Default pivot is the middle, so a pop-in scale must leave the widget's own centre put.
	Popping->SetRenderScale(FVector(1.0, 2.0, 2.0));
	TestTrue(TEXT("Scaling about the centre does not move the centre"),
		Popping->GetWorldTransform().GetLocation().Equals(PoppingCentre, 0.001));
	TestTrue(TEXT("But it does move what sits at the edge"),
		FMath::IsNearlyEqual(Edge->GetWorldTransform().GetLocation().Y,
			PoppingCentre.Y + (EdgeBefore.Y - PoppingCentre.Y) * 2.0, 0.01));

	// Move the pivot to the left edge and the same scale grows rightwards instead.
	Popping->SetRenderTransformPivot(FVector2D(0.0, 0.5));
	const double EdgeFromLeftPivot = Edge->GetWorldTransform().GetLocation().Y;
	TestTrue(TEXT("A different pivot gives a different result"),
		!FMath::IsNearlyEqual(EdgeFromLeftPivot,
			PoppingCentre.Y + (EdgeBefore.Y - PoppingCentre.Y) * 2.0, 0.01));

	// Rotation is roll about the canvas normal, the only rotation that stays on the batched 2D path.
	Popping->SetRenderScale(FVector::OneVector);
	Popping->SetRenderTransformPivot(FVector2D(0.5, 0.5));
	// Roll is the in-plane rotation, about the canvas normal: a child sitting to the right ends up
	// directly above or below, depending on which way round the convention goes. Asserted without a
	// sign so the test is about the rotation happening, not about UE's rotator handedness.
	const double EdgeArm = EdgeBefore.Y - PoppingCentre.Y;
	Popping->SetRenderRotation(FRotator(0.0, 0.0, 90.0));
	TestTrue(TEXT("A quarter roll takes the edge child off the horizontal"),
		FMath::IsNearlyZero(Edge->GetWorldTransform().GetLocation().Y - PoppingCentre.Y, 0.01));
	TestTrue(TEXT("...and puts it the same distance away vertically"),
		FMath::IsNearlyEqual(FMath::Abs(Edge->GetWorldTransform().GetLocation().Z - PoppingCentre.Z),
			FMath::Abs(EdgeArm), 0.01));
	TestTrue(TEXT("Layout is still oblivious"),
		Popping->GetLayoutWorldTransform().GetLocation().Equals(PoppingCentre, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformDoesNotLeakOnReparentTest,
	"DreamGUI.Widget.RenderTransform.NeverBakesItselfIntoAuthoredData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformDoesNotLeakOnReparentTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTransformTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Origin = MakeWidget(TestWorld.World, nullptr, TEXT("Origin"), 400.0f, 400.0f);
	UDreamWidget* Animating = MakeWidget(TestWorld.World, Origin, TEXT("Animating"), 200.0f, 200.0f);
	UDreamWidget* Passenger = MakeWidget(TestWorld.World, Animating, TEXT("Passenger"), 50.0f, 50.0f);
	UDreamWidget* Destination = MakeWidget(TestWorld.World, nullptr, TEXT("Destination"), 400.0f, 400.0f);

	// An ancestor is part-way through a slide-in. Anything that converts a world transform back into
	// authored data now has two candidate answers, and only one of them is layout's.
	Animating->SetRenderTranslation(FVector(0.0, 300.0, 0.0));
	const FVector PassengerAuthoredBefore = Passenger->GetRelativeLocation();

	TestTrue(TEXT("Reparenting keeping world position succeeds"),
		Passenger->TrySetParent(Destination, /*InKeepWorldPosition*/true));

	// If the reparent had used the drawn transform, the ancestor's 300-unit animation offset would
	// now be sitting in the passenger's authored position -- and would be saved into the prefab.
	TestTrue(TEXT("The passenger's authored position did not absorb the animation offset"),
		FMath::Abs(Passenger->GetRelativeLocation().Y - PassengerAuthoredBefore.Y) < 299.0);

	// SetWorldTransform is the other conversion back into authored data.
	UDreamWidget* Direct = MakeWidget(TestWorld.World, Animating, TEXT("Direct"), 50.0f, 50.0f);
	const FTransform LayoutWorld = Direct->GetLayoutWorldTransform();
	Direct->SetWorldTransform(LayoutWorld);
	TestTrue(TEXT("Round-tripping through the layout world transform is a no-op on authored data"),
		FMath::IsNearlyEqual(Direct->GetRelativeLocation().Y, 0.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformIsKeyableTest,
	"DreamGUI.Widget.RenderTransform.TheAnimatableChannelsAreAnimatable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformIsKeyableTest::RunTest(const FString& Parameters)
{
	// Sequencer only offers a property track for a UPROPERTY marked Interp. Losing that specifier
	// would not break a build or any behaviour test -- the feature would simply stop appearing in
	// the animation track list, which is the only place it is meant to be used from.
	auto HasInterp = [this](const TCHAR* PropertyName)
	{
		const FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			AddError(FString::Printf(TEXT("No property named %s on UDreamWidget."), PropertyName));
			return false;
		}
		return Property->HasAnyPropertyFlags(CPF_Interp);
	};

	TestTrue(TEXT("Translation can be keyed"), HasInterp(TEXT("RenderTranslation")));
	TestTrue(TEXT("Scale can be keyed"), HasInterp(TEXT("RenderScale")));
	TestTrue(TEXT("Rotation can be keyed"), HasInterp(TEXT("RenderRotation")));
	// The pivot is a setting, not a channel -- UMG does not animate it either, and a keyable pivot
	// invites animations that drift because two curves are fighting over the same visual result.
	TestFalse(TEXT("The pivot is deliberately not keyable"), HasInterp(TEXT("RenderTransformPivot")));

	// The types have to be ones Sequencer has tracks for. FVector reaches
	// UMovieSceneDoubleVectorTrack as three channels; FRotator has its own track, which is the
	// entire reason RelativeRotationEuler exists alongside the FQuat it mirrors.
	auto StructNameOf = [](const TCHAR* PropertyName)
	{
		const FStructProperty* AsStruct = CastField<const FStructProperty>(
			UDreamWidget::StaticClass()->FindPropertyByName(FName(PropertyName)));
		return AsStruct ? AsStruct->Struct->GetFName() : NAME_None;
	};
	TestEqual(TEXT("Translation is an FVector"), StructNameOf(TEXT("RenderTranslation")), NAME_Vector);
	TestEqual(TEXT("Scale is an FVector"), StructNameOf(TEXT("RenderScale")), NAME_Vector);
	TestEqual(TEXT("Rotation is an FRotator, not the FQuat Sequencer cannot key"),
		StructNameOf(TEXT("RenderRotation")), NAME_Rotator);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformFlipTest,
	"DreamGUI.Widget.RenderTransform.AWidgetCanBeFlippedInThreeDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformFlipTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTransformTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 400.0f, 400.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Panel, TEXT("Card"), 100.0f, 140.0f);
	UDreamWidget* Corner = MakeWidget(TestWorld.World, Card, TEXT("Corner"), 10.0f, 10.0f);
	Corner->SetRelativeLocation(FVector(0.0, 40.0, 0.0));
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamWidget::MarkLayoutForRebuild(Panel);
	UDreamWidget::RebuildLayoutImmediately(Panel);

	const FVector CardCentre = Card->GetWorldTransform().GetLocation();
	const FTransform CardLayoutWorld = Card->GetLayoutWorldTransform();
	const double CornerArm = Corner->GetWorldTransform().GetLocation().Y - CardCentre.Y;

	// A card turning over about its vertical axis. This is the case a two-dimensional render
	// transform cannot express at all, and the reason this one is not two-dimensional: the authored
	// transform on the same widget is already an FVector and an FQuat, so a render transform that
	// only did roll would have been narrower than the thing it mirrors.
	Card->SetRenderRotation(FRotator(0.0, 180.0, 0.0));

	TestTrue(TEXT("The card is flipped"), Card->HasRenderTransform());
	// Half a turn about the vertical mirrors everything across the card's own centre.
	TestTrue(TEXT("What was on the right is now the same distance to the left"),
		FMath::IsNearlyEqual(Corner->GetWorldTransform().GetLocation().Y - CardCentre.Y, -CornerArm, 0.01));
	TestTrue(TEXT("The card's own centre did not move"),
		Card->GetWorldTransform().GetLocation().Equals(CardCentre, 0.01));

	// And the point of all of it: the layout has no idea any of this happened.
	UDreamWidget::MarkLayoutForRebuild(Panel);
	UDreamWidget::RebuildLayoutImmediately(Panel);
	TestTrue(TEXT("Layout still places the card where it always did"),
		Card->GetLayoutWorldTransform().GetLocation().Equals(CardLayoutWorld.GetLocation(), 0.001));
	TestTrue(TEXT("And the flip survived the layout pass"),
		FMath::IsNearlyEqual(Corner->GetWorldTransform().GetLocation().Y - CardCentre.Y, -CornerArm, 0.01));

	// The third axis. Depth is meaningless on a screen-space overlay but not on a world-space
	// canvas, where lifting a card towards the viewer is an ordinary effect -- and it is the
	// component a translation modelled on UMG's two-dimensional one would silently drop.
	Card->SetRenderRotation(FRotator::ZeroRotator);
	Card->SetRenderTranslation(FVector(25.0, 0.0, 0.0));
	TestTrue(TEXT("A depth translation lifts the card off the canvas plane"),
		FMath::IsNearlyEqual(Card->GetWorldTransform().GetLocation().X, CardCentre.X + 25.0, 0.01));
	TestTrue(TEXT("...carrying its children with it"),
		FMath::IsNearlyEqual(Corner->GetWorldTransform().GetLocation().X, CardCentre.X + 25.0, 0.01));
	TestTrue(TEXT("...and layout still does not care"),
		Card->GetLayoutWorldTransform().GetLocation().Equals(CardLayoutWorld.GetLocation(), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformSurvivesLoadTest,
	"DreamGUI.Widget.RenderTransform.ASavedTransformTakesEffectAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformSurvivesLoadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTransformTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->OnRegister();

	// The exact sequence the prefab loader runs: create the object, write the property into memory
	// with no setter and no notification, then register. Every behaviour test that calls the setter
	// is blind to this path, which is how "works in the designer, dead in PIE" shipped.
	UDreamWidget* Loaded = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Loaded->SetDisplayName(TEXT("Loaded"));
	Loaded->SetWidth(100.0f);
	Loaded->SetHeight(100.0f);
	FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(TEXT("RenderTranslation"));
	if (!TestNotNull(TEXT("RenderTranslation exists"), Property))return false;
	*Property->ContainerPtrToValuePtr<FVector>(Loaded) = FVector(0.0, 300.0, 0.0);

	Loaded->OnRegister();
	TestTrue(TEXT("Registration notices the deserialized value"), Loaded->HasRenderTransform());

	Root->AddChild(Loaded);
	TestTrue(TEXT("...and the widget is drawn where the saved transform says"),
		FMath::IsNearlyEqual(Loaded->GetWorldTransform().GetLocation().Y, 300.0, 0.01));
	return true;
}

#endif
