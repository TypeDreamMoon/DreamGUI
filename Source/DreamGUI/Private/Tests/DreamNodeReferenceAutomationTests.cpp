// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/World.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UISlider.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * `Prop = SomeNode`: one node of a .dui file pointing at another one.
 *
 * The feature has two halves that fail in completely different places, and a suite that covered only
 * the first would report a green build for a screen that is visibly dead in a packaged game:
 *
 *   1. THE BUILD. An object-typed property whose PropertyClass derives from UDreamWidget or
 *      UDreamVisual reads a non-'/' value as the id of a node in the same file. The lookup is
 *      DEFERRED to a pass that runs after the whole tree exists, because the motivating case -- a
 *      toggle at the top of a file naming the check mark nested below it -- is a reference to a node
 *      that has not been built yet at the moment the line is read. Both directions are therefore
 *      pinned below, in one tree, because "works downward only" is exactly the shape a non-deferred
 *      implementation has and it is invisible in any file whose targets happen to come first.
 *
 *   2. THE INSTANCE. What the builder writes lands in the class TEMPLATE. FObjectInstancingGraph
 *      re-aims only properties carrying CPF_InstancedReference, and none of these do -- the real
 *      targets are all TWeakObjectPtr -- so without a retargeting pass every instance of the screen
 *      holds the ARCHETYPE's object: one object shared by every instance and never drawn. The last
 *      test here is about that and nothing else. It goes through the real entry point rather than
 *      calling the pass, which is file-local to DreamWidgetGeneratedClass.cpp; the observable claim
 *      is "the pointer lands inside the instanced tree", which is meaningful precisely because
 *      widgets are outered flat to their tree (UDreamWidgetTree::ConstructWidget) and IsIn is
 *      therefore an exact statement of which tree an object belongs to.
 *
 * The property kind the builder branches on is FObjectPropertyBase, and TObjectPtr / TWeakObjectPtr
 * are SIBLINGS under it rather than a chain -- the narrower FObjectProperty cast is what silently
 * excluded every weak reference in this library. So the fixtures here are deliberately built on the
 * weak half: UUISelectable::TransitionTarget (TWeakObjectPtr<UDreamVisual>) and UUISlider::Fill
 * (TWeakObjectPtr<UDreamWidget>). A test written on a TObjectPtr would pass over the defect.
 *
 * Fixtures are hand-built FDreamUIAst rather than parsed .dui text, matching
 * DreamUITextBuilderAutomationTests: a red test here can then only mean the builder is wrong.
 * Assertions are on diagnostic CODES, never on message text -- the codes are the stable half of the
 * contract (see DreamUIDiagnostics.h). The diagnostic bag records; it does not UE_LOG, so a test
 * that asserts on a refusal needs no AddExpectedError -- and adding one would fail, having matched
 * nothing. Nothing here calls CollectGarbage; TStrongObjectPtr is what keeps a tree alive.
 */

namespace DreamNodeReferenceTestLocal
{
	FDreamUISourceLocation At(int32 InLine = 1)
	{
		// Non-zero so FDreamUISourceLocation::IsValid holds and a diagnostic raised against a
		// hand-built node prints the way one raised from a real file does.
		return FDreamUISourceLocation(InLine, 1);
	}

	FDreamUIValue Literal(EDreamUIValueKind InKind, const FString& InRaw)
	{
		FDreamUIValue Value;
		Value.Kind = InKind;
		Value.Raw = InRaw;
		Value.Location = At();
		return Value;
	}

	FDreamUIProperty Assign(const FString& InName, FDreamUIValue InValue)
	{
		FDreamUIProperty Property;
		Property.Name = InName;
		Property.Value = MoveTemp(InValue);
		Property.Location = At();
		return Property;
	}

	/**
	 * `Prop = SomeNode` -- a bare word, which is what the parser produces for an unquoted name and
	 * what the builder tells apart from an asset path by the absence of a leading '/'.
	 */
	FDreamUIProperty AssignNodeId(const FString& InName, const FString& InNodeId)
	{
		return Assign(InName, Literal(EDreamUIValueKind::Identifier, InNodeId));
	}

