// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/World.h"
#include "Interaction/UISelectable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The C++ tree description, and the walk that turns it into widgets.
 *
 * The claim worth testing is not "the fluent calls compile" -- that is the compiler's job and it is
 * doing it -- but that the DESCRIPTION and the TREE agree, on the four points where a builder can
 * plausibly disagree with what was written:
 *
 *   NAMES. The name in the expression becomes DisplayName, not the object name. That is deliberate:
 *   object names must be unique inside the tree, and two nodes called "BG" in different branches is
 *   ordinary authoring. So a test builds exactly that and expects both to survive -- an
 *   implementation that passed the name to ConstructWidget would take one of them away.
 *
 *   ORDER. Children come out in the order they were written. A tree whose children arrive in
 *   argument-evaluation order or reversed is one where every stack layout is subtly wrong, and
 *   nothing else here would notice.
 *
 *   .Then. It runs after the WHOLE tree exists. This is the point of the deferred design: a parent
 *   is built before its children, so a behaviour that has to name a node below it -- a toggle
 *   aiming its checked transition at a knob, a slider handed its fill -- cannot be wired inline.
 *   The test therefore puts a .Then on the ROOT that reads an .Out captured from the DEEPEST child,
 *   which is null at the moment the root is constructed. An implementation that ran .Then inline
 *   fails it, and only it.
 *
 *   .Slot. It runs after ATTACH, because a child has no slot to configure until its parent -- which
 *   is what hands slots out -- is holding it.
 *
 * These are hand-built widget trees with no world and no registration. Realize is documented to
 * return an unregistered tree, which is exactly what makes it testable headlessly: nothing here
 * needs an RHI, a canvas, or a viewport.
 */
namespace DreamUIBuilderTestLocal
{
	/** Realize needs an outer for the tree; a transient package is the whole fixture. */
	struct FScopedTree
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		FScopedTree()
		{
			Tree.Reset(NewObject<UDreamWidgetTree>(GetTransientPackage()));
		}

