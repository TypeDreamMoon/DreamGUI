// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"
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
	 * A hand-built stand-in for a widget blueprint's archetype: a face and the hole on it, named the
	 * way UDreamButton::CollectParts names them, and nothing else. No UUIButton anywhere,
	 * deliberately -- a template's author draws a look, and being handed the behaviour is the point.
	 *
	 * Neither part is where the built-in tree puts it: the face is two levels down under a root this
	 * control has no name for, and the hole is wrapped in a row. A bind that quietly depended on the
	 * built-in SHAPE rather than on the names would fail here.
	 */
	UDreamWidgetTree* MakeTemplateTree()
	{
		using namespace DreamUI;

		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
		Tree->RootWidget = Realize(Tree,
			Widget("Shell")
				.Stretch()
				.With<UDreamLayoutContainerVerticalBox>()
				.Children(
					Node<UDreamRectBlock>("Face")
						.With<UDreamLayoutContainerHorizontalBox>()
						.Children(
							Widget("Row")
								.With<UDreamLayoutContainerHorizontalBox>()
								.Children(
									Widget("Content")
										.With<UDreamNamedSlot>()))));
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
	// No project sheet under a test, so ResolveStyle falls back to the inline Style without
	// StyleSource being touched -- the same arrangement the rest of the control suite relies on.
	Button->Style.CornerRadius = 9.0f;
	Button->Style.Normal = FColor(11, 22, 33, 255);
	Button->InitializeFromArchetype(MakeTemplateTree());

	// Instanced, not borrowed: the parts have to be this control's own to drive, which is why a
	// template's TREE is what gets used rather than the template being placed as a nested widget.
	if (!TestNotNull(TEXT("the supplied tree was instanced"), Button->WidgetTree.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the face was found by name, two levels down"), Button->FaceNode.Get()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("and so was the optional hole"), Button->ContentNode.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the parts belong to the instanced tree, not the archetype"),
		Button->FaceNode->IsIn(Button.Get()));

	// The shape is the template author's, and the binder did not assume the built-in one: in the
	// code tree the face IS the root and the hole is its direct child.
	TestTrue(TEXT("the face sits where the template put it, not where the built-in tree does"),
		Button->FaceNode->GetParent() != Button.Get());
	TestTrue(TEXT("and so does the hole"),
		Button->ContentNode->GetParent() != Button->FaceNode.Get());

	// A face somebody drew is not a button until this happens, and nothing else in the pipeline
	// would have said so -- an unclickable button raises no error anywhere.
	if (!TestNotNull(TEXT("the behaviour was added to a face that had none"), Button->ButtonBehaviour.Get()))
	{
		return false;
	}
	TestTrue(TEXT("and it went on the face itself"),
		(UObject*)Button->ButtonBehaviour->GetWidget() == (UObject*)Button->FaceNode.Get());

	// The style push drives the template's widgets exactly as it drives the built-in ones. These are
	// the assertions that would go quiet rather than red if a part were left unbound: both numbers
	// are written THROUGH the bound face, one onto its visual and one onto the behaviour standing on
	// it, and a null part is silently skipped by every writer in ApplyStyle.
	if (UDreamRectBlock* FaceRect = Cast<UDreamRectBlock>(Button->FaceNode->GetVisual()))
	{
		TestEqual(TEXT("the style's radius reached the template's face"),
			FaceRect->GetCornerRadius().X, 9.0f);
	}
	else
	{
		AddError(TEXT("the template's face has no rect visual to shape"));
	}
	TestEqual(TEXT("and the style's colour reached the behaviour standing on it"),
		Button->ButtonBehaviour->GetNormalColor(), FColor(11, 22, 33, 255));

	// The size box is the built-in tree's, not this control's to impose: a template's author drew
	// their own container and the padding and floor go unpushed rather than overruling it.
	TestNull(TEXT("the template's own container was left alone"),
		Cast<UDreamLayoutContainerSizeBox>(Button->FaceNode->GetLayoutContainer()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTemplateMissingPartTest,
	"DreamGUI.Controls.Template.AMissingRequiredPartIsReportedByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTemplateMissingPartTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;

	// A template with a hole and no face. Perfectly loadable, and every writer to FaceNode
	// null-checks, so without the report this is a button that is never shaped, never skinned and --
	// because WireParts puts the UUIButton on the face -- cannot be clicked, all in silence.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	Tree->RootWidget = Realize(Tree,
		Widget("Content")
			.Stretch()
			.With<UDreamNamedSlot>());

	AddExpectedError(TEXT("found no part named 'Face'"), EAutomationExpectedErrorFlags::Contains, 1);

	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->InitializeFromArchetype(Tree);

	// Optional parts are not reported, which is why the expected-error count above is exactly one:
	// a template that offers no content hole is a perfectly good button, and this one offers it.
	TestNotNull(TEXT("the part that IS there still bound"), Button->ContentNode.Get());
	TestNull(TEXT("and the missing one is null rather than guessed at"), Button->FaceNode.Get());
	TestNull(TEXT("so the behaviour has nowhere to go and is not invented"), Button->ButtonBehaviour.Get());

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