	FDreamUINode MakeNode(const FString& InTypeName, const FString& InId)
	{
		FDreamUINode Node;
		Node.Kind = EDreamUINodeKind::Widget;
		Node.TypeName = InTypeName;
		Node.Id = InId;
		Node.Location = At();
		return Node;
	}

	FDreamUIComponent MakeComponent(const FString& InClassName)
	{
		FDreamUIComponent Component;
		Component.ClassName = InClassName;
		Component.Location = At();
		return Component;
	}

	/** A node whose single `+ Behaviour` carries the node-reference line. */
	FDreamUINode MakeNodeCarrying(const FString& InId, FDreamUIComponent InComponent)
	{
		FDreamUINode Node = MakeNode(TEXT("Widget"), InId);
		Node.Components.Add(MoveTemp(InComponent));
		return Node;
	}

	FDreamUIAst AstWith(FDreamUINode InRoot)
	{
		FDreamUIAst Ast;
		Ast.ClassPath = TEXT("/Game/UI/WBP_NodeReferenceTest");
		Ast.ClassPathLocation = At();
		Ast.Root = MoveTemp(InRoot);
		Ast.bHasRoot = true;
		return Ast;
	}

	/** The tree plus everything the build said about it, kept alive for the length of one test. */
	struct FBuildOutcome
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		FDreamUIDiagnosticBag Diagnostics;
		TArray<FDreamWidgetPropertyBinding> Bindings;

		UDreamWidget* Find(const TCHAR* InDisplayName) const
		{
			return Tree.IsValid() ? Tree->FindWidgetByVariableName(FName(InDisplayName)) : nullptr;
		}

