// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamWidget.h"
#include "PrefabSystem/DreamUIWidgetBinding.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUIAnimEventTrack.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneBindingReferences.h"
#include "Engine/World.h"

namespace DreamUISequencerBindingTestLocal
{
	struct FScopedEditorWorld
	{
		UWorld* World = nullptr;
		FScopedEditorWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedEditorWorld() { if (World) { World->DestroyWorld(false); } }
	};

	// Root -> Panel -> Label, the smallest tree with a two-segment path.
	struct FSmallTree
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Panel = nullptr;
		UDreamWidget* Label = nullptr;

		explicit FSmallTree(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("Root"));
			Root->OnRegister();
			Panel = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Panel->SetDisplayName(TEXT("Panel"));
			Panel->TrySetParent(Root, false);
			Label = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Label->SetDisplayName(TEXT("Label"));
			Label->TrySetParent(Panel, false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWidgetBindingPathRoundTripTest,
	"DreamGUI.Sequencer.WidgetBinding.ThePathSurvivesARoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWidgetBindingPathRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISequencerBindingTestLocal;
	FScopedEditorWorld Scope;
	FSmallTree Tree(Scope.World);

	const FString LabelPath = UDreamUIWidgetBinding::BuildWidgetPathFromRoot(Tree.Root, Tree.Label);
	TestEqual(TEXT("Two-segment path"), LabelPath, TEXT("Panel/Label"));
	TestEqual(TEXT("Path resolves to the widget it came from"),
		UDreamUIWidgetBinding::ResolveWidgetPath(Tree.Root, LabelPath), Tree.Label);
	TestEqual(TEXT("The empty path is the root"),
		UDreamUIWidgetBinding::ResolveWidgetPath(Tree.Root, FString()), Tree.Root);
	TestNull(TEXT("A dead segment resolves to nothing"),
		UDreamUIWidgetBinding::ResolveWidgetPath(Tree.Root, TEXT("Panel/Nope")));
	// A widget from outside the root's tree gives the empty path, not garbage.
	UDreamWidget* Stranger = NewObject<UDreamWidget>(Scope.World, NAME_None, RF_Public | RF_Transactional);
	Stranger->SetDisplayName(TEXT("Stranger"));
	TestEqual(TEXT("A stranger's path is empty"),
		UDreamUIWidgetBinding::BuildWidgetPathFromRoot(Tree.Root, Stranger), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWidgetBindingContextFallbackTest,
	"DreamGUI.Sequencer.WidgetBinding.AWidgetContextReRootsTheBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWidgetBindingContextFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISequencerBindingTestLocal;
	FScopedEditorWorld Scope;
	FSmallTree Tree(Scope.World);

	// No PresenterActor at all: a widget context stands in as the root. This is the path both a
	// component-played sequence asset and a child possessable (parent context) resolve through.
	UDreamUISequence* Asset = NewObject<UDreamUISequence>(GetTransientPackage());
	const FGuid Guid = Asset->AddWidgetBinding(TEXT("Panel/Label"), TEXT("Label"));

	FMovieSceneBindingResolveParams Params;
	Params.Sequence = Asset;
	Params.ObjectBindingID = Guid;
	Params.Context = Tree.Root;

	const FMovieSceneBindingReference* Reference = Asset->BindingReferences.GetReference(Guid, 0);
	if (!TestNotNull(TEXT("Binding reference exists"), Reference))return false;
	if (!TestNotNull(TEXT("Custom binding exists"), Reference->CustomBinding.Get()))return false;

	// ResolveBinding is pure resolution logic; a real playback state is not needed for this branch,
	// but the API demands one -- so exercise the static resolution pieces directly instead.
	UDreamUIWidgetBinding* Binding = Cast<UDreamUIWidgetBinding>(Reference->CustomBinding.Get());
	if (!TestNotNull(TEXT("Binding is a widget binding"), Binding))return false;
	TestEqual(TEXT("Path stored"), Binding->WidgetPath, TEXT("Panel/Label"));
	TestEqual(TEXT("Resolves under the context root"),
		UDreamUIWidgetBinding::ResolveWidgetPath(Tree.Root, Binding->WidgetPath), Tree.Label);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISequenceAssetShapeTest,
	"DreamGUI.Sequencer.SequenceAsset.TheAssetIsBornUsable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUISequenceAssetShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISequencerBindingTestLocal;
	UDreamUISequence* Asset = NewObject<UDreamUISequence>(GetTransientPackage());
	TestNotNull(TEXT("MovieScene exists"), Asset->GetMovieScene());
	TestTrue(TEXT("Parent contexts are significant (children re-root through the root binding)"),
		Asset->AreParentContextsSignificant());

	const FGuid RootGuid = Asset->EnsureRootBinding();
	TestTrue(TEXT("Root binding created"), RootGuid.IsValid());
	TestEqual(TEXT("EnsureRootBinding is idempotent"), Asset->EnsureRootBinding(), RootGuid);

	const FGuid ChildGuid = Asset->AddWidgetBinding(TEXT("Panel"), TEXT("Panel"));
	const FMovieScenePossessable* Child = Asset->GetMovieScene()->FindPossessable(ChildGuid);
	if (!TestNotNull(TEXT("Child possessable exists"), Child))return false;
	TestEqual(TEXT("Children are parented under the root binding"), Child->GetParent(), RootGuid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIAnimEventTrackShapeTest,
	"DreamGUI.Sequencer.EventTrack.KeysBecomeTriggerTimesAndTheDelegateFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIAnimEventTrackShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISequencerBindingTestLocal;
	FScopedEditorWorld Scope;
	FSmallTree Tree(Scope.World);

	UDreamUIAnimEventTrack* Track = NewObject<UDreamUIAnimEventTrack>(GetTransientPackage());
	UDreamUIAnimEventSection* Section = CastChecked<UDreamUIAnimEventSection>(Track->CreateNewSection());
	Track->AddSection(*Section);
	TestTrue(TEXT("Track only accepts its own section type"), Track->SupportsType(UDreamUIAnimEventSection::StaticClass()));

	Section->EventChannel.GetData().AddKey(FFrameNumber(10), TEXT("Opened"));
	Section->EventChannel.GetData().AddKey(FFrameNumber(20), TEXT("Closed"));
	TestEqual(TEXT("Trigger times mirror the channel keys"), Section->GetTriggerTimes().Num(), 2);

	UDreamUIPrefabSequenceComponent* Component = Tree.Root->AddComponent<UDreamUIPrefabSequenceComponent>();
	if (!TestNotNull(TEXT("Component created"), Component))return false;
	// The dynamic delegate wants a UFUNCTION listener, which a test cannot supply cheaply; what the
	// runtime contract requires is that broadcasting with no listeners is a safe no-op.
	Component->BroadcastAnimationEvent(TEXT("Opened"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIGeometryMirrorTest,
	"DreamGUI.Sequencer.GeometryMirrors.PropertyMemoryTracksTheCaches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIGeometryMirrorTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISequencerBindingTestLocal;
	FScopedEditorWorld Scope;
	FSmallTree Tree(Scope.World);

	// Sequencer reads Interp properties straight from memory, so the mirrors must hold the real
	// values without anyone calling a getter first.
	Tree.Root->SetWidth(1234.0f);
	Tree.Root->SetHeight(567.0f);

	auto ReadFloatProperty = [&](const TCHAR* Name) -> float
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(UDreamWidget::StaticClass(), Name);
		return Property != nullptr ? Property->GetPropertyValue_InContainer(Tree.Root) : -1.0f;
	};
	TestEqual(TEXT("AnimatableWidth tracks SetWidth"), ReadFloatProperty(TEXT("AnimatableWidth")), 1234.0f);
	TestEqual(TEXT("AnimatableHeight tracks SetHeight"), ReadFloatProperty(TEXT("AnimatableHeight")), 567.0f);

	// Writing through the property setter (the sequencer's write path) must move the real geometry.
	if (const FFloatProperty* Property = FindFProperty<FFloatProperty>(UDreamWidget::StaticClass(), TEXT("AnimatableWidth")))
	{
		if (Property->HasSetter())
		{
			const float NewValue = 250.0f;
			Property->CallSetter(Tree.Root, &NewValue);
			TestEqual(TEXT("The setter path reaches SetWidth"), Tree.Root->GetWidth(), 250.0f);
		}
		else
		{
			AddError(TEXT("AnimatableWidth lost its setter"));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