		UDreamWidgetTree* Get() const { return Tree.Get(); }
	};

	/** Children by display name, in the order the tree holds them. */
	TArray<FString> ChildNames(const UDreamWidget* InWidget)
	{
		TArray<FString> Names;
		if (InWidget != nullptr)
		{
			for (const UDreamWidget* Child : InWidget->GetChildren())
			{
				Names.Add(Child != nullptr ? Child->GetDisplayName() : TEXT("<null>"));
			}
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBuilderShapeTest,
	"DreamGUI.Builder.TheTreeMatchesTheDescription",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBuilderShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;
	using namespace DreamUIBuilderTestLocal;

	FScopedTree Fixture;

	UDreamWidget* Knob = nullptr;
	UDreamWidget* Label = nullptr;

	UDreamWidget* Root = Realize(Fixture.Get(),
		Widget("Toggle")
			.With<UDreamLayoutContainerHorizontalBox>([](UDreamLayoutContainerHorizontalBox& InBox)
			{
				InBox.SetSpacing(8.0f);
			})
			.With<UUISelectable>()
			.Children(
				Image("Box")
					.Size(26.0f, 26.0f)
					.Children(
						Image("Knob").Out(Knob).Size(16.0f, 16.0f)),
				Text("Label").Out(Label)));

	if (!TestNotNull(TEXT("Realize returns a root"), Root))
	{
		return false;
	}

	// Names. The expression said "Toggle"; the tree has to answer to it.
	TestEqual(TEXT("the root carries the authored name"), Root->GetDisplayName(), FString(TEXT("Toggle")));

	// Order, and only the children that were written.
	const TArray<FString> Names = ChildNames(Root);
	TestEqual(TEXT("the root has two children"), Names.Num(), 2);
	if (Names.Num() == 2)
	{
		TestEqual(TEXT("written first, held first"), Names[0], FString(TEXT("Box")));
		TestEqual(TEXT("written second, held second"), Names[1], FString(TEXT("Label")));
	}

	// The tag position of the expression decides what draws. A Widget() node draws nothing, and that
	// is a distinct state from "an Image whose brush is empty".
	TestNull(TEXT("Widget() makes a node with no visual"), Root->GetVisual());
	TestNotNull(TEXT("Image() makes a node drawn by a UDreamImage"), Cast<UDreamImage>(Root->GetChildren()[0]->GetVisual()));
	TestNotNull(TEXT("Text() makes a node drawn by a UDreamText"), Cast<UDreamText>(Root->GetChildren()[1]->GetVisual()));

	// `+ Something {}` in .dui splits three ways on the class, and so does With<>: a layout container
	// is not a component and does not arrive in the component list.
	TestNotNull(TEXT("a layout container becomes the layout container"), Root->GetLayoutContainer());
	TestNotNull(TEXT("a behaviour becomes a component"), Root->GetComponent<UUISelectable>());

	// .Out hands back the widget that is actually in the tree, not a copy of the description.
	TestNotNull(TEXT("Out captured the knob"), Knob);
	TestNotNull(TEXT("Out captured the label"), Label);
	if (Knob != nullptr)
	{
		TestTrue(TEXT("the captured knob is the one in the tree"),
			(UObject*)Knob == (UObject*)Root->GetChildren()[0]->GetChildren()[0]);
	}
	if (Label != nullptr)
	{
		TestTrue(TEXT("the captured label is the one in the tree"),
			(UObject*)Label == (UObject*)Root->GetChildren()[1]);
	}

	// Nesting is a tree, not a list: the knob hangs under the box, not under the root.
	TestEqual(TEXT("the box holds the knob"), ChildNames(Root->GetChildren()[0]).Num(), 1);

	// The shortcuts are shorthand for Self, and Self runs.
	TestEqual(TEXT("Size set the width"), Root->GetChildren()[0]->GetWidth(), 26.0f);
	TestEqual(TEXT("Size set the height"), Root->GetChildren()[0]->GetHeight(), 26.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBuilderAttachOrderTest,
	"DreamGUI.Builder.ANodeIsAttachedBeforeItIsConfigured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBuilderAttachOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;
	using namespace DreamUIBuilderTestLocal;

	FScopedTree Fixture;

	// Half of what a widget can be told is refused while it has no parent, and refused SILENTLY:
	// SetHorizontalAndVerticalAnchorMinMax's whole body is inside `if (Parent.IsValid())`. Configuring
	// before attaching therefore produces a tree that is correct in structure and inert in layout,
	// which is exactly the state the first native control came out in -- a root anchored to nothing,
	// measuring zero, with every part of it correctly sized inside a box of no size.
	UDreamWidget* Child = nullptr;
	UDreamWidget* Root = Realize(Fixture.Get(),
		Widget("Parent")
			.Size(200.0f, 100.0f)
			.Children(
				Widget("Stretched").Out(Child).Anchors(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0))));

	if (!TestNotNull(TEXT("Realize returns a root"), Root) || !TestNotNull(TEXT("and the child"), Child))
	{
		return false;
	}
	TestEqual(TEXT("the child's anchors were actually set"), Child->GetAnchorMin(), FVector2D(0.0, 0.0));
	TestEqual(TEXT("both corners of them"), Child->GetAnchorMax(), FVector2D(1.0, 1.0));

	// And the ROOT, which is the one that actually went missing: inside the walk it has no parent
	// unless the caller names one, so a root described as stretched came out at its birth anchors and
	// measured zero -- with every part beneath it correctly sized inside a box of no size.
	UDreamWidget* Host = Realize(Fixture.Get(), Widget("Host").Size(300.0f, 80.0f));
	UDreamWidget* StretchedRoot = Realize(Fixture.Get(),
		Widget("Root").Anchors(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0)), Host);
	if (TestNotNull(TEXT("a root realized under a parent"), StretchedRoot))
	{
		TestEqual(TEXT("is told its anchors too"), StretchedRoot->GetAnchorMax(), FVector2D(1.0, 1.0));
		TestTrue(TEXT("and hangs where it was told"), (UObject*)StretchedRoot->GetParent() == (UObject*)Host);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBuilderDeferredTest,
	"DreamGUI.Builder.ThenRunsOnceTheWholeTreeExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBuilderDeferredTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;
	using namespace DreamUIBuilderTestLocal;

	FScopedTree Fixture;

	UDreamImage* KnobVisual = nullptr;
	int32 ThenCallCount = 0;
	UDreamVisual* SeenByThen = nullptr;
	UDreamWidget* SeenSelf = nullptr;

	// The wiring a real control needs: a toggle whose CHECKED transition points at a knob two levels
	// below it, while its hover transition keeps its own visual. The reference runs downward from a
	// node built before its target existed, which is the whole reason .Then is not inline.
	UDreamWidget* Root = Realize(Fixture.Get(),
		Widget("Toggle")
			.With<UUISelectable>()
			.Then([&](UDreamWidget& InRoot)
			{
				++ThenCallCount;
				SeenSelf = &InRoot;
				SeenByThen = KnobVisual;
				if (UUISelectable* Selectable = InRoot.GetComponent<UUISelectable>())
				{
					Selectable->SetTransitionTarget(KnobVisual);
				}
			})
			.Children(
				Image("Box").Children(
					Image("Knob").OutVisual(KnobVisual))));

	if (!TestNotNull(TEXT("Realize returns a root"), Root))
	{
		return false;
	}

	TestEqual(TEXT("Then ran exactly once"), ThenCallCount, 1);
	TestTrue(TEXT("Then was handed the node it was written on"), SeenSelf == Root);

	// The load-bearing assertion. The root's .Then was written before the knob existed; if it ran
	// inline this is null, and a control wired that way ships with a checked state that does nothing.
	TestNotNull(TEXT("Then saw a node built after the one it was written on"), SeenByThen);
	if (UUISelectable* Selectable = Root->GetComponent<UUISelectable>())
	{
		TestTrue(TEXT("the deferred wiring reached the behaviour"),
			(UObject*)Selectable->GetTransitionTarget() == (UObject*)SeenByThen);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBuilderSlotTest,
	"DreamGUI.Builder.SlotSettingsReachASlotMintedOnAttach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBuilderSlotTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;
	using namespace DreamUIBuilderTestLocal;

	FScopedTree Fixture;

	UDreamWidget* Root = Realize(Fixture.Get(),
		Widget("Row")
			.With<UDreamLayoutContainerHorizontalBox>()
			.Children(
				Text("Fills").Slot([](UDreamPanelSlot& InSlot)
				{
					InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
					InSlot.SetPadding(FMargin(4.0f));
				}),
				Text("Plain")));

	if (!TestNotNull(TEXT("Realize returns a root"), Root) || Root->GetChildren().Num() != 2)
	{
		return false;
	}

	// A slot exists only because the PARENT hands them out, and only after the child is attached to
	// that parent -- so this pins the ordering inside Realize as much as the setting itself. An
	// authoring tree never reaches registration, where EnsurePanelSlotForChild would otherwise mint it.
	UDreamPanelSlot* Slot = Root->GetChildren()[0]->GetPanelSlot();
	if (TestNotNull(TEXT("the child with slot settings has a slot"), Slot))
	{
		TestTrue(TEXT("the slot carries what was written"), Slot->SizeRule == EDreamPanelSizeRule::Fill);
		TestEqual(TEXT("and the padding with it"), Slot->Padding.Left, 4.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBuilderDuplicateNameTest,
	"DreamGUI.Builder.TwoBranchesMayUseTheSameName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBuilderDuplicateNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;
	using namespace DreamUIBuilderTestLocal;

	FScopedTree Fixture;

	// Every control in the library does this -- a "BG" under the track and a "BG" under the handle.
	// The names are DisplayNames, and only object names have to be unique within the tree, so both
	// survive. Passing the authored name to ConstructWidget instead would make this collide.
	UDreamWidget* Root = Realize(Fixture.Get(),
		Widget("Slider")
			.Children(
				Widget("TrackArea").Children(Image("BG")),
				Widget("HandleArea").Children(Image("BG"))));

	if (!TestNotNull(TEXT("Realize returns a root"), Root) || Root->GetChildren().Num() != 2)
	{
		return false;
	}

	const TArray<FString> First = ChildNames(Root->GetChildren()[0]);
	const TArray<FString> Second = ChildNames(Root->GetChildren()[1]);
	TestEqual(TEXT("the first branch kept its BG"), First.Num(), 1);
	TestEqual(TEXT("the second branch kept its BG"), Second.Num(), 1);
	if (First.Num() == 1 && Second.Num() == 1)
	{
		TestEqual(TEXT("and both answer to the authored name"), First[0], FString(TEXT("BG")));
		TestEqual(TEXT("and both answer to the authored name"), Second[0], FString(TEXT("BG")));
	}
	TestTrue(TEXT("they are nevertheless two objects"),
		(UObject*)Root->GetChildren()[0]->GetChildren()[0] != (UObject*)Root->GetChildren()[1]->GetChildren()[0]);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBuilderUserWidgetTest,
	"DreamGUI.Builder.AUserWidgetWithNoArchetypeGetsATreeOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBuilderUserWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;

	// The path a native control takes: a class with no asset behind it, so InitializeWidgetStatic
	// makes no tree, and the widget builds its own contents from NativeOnInitialized.
	TStrongObjectPtr<UDreamUserWidget> Owner(NewObject<UDreamUserWidget>(GetTransientPackage()));
	TestNull(TEXT("a user widget starts with no tree"), Owner->WidgetTree.Get());

	UDreamWidget* Root = Realize(Owner.Get(),
		Widget("Contents").Children(Image("BG"), Text("Caption")));

	if (!TestNotNull(TEXT("Realize returns a root"), Root))
	{
		return false;
	}
	TestNotNull(TEXT("the tree was made for it"), Owner->WidgetTree.Get());
	if (Owner->WidgetTree != nullptr)
	{
		TestTrue(TEXT("and the root is the tree's root"), (UObject*)Owner->WidgetTree->RootWidget == (UObject*)Root);
		TestTrue(TEXT("the contents live in that tree"), Root->IsIn(Owner->WidgetTree));
	}
	// Hung under the user widget, the way InitializeWidgetStatic's last step hangs an instanced one --
	// without it the contents are built and belong to nobody.
	TestTrue(TEXT("the root hangs under the user widget"), (UObject*)Root->GetParent() == (UObject*)Owner.Get());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