		bool HasCode(EDreamUIDiagnosticCode InCode) const
		{
			return Diagnostics.Diagnostics.ContainsByPredicate(
				[InCode](const FDreamUIDiagnostic& Diagnostic) { return Diagnostic.Code == InCode; });
		}
	};

	FBuildOutcome BuildFrom(const FDreamUIAst& InAst)
	{
		FBuildOutcome Outcome;
		Outcome.Diagnostics.SourceName = TEXT("NodeReferenceTest.dui");
		// The transient package rather than a world: what the builder produces is a class template,
		// and a tree whose GetWorld() is null is the defining property of one.
		Outcome.Tree.Reset(FDreamUITextBuilder::Build(InAst, GetTransientPackage(), Outcome.Diagnostics, Outcome.Bindings));
		return Outcome;
	}

	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNodeReferenceVisualTypedTest,
	"DreamGUI.Text.NodeReference.AVisualTypedPropertyTakesTheNamedNodesVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNodeReferenceVisualTypedTest::RunTest(const FString& Parameters)
{
	using namespace DreamNodeReferenceTestLocal;

	// Two references at ONE target, from opposite sides of its declaration. 'Ahead' names a node the
	// walk has not reached yet, which is the whole reason resolution is a deferred pass and the case
	// a straight-line implementation gets wrong; 'Behind' names one it has already built, which is
	// the case a deferred pass could break while making the first one work. Both in one tree so the
	// two answers cannot come from two different code paths.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));

	FDreamUIComponent AheadSelectable = MakeComponent(TEXT("Selectable"));
	AheadSelectable.Properties.Add(AssignNodeId(TEXT("TransitionTarget"), TEXT("Glow")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Ahead"), MoveTemp(AheadSelectable)));

	// An Image tag is what gives a node a UDreamVisual for a visual-typed property to take.
	Root.Children.Add(MakeNode(TEXT("Image"), TEXT("Glow")));

	FDreamUIComponent BehindSelectable = MakeComponent(TEXT("Selectable"));
	BehindSelectable.Properties.Add(AssignNodeId(TEXT("TransitionTarget"), TEXT("Glow")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Behind"), MoveTemp(BehindSelectable)));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Tree.Get()))
	{
		return false;
	}
	TestFalse(TEXT("and built clean"), Outcome.Diagnostics.HasErrors());

	UDreamWidget* Glow = Outcome.Find(TEXT("Glow"));
	if (!TestNotNull(TEXT("the target node exists"), Glow))
	{
		return false;
	}
	UDreamVisual* GlowVisual = Glow->GetVisual();
	if (!TestNotNull(TEXT("and the Image tag gave it a visual"), GlowVisual))
	{
		return false;
	}

	// TransitionTarget is a TWeakObjectPtr<UDreamVisual>. The weak half of FObjectPropertyBase is the
	// half the narrower FObjectProperty cast used to miss, so this is the interesting spelling.
	UDreamWidget* Ahead = Outcome.Find(TEXT("Ahead"));
	if (TestNotNull(TEXT("the forward-referencing node exists"), Ahead))
	{
		UUISelectable* Selectable = Ahead->GetComponent<UUISelectable>();
		if (TestNotNull(TEXT("and carries its Selectable"), Selectable))
		{
			// The VISUAL, not the widget: the property's type is what says which of a node's two
			// nameable parts the author meant, and coercing to the visual is what lets a transition
			// be aimed at 'Glow' rather than at a second name for the same node.
			TestEqual(TEXT("a forward reference resolves to the named node's visual"),
				(UObject*)Selectable->GetTransitionTarget(), (UObject*)GlowVisual);
		}
	}

	UDreamWidget* Behind = Outcome.Find(TEXT("Behind"));
	if (TestNotNull(TEXT("the backward-referencing node exists"), Behind))
	{
		UUISelectable* Selectable = Behind->GetComponent<UUISelectable>();
		if (TestNotNull(TEXT("and carries its Selectable"), Selectable))
		{
			TestEqual(TEXT("and so does a backward one"),
				(UObject*)Selectable->GetTransitionTarget(), (UObject*)GlowVisual);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNodeReferenceWidgetTypedTest,
	"DreamGUI.Text.NodeReference.AWidgetTypedPropertyTakesTheNodeItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNodeReferenceWidgetTypedTest::RunTest(const FString& Parameters)
{
	using namespace DreamNodeReferenceTestLocal;

	// UUISlider::Fill and ::Handle are TWeakObjectPtr<UDreamWidget> -- the other half of the rule,
	// where the property wants the NODE and not its visual. 'Bar' is deliberately a plain Widget
	// with no visual at all: that is legal here and is a refusal one test further down, and the two
	// only stay told apart if both are pinned.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));

	FDreamUIComponent Slider = MakeComponent(TEXT("Slider"));
	Slider.Properties.Add(AssignNodeId(TEXT("Fill"), TEXT("Bar")));
	// 'None' has to keep meaning the null pointer. It is a bare word like any node id, so the branch
	// that clears the property must stay AHEAD of the one that records a pending reference -- were
	// they the other way round this would resolve as a missing node instead of clearing.
	Slider.Properties.Add(AssignNodeId(TEXT("Handle"), TEXT("None")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Track"), MoveTemp(Slider)));

	Root.Children.Add(MakeNode(TEXT("Widget"), TEXT("Bar")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the tree built"), Outcome.Tree.Get()))
	{
		return false;
	}
	TestFalse(TEXT("and built clean"), Outcome.Diagnostics.HasErrors());

	UDreamWidget* Bar = Outcome.Find(TEXT("Bar"));
	UDreamWidget* Track = Outcome.Find(TEXT("Track"));
	if (!TestNotNull(TEXT("the target node exists"), Bar) || !TestNotNull(TEXT("the slider node exists"), Track))
	{
		return false;
	}
	TestNull(TEXT("the target is a plain Widget and has no visual"), Bar->GetVisual());

	UUISlider* SliderBehaviour = Track->GetComponent<UUISlider>();
	if (!TestNotNull(TEXT("the slider behaviour was attached"), SliderBehaviour))
	{
		return false;
	}
	TestEqual(TEXT("a widget-typed property takes the node itself"),
		(UObject*)SliderBehaviour->GetFill(), (UObject*)Bar);
	TestNull(TEXT("and 'None' still clears rather than naming a node"), SliderBehaviour->GetHandle());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNodeReferenceUnknownIdTest,
	"DreamGUI.Text.NodeReference.AnUnknownIdIsAMissingNodeNotATypeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNodeReferenceUnknownIdTest::RunTest(const FString& Parameters)
{
	using namespace DreamNodeReferenceTestLocal;

	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUIComponent Selectable = MakeComponent(TEXT("Selectable"));
	Selectable.Properties.Add(AssignNodeId(TEXT("TransitionTarget"), TEXT("Nowhere")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Button"), MoveTemp(Selectable)));
	// A real node exists, and is not the one named: a suite whose only failing fixture had no nodes
	// at all could not tell "the lookup missed" from "the lookup never ran".
	Root.Children.Add(MakeNode(TEXT("Image"), TEXT("Glow")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));

	TestTrue(TEXT("a name no node carries is reported under DUI5013"),
		Outcome.HasCode(EDreamUIDiagnosticCode::NodeReferenceNotFound));
	// The split is the point of the code existing. "There is no node called Nowhere" and "Nowhere is
	// not the kind of thing this property holds" send a reader to different places -- the first to
	// the spelling of a name, the second to the tag on a node that does exist.
	TestFalse(TEXT("and not as a value type mismatch"),
		Outcome.HasCode(EDreamUIDiagnosticCode::ValueTypeMismatch));
	// A missing node is not a missing ASSET either: 5013 exists because a reader told 'could not be
	// loaded' would go looking through the content browser for something never meant to be there.
	TestFalse(TEXT("nor as a missing asset"), Outcome.HasCode(EDreamUIDiagnosticCode::AssetNotFound));
	TestNull(TEXT("and the half-built tree is withheld"), Outcome.Tree.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNodeReferenceNoVisualTest,
	"DreamGUI.Text.NodeReference.ANodeWithNoVisualIsATypeMismatchNotAMissingNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNodeReferenceNoVisualTest::RunTest(const FString& Parameters)
{
	using namespace DreamNodeReferenceTestLocal;

	// The other half of the pair above, and the one that is easy to get wrong: 'Plain' EXISTS, so a
	// lookup that reported its own null result would blame the name. What is actually wrong is the
	// tag -- a plain Widget creates no visual, and the fix is to write 'Image Plain', not to correct
	// a spelling. The reader's move differs, so the code does.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUIComponent Selectable = MakeComponent(TEXT("Selectable"));
	Selectable.Properties.Add(AssignNodeId(TEXT("TransitionTarget"), TEXT("Plain")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Button"), MoveTemp(Selectable)));
	Root.Children.Add(MakeNode(TEXT("Widget"), TEXT("Plain")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));

	TestTrue(TEXT("a node that cannot supply a visual is reported under DUI4003"),
		Outcome.HasCode(EDreamUIDiagnosticCode::ValueTypeMismatch));
	TestFalse(TEXT("and not as a node this file does not declare"),
		Outcome.HasCode(EDreamUIDiagnosticCode::NodeReferenceNotFound));
	TestNull(TEXT("and the half-built tree is withheld"), Outcome.Tree.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNodeReferenceAssetPathUntouchedTest,
	"DreamGUI.Text.NodeReference.APathIsStillAnAssetPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNodeReferenceAssetPathUntouchedTest::RunTest(const FString& Parameters)
{
	using namespace DreamNodeReferenceTestLocal;

	// The regression guard. Node references were added to the ONE property kind that could already
	// hold an asset, and the only thing keeping the two apart is a leading '/'. If the new branch
	// ever widened to swallow paths, every authored asset reference on a widget- or visual-typed
	// property in the project would start reporting itself as a missing node -- and the message
	// would send its reader hunting for a node nobody ever wrote.
	//
	// The assertion is on WHICH refusal, not on whether it failed: DUI5001 can only come from the
	// asset branch and DUI5013 can only come from the node branch, so the code names the route taken.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));
	FDreamUIComponent Selectable = MakeComponent(TEXT("Selectable"));
	Selectable.Properties.Add(Assign(TEXT("TransitionTarget"),
		Literal(EDreamUIValueKind::AssetPath, TEXT("/Game/DreamGUITests/NoSuchNodeReferenceAsset"))));
	Root.Children.Add(MakeNodeCarrying(TEXT("Button"), MoveTemp(Selectable)));
	// Named so that a builder which tried the node lookup on the path's tail would find something --
	// a silent success is a louder failure here than a wrong code.
	Root.Children.Add(MakeNode(TEXT("Image"), TEXT("NoSuchNodeReferenceAsset")));

	const FBuildOutcome Outcome = BuildFrom(AstWith(Root));

	TestTrue(TEXT("a '/'-leading value still goes to the asset loader"),
		Outcome.HasCode(EDreamUIDiagnosticCode::AssetNotFound));
	TestFalse(TEXT("and is never read as a node id"),
		Outcome.HasCode(EDreamUIDiagnosticCode::NodeReferenceNotFound));
	TestNull(TEXT("and the half-built tree is withheld"), Outcome.Tree.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNodeReferenceSurvivesInstancingTest,
	"DreamGUI.Text.NodeReference.AnInstancedTreeAimsItsReferencesAtItsOwnNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNodeReferenceSurvivesInstancingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNodeReferenceTestLocal;
	FScopedGameWorld TestWorld;

	// The second half, and the one nothing above can see. Everything the builder writes lands in the
	// class template. FObjectInstancingGraph follows CPF_InstancedReference -- RootWidget, Children,
	// Visual, Components -- which is what carries the hierarchy across; a plain or weak pointer from
	// one node to another carries no such flag and is copied VERBATIM. Left alone, every instance of
	// this screen would drive the archetype's visual: one object shared by every instance of the
	// class, parented to nothing, never drawn. UUISelectable::Start's `if (!TransitionTarget.
	// IsValid())` self-heal cannot notice, because a pointer at the template is perfectly valid.
	//
	// Entry point is InitializeWidgetStatic rather than the retargeting pass itself: the pass is
	// file-local to DreamWidgetGeneratedClass.cpp and calling it would prove only that it compiles.
	// This is the same entry point UDreamUserWidget::Initialize uses, and it takes its archetype by
	// parameter, so the whole instancing path is reachable without a Blueprint or a generated class.
	//
	// The archetype is produced by the real builder rather than assembled by hand, which makes this
	// the one test that covers both halves composed: a .dui-shaped tree in, a usable instance out.
	FDreamUINode Root = MakeNode(TEXT("Widget"), TEXT("Root"));

	FDreamUIComponent Selectable = MakeComponent(TEXT("Selectable"));
	Selectable.Properties.Add(AssignNodeId(TEXT("TransitionTarget"), TEXT("Glow")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Button"), MoveTemp(Selectable)));
	Root.Children.Add(MakeNode(TEXT("Image"), TEXT("Glow")));

	// The weak-WIDGET family on its own node. UUISlider derives from UUISelectable, so putting both
	// behaviours on one node would give it two selectables and make GetComponent<UUISelectable>()
	// ambiguous about which one was under test.
	FDreamUIComponent Slider = MakeComponent(TEXT("Slider"));
	Slider.Properties.Add(AssignNodeId(TEXT("Fill"), TEXT("Bar")));
	Root.Children.Add(MakeNodeCarrying(TEXT("Track"), MoveTemp(Slider)));
	Root.Children.Add(MakeNode(TEXT("Widget"), TEXT("Bar")));

	const FBuildOutcome Archetype = BuildFrom(AstWith(Root));
	if (!TestNotNull(TEXT("the archetype tree built"), Archetype.Tree.Get()))
	{
		return false;
	}
	TestFalse(TEXT("and built clean"), Archetype.Diagnostics.HasErrors());

	UDreamWidget* ArchetypeGlow = Archetype.Find(TEXT("Glow"));
	UDreamWidget* ArchetypeBar = Archetype.Find(TEXT("Bar"));
	if (!TestNotNull(TEXT("the archetype's visual target exists"), ArchetypeGlow)
		|| !TestNotNull(TEXT("the archetype's widget target exists"), ArchetypeBar))
	{
		return false;
	}

	// UDreamUserWidget itself, not a fixture subclass: the by-name binding step only considers
	// properties declared BELOW UDreamUserWidget, so the base class declares no binding candidates
	// and nothing in this test can be confused with a binding having done the work.
	TStrongObjectPtr<UDreamUserWidget> UserWidget(NewObject<UDreamUserWidget>(TestWorld.World));
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(
		UserWidget.Get(), UDreamUserWidget::StaticClass(), Archetype.Tree.Get());

	UDreamWidgetTree* Instanced = UserWidget->GetWidgetTree();
	if (!TestNotNull(TEXT("the user widget got a tree of its own"), Instanced))
	{
		return false;
	}
	TestNotEqual(TEXT("and it is not the archetype"),
		(const UObject*)Instanced, (const UObject*)Archetype.Tree.Get());

	UDreamWidget* InstancedGlow = Instanced->FindWidgetByVariableName(FName(TEXT("Glow")));
	UDreamWidget* InstancedBar = Instanced->FindWidgetByVariableName(FName(TEXT("Bar")));
	UDreamWidget* InstancedButton = Instanced->FindWidgetByVariableName(FName(TEXT("Button")));
	UDreamWidget* InstancedTrack = Instanced->FindWidgetByVariableName(FName(TEXT("Track")));
	if (!TestNotNull(TEXT("the whole hierarchy came across (Glow)"), InstancedGlow)
		|| !TestNotNull(TEXT("the whole hierarchy came across (Bar)"), InstancedBar)
		|| !TestNotNull(TEXT("the whole hierarchy came across (Button)"), InstancedButton)
		|| !TestNotNull(TEXT("the whole hierarchy came across (Track)"), InstancedTrack))
	{
		return false;
	}

	// The visual case. Three separate claims, because two of them can hold while the third fails:
	// a pointer can be non-null and still be the archetype's, and it can be inside the right tree
	// and still be the wrong node's visual.
	UUISelectable* InstancedSelectable = InstancedButton->GetComponent<UUISelectable>();
	if (TestNotNull(TEXT("the instance carries its own Selectable"), InstancedSelectable))
	{
		UDreamVisual* Target = InstancedSelectable->GetTransitionTarget();
		if (TestNotNull(TEXT("whose transition target survived instancing"), Target))
		{
			TestTrue(TEXT("the target belongs to the instance's tree"), Target->IsIn(Instanced));
			// The assertion this test exists for. Widgets are outered flat to their tree, so IsIn is
			// an exact statement of which tree an object belongs to -- and an object still inside
			// the archetype is, by construction, one no instance may hold.
			TestFalse(TEXT("and not to the class template"), Target->IsIn(Archetype.Tree.Get()));
			TestEqual(TEXT("it is the instanced node's own visual"),
				(UObject*)Target, (UObject*)InstancedGlow->GetVisual());
		}
	}

	// The widget case. Same claim on the other family; a fix that matched only visuals would pass
	// everything above and still leave every slider in the project driving the template's fill.
	UUISlider* InstancedSlider = InstancedTrack->GetComponent<UUISlider>();
	if (TestNotNull(TEXT("the instance carries its own Slider"), InstancedSlider))
	{
		UDreamWidget* Fill = InstancedSlider->GetFill();
		if (TestNotNull(TEXT("whose fill survived instancing"), Fill))
		{
			TestTrue(TEXT("the fill belongs to the instance's tree"), Fill->IsIn(Instanced));
			TestFalse(TEXT("and not to the class template"), Fill->IsIn(Archetype.Tree.Get()));
			TestEqual(TEXT("it is the instanced node itself"), (UObject*)Fill, (UObject*)InstancedBar);
		}
	}

	// The template is instanced once per instance and must come out of it unchanged. A retargeting
	// pass that wrote through to the archetype would leave the first instance correct and every
	// later one aimed at the first.
	UDreamWidget* ArchetypeButton = Archetype.Find(TEXT("Button"));
	if (TestNotNull(TEXT("the archetype's referencing node is still there"), ArchetypeButton))
	{
		UUISelectable* ArchetypeSelectable = ArchetypeButton->GetComponent<UUISelectable>();
		if (TestNotNull(TEXT("with its behaviour"), ArchetypeSelectable))
		{
			TestEqual(TEXT("and the archetype still points at its own visual"),
				(UObject*)ArchetypeSelectable->GetTransitionTarget(), (UObject*)ArchetypeGlow->GetVisual());
		}
	}
	return true;
}

#endif
