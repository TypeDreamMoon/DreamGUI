// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "UObject/Package.h"

/*
 * What a binding is written in terms of, and what happens to it when that changes underneath.
 *
 * A binding names its widget by the VARIABLE name the compiler derives from the display name, and its
 * behaviour by POSITION in the widget's component array -- neither of which is stable under ordinary
 * designer editing. Renaming a widget, deleting one, or dragging a behaviour up the component list
 * used to leave every binding that mentioned it pointing somewhere else: at a name the hierarchy no
 * longer has (a compile error, forever, from a panel with no control that could clear it) or, worse,
 * at whatever behaviour had moved into the vacated slot, which is silent whenever the two behaviours
 * share a class.
 *
 * These drive the real editing entry points rather than the fixup helper, because the claim is that
 * the fixup HAPPENS -- a helper nobody calls passes its own tests forever.
 */

namespace DreamBindingBookkeepingTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		UDreamWidget* AddWidget(const TCHAR* InDisplayName, UDreamWidget* InParent = nullptr)
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			UDreamWidget* Widget = Tree->ConstructWidget<UDreamWidget>();
			Widget->SetDisplayName(InDisplayName);
			Widget->SetParentBeforeRegister(InParent != nullptr ? InParent : Tree->RootWidget.Get());
			return Widget;
		}
	};

	/** A property binding on a widget, authored the way the Bind button authors one. */
	void AddWidgetBinding(UDreamWidgetBlueprint* InBlueprint, FName InWidgetName, FName InPropertyName)
	{
		FDreamWidgetPropertyBinding& Binding = InBlueprint->PropertyBindings.AddDefaulted_GetRef();
		Binding.WidgetName = InWidgetName;
		Binding.Target = EDreamWidgetBindingTarget::Widget;
		Binding.PropertyName = InPropertyName;
		Binding.FunctionName = FName(TEXT("GetSomething"));
	}

	/** A property binding on one of a widget's behaviours, which is named by position. */
	void AddBehaviourBinding(UDreamWidgetBlueprint* InBlueprint, FName InWidgetName, int32 InBehaviourIndex, FName InPropertyName)
	{
		FDreamWidgetPropertyBinding& Binding = InBlueprint->PropertyBindings.AddDefaulted_GetRef();
		Binding.WidgetName = InWidgetName;
		Binding.Target = EDreamWidgetBindingTarget::Behaviour;
		Binding.BehaviourIndex = InBehaviourIndex;
		Binding.PropertyName = InPropertyName;
		Binding.FunctionName = FName(TEXT("GetSomething"));
	}

	const FDreamWidgetPropertyBinding* FindByProperty(const UDreamWidgetBlueprint* InBlueprint, FName InPropertyName)
	{
		return InBlueprint->PropertyBindings.FindByPredicate(
			[InPropertyName](const FDreamWidgetPropertyBinding& Candidate) { return Candidate.PropertyName == InPropertyName; });
	}

	/**
	 * An animation on the hierarchy's root with one track bound to InTarget, the way the sequencer binds
	 * one: a possessable, then BindPossessableObject with the ROOT as context, so the recorded path runs
	 * from the animation's owner down to InTarget by display name.
	 */
	FGuid BindAnimationToWidget(FScopedBlueprint& InFixture, UDreamWidget* InTarget, UDreamWidgetAnimation*& OutSequence)
	{
		OutSequence = nullptr;
		UDreamWidget* Root = InFixture.Blueprint->GetOrCreateWidgetTree()->RootWidget.Get();
		UDreamWidgetAnimationComponent* Animator = Root != nullptr
			? Cast<UDreamWidgetAnimationComponent>(Root->AddComponent(UDreamWidgetAnimationComponent::StaticClass())) : nullptr;
		OutSequence = Animator != nullptr ? Animator->AddNewAnimation() : nullptr;
		if (OutSequence == nullptr || OutSequence->GetMovieScene() == nullptr || InTarget == nullptr)
		{
			return FGuid();
		}
		const FGuid BindingId = OutSequence->GetMovieScene()->AddPossessable(InTarget->GetDisplayName(), InTarget->GetClass());
		OutSequence->BindPossessableObject(BindingId, *InTarget, Root);
		return BindingId;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenameCarriesBindingsWithItTest,
	"DreamGUI.Editor.Bindings.RenamingAWidgetCarriesTheBindingsThatNamedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenameCarriesBindingsWithItTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingBookkeepingTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_RenameCarriesBindings"));
	if (!TestNotNull(TEXT("the fixture Blueprint was created"), Fixture.Blueprint))return true;

	UDreamWidget* Label = Fixture.AddWidget(TEXT("Label"));
	UDreamWidget* Other = Fixture.AddWidget(TEXT("Other"));
	if (!TestTrue(TEXT("the fixture widgets are in the authored tree"),
		DreamWidgetTreeEditing::IsTemplateWidgetOf(Fixture.Blueprint, Label)
		&& DreamWidgetTreeEditing::IsTemplateWidgetOf(Fixture.Blueprint, Other)))return true;

	AddWidgetBinding(Fixture.Blueprint, UDreamWidgetTree::MakeWidgetVariableName(Label), TEXT("BoundToLabel"));
	// A second binding on a widget that is NOT being renamed: a fixup that rewrites the whole list
	// rather than the entries that matched would pass every assertion below without this one.
	AddWidgetBinding(Fixture.Blueprint, UDreamWidgetTree::MakeWidgetVariableName(Other), TEXT("BoundToOther"));

	const FString Applied = DreamWidgetTreeEditing::RenameWidget(Fixture.Blueprint, Label, TEXT("Title"));
	if (!TestEqual(TEXT("the rename went through"), Applied, FString(TEXT("Title"))))return true;

	const FDreamWidgetPropertyBinding* Carried = FindByProperty(Fixture.Blueprint, TEXT("BoundToLabel"));
	if (!TestNotNull(TEXT("the binding still exists"), Carried))return true;
	// The whole defect: the binding used to keep saying "Label", which the next compile reports as a
	// widget this hierarchy does not have, and the binding is dropped from the class.
	TestEqual(TEXT("the binding now names the widget's new variable name"),
		Carried->WidgetName, FName(TEXT("Title")));

	const FDreamWidgetPropertyBinding* Untouched = FindByProperty(Fixture.Blueprint, TEXT("BoundToOther"));
	if (!TestNotNull(TEXT("the other widget's binding still exists"), Untouched))return true;
	TestEqual(TEXT("...and was left alone"), Untouched->WidgetName, FName(TEXT("Other")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteDropsBindingsThatNamedItTest,
	"DreamGUI.Editor.Bindings.DeletingAWidgetDropsTheBindingsThatNamedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteDropsBindingsThatNamedItTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingBookkeepingTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_DeleteDropsBindings"));
	if (!TestNotNull(TEXT("the fixture Blueprint was created"), Fixture.Blueprint))return true;

	UDreamWidget* Panel = Fixture.AddWidget(TEXT("Panel"));
	// A CHILD of the deleted widget: it goes with its parent, so its bindings have to go too, and a
	// fixup that only looked at the widget it was handed would leave this one behind.
	UDreamWidget* Inner = Fixture.AddWidget(TEXT("Inner"), Panel);
	UDreamWidget* Survivor = Fixture.AddWidget(TEXT("Survivor"));

	AddWidgetBinding(Fixture.Blueprint, UDreamWidgetTree::MakeWidgetVariableName(Panel), TEXT("BoundToPanel"));
	AddWidgetBinding(Fixture.Blueprint, UDreamWidgetTree::MakeWidgetVariableName(Inner), TEXT("BoundToInner"));
	AddWidgetBinding(Fixture.Blueprint, UDreamWidgetTree::MakeWidgetVariableName(Survivor), TEXT("BoundToSurvivor"));

	if (!TestTrue(TEXT("the delete went through"), DreamWidgetTreeEditing::DeleteWidget(Fixture.Blueprint, Panel)))return true;

	// Left behind, these are records naming widgets the hierarchy no longer has: a compile error on
	// every compile from now on, and no UI anywhere that could clear them.
	TestNull(TEXT("the deleted widget's binding is gone"), FindByProperty(Fixture.Blueprint, TEXT("BoundToPanel")));
	TestNull(TEXT("its child's binding is gone with it"), FindByProperty(Fixture.Blueprint, TEXT("BoundToInner")));
	TestNotNull(TEXT("an unrelated widget's binding survives"), FindByProperty(Fixture.Blueprint, TEXT("BoundToSurvivor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamReorderingBehavioursRenumbersBindingsTest,
	"DreamGUI.Editor.Bindings.ReorderingBehavioursRenumbersTheBindingsThatNamedThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamReorderingBehavioursRenumbersBindingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingBookkeepingTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_ReorderBehaviours"));
	if (!TestNotNull(TEXT("the fixture Blueprint was created"), Fixture.Blueprint))return true;

	UDreamWidget* Host = Fixture.AddWidget(TEXT("Host"));
	const FName HostName = UDreamWidgetTree::MakeWidgetVariableName(Host);
	// Three, so a move has a slot to pass over and the arithmetic is not the identity by accident.
	AddBehaviourBinding(Fixture.Blueprint, HostName, 0, TEXT("BoundToFirst"));
	AddBehaviourBinding(Fixture.Blueprint, HostName, 1, TEXT("BoundToSecond"));
	AddBehaviourBinding(Fixture.Blueprint, HostName, 2, TEXT("BoundToThird"));

	// Behaviour 0 dragged to the end: 0 -> 2, and everything it passed shifts down one.
	DreamWidgetTreeEditing::RemapBehaviourBindings(Fixture.Blueprint, Host, 0, 2);
	{
		const FDreamWidgetPropertyBinding* First = FindByProperty(Fixture.Blueprint, TEXT("BoundToFirst"));
		const FDreamWidgetPropertyBinding* Second = FindByProperty(Fixture.Blueprint, TEXT("BoundToSecond"));
		const FDreamWidgetPropertyBinding* Third = FindByProperty(Fixture.Blueprint, TEXT("BoundToThird"));
		if (!TestTrue(TEXT("all three bindings survive a reorder"),
			First != nullptr && Second != nullptr && Third != nullptr))return true;
		TestEqual(TEXT("the moved behaviour's binding followed it"), First->BehaviourIndex, 2);
		TestEqual(TEXT("the one it passed moved down"), Second->BehaviourIndex, 0);
		TestEqual(TEXT("...and so did the next"), Third->BehaviourIndex, 1);
	}

	// Now remove what is at index 0 (the one that used to be second). Its binding has no behaviour
	// left to name, and the ones after it move down.
	DreamWidgetTreeEditing::RemapBehaviourBindings(Fixture.Blueprint, Host, 0, INDEX_NONE);
	TestNull(TEXT("the removed behaviour's binding is gone"), FindByProperty(Fixture.Blueprint, TEXT("BoundToSecond")));
	{
		const FDreamWidgetPropertyBinding* Third = FindByProperty(Fixture.Blueprint, TEXT("BoundToThird"));
		const FDreamWidgetPropertyBinding* First = FindByProperty(Fixture.Blueprint, TEXT("BoundToFirst"));
		if (!TestTrue(TEXT("the other two survive the removal"), Third != nullptr && First != nullptr))return true;
		TestEqual(TEXT("the binding after the hole moved down"), Third->BehaviourIndex, 0);
		TestEqual(TEXT("...and so did the one after that"), First->BehaviourIndex, 1);
	}

	// A binding on the WIDGET itself shares the widget name and must never be renumbered: only
	// EDreamWidgetBindingTarget::Behaviour carries an index at all.
	AddWidgetBinding(Fixture.Blueprint, HostName, TEXT("BoundToWidget"));
	DreamWidgetTreeEditing::RemapBehaviourBindings(Fixture.Blueprint, Host, 0, 1);
	const FDreamWidgetPropertyBinding* WidgetBinding = FindByProperty(Fixture.Blueprint, TEXT("BoundToWidget"));
	if (!TestNotNull(TEXT("the widget's own binding survives"), WidgetBinding))return true;
	TestEqual(TEXT("...with no behaviour index invented for it"), WidgetBinding->BehaviourIndex, (int32)INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenameCarriesGraphAndAnimationTest,
	"DreamGUI.Editor.Bindings.RenamingAWidgetCarriesTheGraphNodesAndAnimationPathsThatNamedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Renaming a widget in the designer broke every graph node that read it, and every animation path through it.
 *
 * The rename carried the authored bindings and nothing else. The widget's variable has a guid derived from its name, so
 * a "Get" of the old name found nothing at the next compile, and an animation bound through the widget kept the old path
 * -- which the compile reports as a path that walks to nothing -- until a save happened to repair it. A `(was:)` clause
 * already carries both for a .dui; the designer's rename now goes through the same function. This reads the widget in
 * the event graph and animates it, renames it, and compiles.
 */
bool FDreamRenameCarriesGraphAndAnimationTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingBookkeepingTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_RenameCarriesGraphAndAnimation"));
	if (!TestNotNull(TEXT("the fixture Blueprint was created"), Fixture.Blueprint))return true;

	UDreamWidget* Label = Fixture.AddWidget(TEXT("Label"));
	UDreamWidgetAnimation* Sequence = nullptr;
	if (!TestTrue(TEXT("an animation drives the widget"), BindAnimationToWidget(Fixture, Label, Sequence).IsValid()))return true;
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		if (!TestEqual(TEXT("the hierarchy compiles clean before the rename"), Results.NumErrors, 0))return true;
	}

	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Fixture.Blueprint);
	if (!TestNotNull(TEXT("the Blueprint has an event graph"), Graph))return true;
	UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(Graph);
	Getter->VariableReference.SetSelfMember(FName(TEXT("Label")));
	Graph->AddNode(Getter, /*bFromUI*/false, /*bSelectNewNode*/false);
	Getter->CreateNewGuid();
	Getter->PostPlacedNewNode();
	Getter->AllocateDefaultPins();

	const FString Applied = DreamWidgetTreeEditing::RenameWidget(Fixture.Blueprint, Label, TEXT("Title"));
	if (!TestEqual(TEXT("the rename went through"), Applied, FString(TEXT("Title"))))return true;
	TestEqual(TEXT("the graph node reads the new name"), Getter->GetVarName(), FName(TEXT("Title")));

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("and the asset compiles clean -- no node naming a missing variable, no path naming a missing widget"),
		Results.NumErrors, 0);
	TestEqual(TEXT("the node still reads the new name after the compile"), Getter->GetVarName(), FName(TEXT("Title")));

	TArray<TPair<FGuid, FString>> Unresolvable;
	Sequence->GetUnresolvableBindingPaths(Fixture.Blueprint->WidgetTree->RootWidget, Unresolvable);
	TestEqual(TEXT("the animation's path walks to the renamed widget"), Unresolvable.Num(), 0);
	return true;
}

#endif
