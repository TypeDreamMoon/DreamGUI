// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "UObject/UObjectIterator.h"
#include "DreamControlTestScope.h"
#include "UObject/Package.h"

/*
 * A control driving a hierarchy it did not write.
 *
 * A control used to reach its parts through the builder's .Out(), which writes a pointer at
 * construction -- so the pointers only ever pointed into a tree this code built, and "use somebody
 * else's tree" was not a thing that could be expressed. Splitting the four steps apart is what
 * changed that: build a tree (RealizeBuiltIn, or a template), bind the parts BY NAME (CollectParts),
 * put the behaviours on whatever those turned out to be (WireParts), then style it. Only step one
 * differs between the two roads.
 *
 * This file drives the second road through UDreamUserWidget::InitializeFromArchetype -- the entry
 * point the designer's preview already uses, and the honest way to hand a widget contents that came
 * from somewhere other than its own class. A Template property set to a widget blueprint arrives at
 * the same place: RealizeTemplate finds that class's archetype and instances it exactly as this
 * does, then takes the "a tree already arrived" branch on the way through.
 *
 * What has to be true, and is the whole claim:
 *
 *   - the parts are found by NAME in the supplied tree, not by construction order or by type
 *   - the behaviour the control guarantees is ADDED to a face that never had one
 *   - the style push then drives that tree as if the control had built it
 *
 * The last one is the one that would rot quietly. A part left null does not crash -- every writer
 * null-checks -- it just never changes, which looks like a styling bug forever.
 */
namespace DreamControlTemplateTestLocal
{
	/**
	 * A hand-built stand-in for a widget blueprint's archetype: a face and a label, named the way
	 * UDreamButton::CollectParts names them, and nothing else. No UUIButton anywhere, deliberately --
	 * a template's author draws a look, and being handed the behaviour is the point.
	 *
	 * Nested one level deeper than the built-in tree, and the label wrapped in a row, so that a bind
	 * that quietly depended on the built-in SHAPE rather than on the names would fail here.
	 */
	UDreamWidgetTree* MakeTemplateTree()
	{
		using namespace DreamUI;

		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
		Tree->RootWidget = Realize(Tree,
			Node<UDreamRectBlock>("Face")
				.Stretch()
				.With<UDreamLayoutContainerVerticalBox>()
				.Children(
					Widget("Row")
						.With<UDreamLayoutContainerHorizontalBox>()
						.Children(
							Text("Label"))));
		return Tree;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTemplatePartsTest,
	"DreamGUI.Controls.Template.PartsAreFoundByNameInATreeTheControlDidNotWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTemplatePartsTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlTemplateTestLocal;

	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->Label = FText::FromString(TEXT("OK"));
	Button->InitializeFromArchetype(MakeTemplateTree());

	// Instanced, not borrowed: the parts have to be this control's own to drive, which is why a
	// template's TREE is what gets used rather than the template being placed as a nested widget.
	if (!TestNotNull(TEXT("the supplied tree was instanced"), Button->WidgetTree.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the face was found by name"), Button->FaceNode.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("and the label, two levels down"), Button->LabelNode.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the parts belong to the instanced tree, not the archetype"),
		Button->FaceNode->IsIn(Button.Get()));

	// The shape is the template author's, and the binder did not assume the built-in one: in the
	// code tree the label is a direct child of the face.
	TestTrue(TEXT("the label sits where the template put it, not where the built-in tree does"),
		Button->LabelNode->GetParent() != Button->FaceNode.Get());

	// A face somebody drew is not a button until this happens, and nothing else in the pipeline
	// would have said so -- an unclickable button raises no error anywhere.
	if (!TestNotNull(TEXT("the behaviour was added to a face that had none"), Button->ButtonBehaviour.Get()))
	{
		return false;
	}
	TestTrue(TEXT("and it went on the face itself"),
		(UObject*)Button->ButtonBehaviour->GetWidget() == (UObject*)Button->FaceNode.Get());

	// The style push drives the template's widgets exactly as it drives the built-in ones. This is
	// the assertion that would go quiet rather than red if a part were left unbound.
	if (UDreamText* LabelVisual = Cast<UDreamText>(Button->LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the control's text reached the template's label"),
			LabelVisual->GetText().ToString(), FString(TEXT("OK")));
	}
	else
	{
		AddError(TEXT("the template's label has no text visual to write to"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTemplateMissingPartTest,
	"DreamGUI.Controls.Template.AMissingRequiredPartIsReportedByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTemplateMissingPartTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;

	// A template with a face and no label. Perfectly loadable, and every writer to LabelNode
	// null-checks, so without the report this is a button whose text silently never appears.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	Tree->RootWidget = Realize(Tree, Node<UDreamRectBlock>("Face").Stretch());

	AddExpectedError(TEXT("found no part named 'Label'"), EAutomationExpectedErrorFlags::Contains, 1);

	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->InitializeFromArchetype(Tree);

	TestNotNull(TEXT("the part that IS there still bound"), Button->FaceNode.Get());
	TestNull(TEXT("and the missing one is null rather than guessed at"), Button->LabelNode.Get());
	// Optional parts are not reported, which is why the expected-error count above is exactly one:
	// a template that offers no content hole is a perfectly good button.
	TestNull(TEXT("the optional hole is simply absent"), Button->ContentNode.Get());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlPartNamesTest,
	"DreamGUI.Controls.Template.EveryControlsPartListMatchesTheTreeItBuilds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlPartNamesTest::RunTest(const FString& Parameters)
{
	// CollectParts and RealizeBuiltIn are two places that spell the same names, and nothing in the
	// language makes them agree: a part naming a node the tree does not have binds to null on BOTH
	// roads, and every writer to it null-checks, so the control comes up looking right and drives
	// nothing. This sweep is what makes that a red test instead of a bug report months later.
	//
	// It found three the first time it ran -- a dropdown asking for "List" when the node is called
	// "ListRoot", and two more in the spin box and the text input.
	int32 Checked = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UDreamUIControl::StaticClass())
			|| Class == UDreamUIControl::StaticClass()
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		// Native only. A Blueprint subclass brings its own tree, which is the TEMPLATE road, and
		// whether somebody's asset happens to name every part is not this codebase's claim to make.
		if (Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			continue;
		}

		TDreamTestControl<UDreamUIControl> Control(NewObject<UDreamUIControl>(GetTransientPackage(), Class));
		Control->Initialize();
		++Checked;

		const TArray<FName> Missing = Control->GetUnboundRequiredParts();
		if (Missing.Num() > 0)
		{
			TArray<FString> Names;
			for (const FName& Name : Missing)
			{
				Names.Add(Name.ToString());
			}
			AddError(FString::Printf(TEXT("%s names %d part(s) its own tree does not have: %s"),
				*Class->GetName(), Missing.Num(), *FString::Join(Names, TEXT(", "))));
		}
	}

	// A sweep that swept nothing passes for the wrong reason -- if the iteration ever stops finding
	// controls, this is what says so rather than reporting a clean run over an empty set.
	TestTrue(TEXT("the sweep actually found controls to check"), Checked >= 15);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
