// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetReference.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"
#include "Designer/DreamWidgetHierarchyPickerView.h"
#include "Kismet2/CompilerResultsLog.h"

#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

/*
 * The designer's data half: the authored tree, the preview built from it, and the correspondence.
 *
 * The load-bearing claim is the correspondence. Everything the designer does -- selecting, showing
 * properties, dragging, mirroring an edit back -- is a lookup from one half of a pair to the other,
 * and the pair is matched by object FName. Nothing enforces that names survive instancing; it is a
 * property of FObjectInstancingGraph that the whole surface is built on. So it is asserted here,
 * over a hierarchy deep and wide enough that a partial walk would show up, rather than assumed.
 *
 * These run headless. What they cannot see is pixels: a preview that registers, lays out and draws
 * nothing would pass every assertion below. That gap is real and is covered in the designer phases
 * by looking at the thing.
 *
 * It is not for want of trying. UDreamCanvas::GetDrawCallCount() is exactly the number that would
 * have caught the blank preview -- the canvas building no geometry at all -- and under -nullrhi it
 * is zero for a working preview too, so a test written on it fails on correct code. The defect it
 * would have caught (a rebuild that never marked the canvas, so the designer drew only on the first
 * open in an editor session) was found by opening the editor and fixed in RebuildPreview.
 */

namespace DreamWidgetDesignerHostTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint != nullptr)
			{
				Blueprint->GetOrCreateWidgetTree();
			}
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		void Compile() const
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		}
	};

	/** A root with a vertical box, three children under it, and a grandchild. Deep AND wide on purpose. */
	void BuildSampleHierarchy(UDreamWidgetBlueprint* InBlueprint)
	{
		UDreamWidgetTree* Tree = InBlueprint->GetOrCreateWidgetTree();
		Tree->RootWidget->SetDisplayName(TEXT("Root"));
		Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());

		UDreamWidget* First = DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("First"));
		DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("Second"));
		DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("Third"));
		DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamWidget::StaticClass(), First, -1, TEXT("Grandchild"));
	}

	UDreamWidget* FindTemplateByDisplayName(const UDreamWidgetBlueprint* InBlueprint, const FString& InDisplayName)
	{
		UDreamWidget* Found = nullptr;
		InBlueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
		{
			if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
			{
				Found = Widget;
			}
		});
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPreviewMatchesTemplateTest,
	"DreamGUI.Designer.PreviewAnswersForEveryTemplateWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPreviewMatchesTemplateTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	FScopedBlueprint Scoped(TEXT("DesignerPreviewMatch"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);

	TestNotNull(TEXT("The preview world exists"), Host->GetWorld());
	TestNotNull(TEXT("The design canvas exists"), Host->GetRootAgent());
	if (!TestNotNull(TEXT("The preview widget was built"), Host->GetPreviewWidget()))
	{
		Host->Shutdown();
		return false;
	}

	// Every template widget has exactly one preview counterpart, of the same class, and the map
	// round-trips. This is the assumption the entire designer surface rests on.
	int32 TemplateCount = 0;
	Scoped.Blueprint->WidgetTree->ForEachWidget([&](UDreamWidget* Template)
	{
		TemplateCount++;
		UDreamWidget* Preview = Host->FindPreviewForTemplate(Template);
		if (TestNotNull(*FString::Printf(TEXT("'%s' has a preview counterpart"), *Template->GetDisplayName()), Preview))
		{
			TestNotEqual(TEXT("The preview is a different object from the template"), (void*)Preview, (void*)Template);
			TestEqual(TEXT("The counterpart has the same class"), Preview->GetClass(), Template->GetClass());
			TestEqual(TEXT("The counterpart has the same display name"), Preview->GetDisplayName(), Template->GetDisplayName());
			TestEqual(TEXT("The map round-trips"), Host->FindTemplateForPreview(Preview), Template);
		}
	});
	TestEqual(TEXT("The sample hierarchy has five widgets"), TemplateCount, 5);

	// And the shape came across, not just the membership.
	if (UDreamWidget* PreviewFirst = Host->FindPreviewForTemplate(FindTemplateByDisplayName(Scoped.Blueprint, TEXT("First"))))
	{
		TestEqual(TEXT("First keeps its one child in the preview"), PreviewFirst->GetChildrenCount(), 1);
	}
	TestEqual(TEXT("The preview root is the counterpart of the template root"),
		Host->GetPreviewRoot(), Host->FindPreviewForTemplate(Scoped.Blueprint->WidgetTree->RootWidget));
	TestEqual(TEXT("The preview root hangs under the design canvas"),
		Host->GetPreviewWidget()->GetParent(), Host->GetRootAgent());

	Host->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPreviewFollowsStructureTest,
	"DreamGUI.Designer.PreviewFollowsAnAuthoringEditWithoutARecompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPreviewFollowsStructureTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	FScopedBlueprint Scoped(TEXT("DesignerPreviewFollows"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);

	// The point of instancing the AUTHORING tree rather than the class archetype: no compile here.
	UDreamWidget* Added = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("AddedAfterCompile"));
	if (!TestNotNull(TEXT("The widget was added to the authoring tree"), Added))
	{
		Host->Shutdown();
		return false;
	}
	TestNull(TEXT("Before the rebuild the preview does not have it yet"), Host->FindPreviewForTemplate(Added));

	Host->InvalidatePreview();
	Host->RebuildPreviewIfInvalidated();
	TestNotNull(TEXT("After the rebuild the preview has it, with no compile in between"),
		Host->FindPreviewForTemplate(Added));

	// And a removal comes across the same way.
	DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Added);
	Host->RebuildPreview();
	TestNull(TEXT("A deleted widget leaves the preview"), Host->FindPreviewForTemplate(Added));

	Host->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerTreeEditingTest,
	"DreamGUI.Designer.StructuralEditsAreCorrectAndUndoable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerTreeEditingTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	// This test asks for two refusals on purpose, and every refusal in the editing API now says so
	// out loud -- a command that returns false without a word is a menu item that does nothing when
	// clicked. So the refusals it provokes have to be declared here, or the very thing under test
	// counts as a failure.
	AddExpectedError(TEXT("into itself or into something it contains"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("not part of .* authoring tree"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedBlueprint Scoped(TEXT("DesignerTreeEditing"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);

	UDreamWidgetTree* Tree = Scoped.Blueprint->WidgetTree;
	UDreamWidget* Root = Tree->RootWidget;
	UDreamWidget* First = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("First"));
	UDreamWidget* Second = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("Second"));
	UDreamWidget* Grandchild = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("Grandchild"));

	// The root carries a vertical box, so its children get panel slots -- authored data that has to
	// exist on the TEMPLATE, since that is what the class archetype is duplicated from.
	TestNotNull(TEXT("A child of a panel gets a panel slot on the template"), First->GetPanelSlot());
	TestNull(TEXT("A child of a plain widget gets none"), Grandchild->GetPanelSlot());

	// A duplicate name is disambiguated rather than allowed to collapse two compiler variables.
	UDreamWidget* Duplicate = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("First"));
	TestEqual(TEXT("A clashing display name is suffixed"), Duplicate->GetDisplayName(), FString(TEXT("First_1")));

	// Refusals.
	TestFalse(TEXT("The root cannot be deleted"), DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Root));
	TestFalse(TEXT("A widget cannot be parented into its own subtree"),
		DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, First, Grandchild));
	{
		// A widget that belongs to a different asset must be refused rather than silently adopted:
		// reparenting it would leave it outered to the other Blueprint's tree while appearing in this
		// hierarchy, which saves one asset's widget into another's package.
		FScopedBlueprint Other(TEXT("DesignerTreeEditingOther"));
		BuildSampleHierarchy(Other.Blueprint);
		UDreamWidget* Foreign = FindTemplateByDisplayName(Other.Blueprint, TEXT("Second"));
		TestFalse(TEXT("A widget from another hierarchy is refused"),
			DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, Foreign, Root));
		TestFalse(TEXT("So is deleting one"), DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Foreign));
	}

	// Reparent, then reorder within the same parent.
	TestTrue(TEXT("Grandchild moves to the root"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, Grandchild, Root, 0));
	TestEqual(TEXT("It arrived at index 0"), Root->GetChildIndex(Grandchild), 0);
	TestEqual(TEXT("It left its old parent"), First->GetChildrenCount(), 0);
	TestNotNull(TEXT("Moving under a panel gave it a panel slot"), Grandchild->GetPanelSlot());
	TestTrue(TEXT("Reordering inside the same parent is allowed"),
		DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, Grandchild, Root, 2));
	TestEqual(TEXT("It moved to index 2"), Root->GetChildIndex(Grandchild), 2);

	// Undo. The transaction has to restore the parent's Children array, which is why every one of
	// these snapshots the parent as well as the child.
	const int32 ChildCountBefore = Root->GetChildrenCount();
	{
		FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "DeleteWidget", "Delete Widget"));
		DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Second);
	}
	TestEqual(TEXT("The delete took effect"), Root->GetChildrenCount(), ChildCountBefore - 1);
	GEditor->UndoTransaction();
	TestEqual(TEXT("Undo put the widget back"), Root->GetChildrenCount(), ChildCountBefore);
	TestNotNull(TEXT("And it is the same widget"), FindTemplateByDisplayName(Scoped.Blueprint, TEXT("Second")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPropertyMirrorTest,
	"DreamGUI.Designer.APreviewEditIsMirroredOntoTheTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPropertyMirrorTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	FScopedBlueprint Scoped(TEXT("DesignerPropertyMirror"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);

	UDreamWidget* Template = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("Second"));
	UDreamWidget* Preview = Host->FindPreviewForTemplate(Template);
	if (!TestNotNull(TEXT("The widget has a preview"), Preview))
	{
		Host->Shutdown();
		return false;
	}

	// The chain a details panel would hand over for an edit to the widget's own anchor data.
	FProperty* AnchorDataProperty = UDreamWidget::StaticClass()->FindPropertyByName(
		UDreamWidget::GetPropertyName_AnchorData());
	if (!TestNotNull(TEXT("The anchor data property was found"), AnchorDataProperty))
	{
		Host->Shutdown();
		return false;
	}

	Preview->SetSizeDelta(FVector2D(321.0, 123.0));
	TestNotEqual(TEXT("The template has not been touched yet"), (float)Template->GetSizeDelta().X, 321.0f, 0.001f);

	FEditPropertyChain Chain;
	Chain.AddHead(AnchorDataProperty);
	TestTrue(TEXT("The mirror reports it wrote"), Host->MigratePropertyToTemplate(Preview, Chain, /*bIsModify*/false));
	TestEqual(TEXT("The template carries the edited width"), (float)Template->GetSizeDelta().X, 321.0f, 0.001f);
	TestEqual(TEXT("The template carries the edited height"), (float)Template->GetSizeDelta().Y, 123.0f, 0.001f);

	// And it survives a rebuild, which is the whole reason the mirror exists: the preview object the
	// value was typed into is about to be destroyed.
	Host->RebuildPreview();
	UDreamWidget* RebuiltPreview = Host->FindPreviewForTemplate(Template);
	if (TestNotNull(TEXT("The widget still has a preview after the rebuild"), RebuiltPreview))
	{
		TestEqual(TEXT("The rebuilt preview carries the width"), (float)RebuiltPreview->GetSizeDelta().X, 321.0f, 0.001f);
		TestEqual(TEXT("The rebuilt preview carries the height"), (float)RebuiltPreview->GetSizeDelta().Y, 123.0f, 0.001f);
	}

	// Anything in the preview world that is not part of the authored hierarchy has no template, and
	// asking to mirror it is answered rather than crashed on.
	TestFalse(TEXT("The design canvas has no template to mirror onto"),
		Host->MigratePropertyToTemplate(Host->GetRootAgent(), Chain, /*bIsModify*/false));

	Host->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSurfaceReparentReachesTemplateTest,
	"DreamGUI.Designer.ASurfaceReparentReachesTheTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerSurfaceReparentReachesTemplateTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	// Dragging a widget into another container on the design surface moves a PREVIEW widget, and the
	// preview is rebuilt from the authoring tree. Without the mirroring half the move looks right,
	// survives until the next rebuild, and then is silently gone -- which is indistinguishable from
	// "the drag did not take" and impossible to notice from a structure test of the preview alone.
	FScopedBlueprint Scoped(TEXT("DesignerSurfaceReparent"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);
	// Give First a panel so it can accept a child at all.
	UDreamWidget* FirstTemplate = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("First"));
	FirstTemplate->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);

	UDreamWidget* SecondTemplate = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("Second"));
	UDreamWidget* PreviewSecond = Host->FindPreviewForTemplate(SecondTemplate);
	UDreamWidget* PreviewFirst = Host->FindPreviewForTemplate(FirstTemplate);
	if (!TestNotNull(TEXT("Both widgets have previews"), PreviewSecond) || PreviewFirst == nullptr)
	{
		Host->Shutdown();
		return false;
	}
	TestEqual(TEXT("Second starts under the root"), SecondTemplate->GetParent(), Scoped.Blueprint->WidgetTree->RootWidget.Get());

	// What the surface does: move the preview, then mirror. Done here without the viewport client,
	// because what is being claimed is the mirroring, not Slate.
	PreviewSecond->TrySetParent(PreviewFirst, /*bKeepWorldPosition*/true);
	TestEqual(TEXT("The preview moved"), PreviewSecond->GetParent(), PreviewFirst);
	TestEqual(TEXT("But the template has not, yet"), SecondTemplate->GetParent(), Scoped.Blueprint->WidgetTree->RootWidget.Get());

	const int32 SiblingIndex = PreviewFirst->GetChildIndex(PreviewSecond);
	TestTrue(TEXT("The mirror reports it moved something"),
		DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, SecondTemplate, FirstTemplate, SiblingIndex));
	TestEqual(TEXT("The template moved too"), SecondTemplate->GetParent(), FirstTemplate);

	// And it survives the rebuild, which is the whole point.
	Host->RebuildPreview();
	UDreamWidget* RebuiltSecond = Host->FindPreviewForTemplate(SecondTemplate);
	if (TestNotNull(TEXT("Second still has a preview"), RebuiltSecond))
	{
		UDreamWidget* RebuiltFirst = Host->FindPreviewForTemplate(FirstTemplate);
		TestEqual(TEXT("And the rebuilt preview kept the new parent"), RebuiltSecond->GetParent(), RebuiltFirst);
	}

	Host->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerReferenceLifetimeTest,
	"DreamGUI.Designer.AReferenceSurvivesRebuildsAndReportsDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerReferenceLifetimeTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	FScopedBlueprint Scoped(TEXT("DesignerReferenceLifetime"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);

	UDreamWidget* Template = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("Third"));
	FDreamWidgetReference Reference = Host->GetReferenceFromTemplate(Template);
	TestTrue(TEXT("A fresh reference is valid"), Reference.IsValid());

	UDreamWidget* PreviewBefore = Reference.GetPreview();
	Host->RebuildPreview();
	TestTrue(TEXT("It is still valid after a rebuild"), Reference.IsValid());
	TestNotEqual(TEXT("And it now resolves to the NEW preview object"), (void*)Reference.GetPreview(), (void*)PreviewBefore);
	TestEqual(TEXT("While the template is unchanged"), Reference.GetTemplate(), Template);

	// Identity is the template, so a second reference to the same widget is the same reference.
	TestTrue(TEXT("Two references to one template compare equal"), Reference == Host->GetReferenceFromTemplate(Template));

	// A widget the author deleted: the template is still reachable through the reference (the undo
	// buffer is holding it) but nothing in the preview answers for it any more.
	DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Template);
	Host->RebuildPreview();
	TestFalse(TEXT("A deleted widget's reference reports invalid"), Reference.IsValid());
	TestNull(TEXT("And has no preview"), Reference.GetPreview());

	// A default-constructed one answers rather than crashes.
	FDreamWidgetReference Empty;
	TestFalse(TEXT("An empty reference is invalid"), Empty.IsValid());
	TestNull(TEXT("An empty reference has no template"), Empty.GetTemplate());
	TestNull(TEXT("An empty reference has no preview"), Empty.GetPreview());

	Host->Shutdown();
	return true;
}

/*
 * Duplicating a widget copies what is under it.
 *
 * The duplicate that shipped is DuplicateObject onto the tree, and the assumption in that one line is
 * that a widget's children come with it. They are not its subobjects: every widget in a tree is
 * outered flat to the UDreamWidgetTree, and duplication follows the OUTER chain, not the Children
 * array. Whether the children are copied, shared, or dropped is therefore a property of the engine,
 * not of the code that reads as if it had decided -- and RestoreParentLinksRecursive on a copy that
 * SHARED them would walk the originals and re-parent them onto the copy, taking them out of the
 * hierarchy they were in.
 *
 * Every existing duplicate test duplicates a leaf, so none of them can tell those three apart.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeEditingDuplicateCopiesTheSubtreeTest,
	"DreamGUI.Designer.TreeEditing.DuplicatingAWidgetCopiesItsChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeEditingDuplicateCopiesTheSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetDesignerHostTestLocal;
	FScopedBlueprint Scoped(TEXT("DuplicateSubtreeInTree"));
	if (!TestNotNull(TEXT("the blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	UDreamWidgetTree* Tree = Scoped.Blueprint->WidgetTree;
	UDreamWidget* Root = Tree->RootWidget.Get();

	UDreamWidget* Panel = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Root, -1, TEXT("Panel"));
	UDreamWidget* ChildA = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Panel, -1, TEXT("ChildA"));
	UDreamWidget* Grandchild = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), ChildA, -1, TEXT("Grandchild"));
	DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Panel, -1, TEXT("ChildB"));
	if (!TestNotNull(TEXT("the subtree was built"), Grandchild))
	{
		return false;
	}
	const int32 CountBefore = Tree->CountWidgets();

	UDreamWidget* Copy = DreamWidgetTreeEditing::DuplicateWidget(Scoped.Blueprint, Panel, Root, -1);
	if (!TestNotNull(TEXT("duplicating returned a widget"), Copy))
	{
		return false;
	}

	// Four widgets went in, four more should be in the tree.
	TestEqual(TEXT("the whole subtree was copied, not just its root"), Tree->CountWidgets(), CountBefore + 4);
	TestEqual(TEXT("the copy has both children"), Copy->GetChildren().Num(), 2);

	// The half a shared-children duplicate would fail: the original keeps what it had.
	TestEqual(TEXT("and the original still has both of its own"), Panel->GetChildren().Num(), 2);
	TestEqual(TEXT("ChildA is still under Panel"), ChildA->GetParent(), Panel);
	TestEqual(TEXT("and still has its grandchild"), ChildA->GetChildren().Num(), 1);

	for (UDreamWidget* Child : Copy->GetChildren())
	{
		TestFalse(TEXT("no child object is shared between the original and the copy"),
			Panel->GetChildren().Contains(Child));
		TestEqual(TEXT("every copied child points at the copy"), Child->GetParent(), Copy);
	}
	return true;
}

/*
 * A tree that came off disk knows who its parents are.
 *
 * Parent is transient. Serialization carries Children and drops every back-pointer, and the doc on
 * RebuildParentLinks says as much -- "required after any path that produces a tree without going
 * through the attach functions: package load, class template instancing, subtree duplication". The
 * compiled tree got it from the compiler and the instanced one from the generated class. The
 * AUTHORING tree on the Blueprint, the one the designer edits, came straight off disk and had nobody
 * to do it, so GetParent() was null for every widget in every saved asset.
 *
 * Nothing headless noticed because a test builds its tree with CreateWidget, which attaches live. The
 * symptom in the editor was Duplicate on a loaded asset doing nothing at all, silently: it asks the
 * template for its parent, and refused when there was none.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTreeRestoresParentsOnLoadTest,
	"DreamGUI.Designer.ATreeFromDiskKnowsItsParents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTreeRestoresParentsOnLoadTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	FScopedBlueprint Scoped(TEXT("TreeFromDisk"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}
	BuildSampleHierarchy(Scoped.Blueprint);

	// A duplicate is the closest thing to a load that a test can make: Parent is DuplicateTransient
	// as well as Transient, so the copy arrives in exactly the state a package load leaves behind.
	UDreamWidgetTree* AsLoaded = DuplicateObject<UDreamWidgetTree>(Scoped.Blueprint->WidgetTree, Scoped.Blueprint);
	if (!TestNotNull(TEXT("the tree was copied"), AsLoaded) || AsLoaded->RootWidget == nullptr)
	{
		return false;
	}
	UDreamWidget* FirstChild = AsLoaded->RootWidget->GetChildren().Num() > 0 ? AsLoaded->RootWidget->GetChildren()[0] : nullptr;
	if (!TestNotNull(TEXT("and carries its children"), FirstChild))
	{
		return false;
	}
	// No negative control is possible here and that is the point: the copy cannot be observed without
	// its links, because the same PostLoad that fixes a load fixes a duplicate. On the code this was
	// written against, GetParent() here is null and the assertion fails.
	TestEqual(TEXT("a tree that arrived without the attach path still knows its parents"),
		FirstChild->GetParent(), AsLoaded->RootWidget.Get());

	// And the consequence that was actually visible: the designer can edit such a tree.
	Scoped.Blueprint->WidgetTree = AsLoaded;
	TestNotNull(TEXT("a widget in a loaded tree can be duplicated"),
		DreamWidgetTreeEditing::DuplicateWidget(Scoped.Blueprint, FirstChild, AsLoaded->RootWidget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerNestedInstanceIsFoldedTest,
	"DreamGUI.Designer.Folding.ANestedInstanceIsOneRowAndPairsAsOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerNestedInstanceIsFoldedTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	// Two assets, the way a real project has them: a small reusable one, and a screen that places it.
	FScopedBlueprint Inner(TEXT("FoldingInnerControl"));
	if (!TestNotNull(TEXT("the inner Blueprint was created"), Inner.Blueprint))return false;
	Inner.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("InnerRoot"));
	Inner.Blueprint->GetOrCreateWidgetTree()->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamWidget* InnerLabel = DreamWidgetTreeEditing::CreateWidget(Inner.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("InnerLabel"));
	Inner.Compile();
	if (!TestNotNull(TEXT("the inner class compiled"), (UObject*)Inner.Blueprint->GeneratedClass))return false;

	FScopedBlueprint Host(TEXT("FoldingHostScreen"));
	if (!TestNotNull(TEXT("the host Blueprint was created"), Host.Blueprint))return false;
	UDreamWidgetTree* HostTree = Host.Blueprint->GetOrCreateWidgetTree();
	HostTree->RootWidget->SetDisplayName(TEXT("HostRoot"));
	HostTree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());

	// The collision, built rather than hoped for: every asset's object names start at DreamWidget_0
	// once it has been to disk, so a host widget and a widget inside a nested control share one. In a
	// single process the counter never repeats, which is exactly why no test had ever seen this.
	const FName CollidingName = InnerLabel->GetFName();
	UDreamWidget* HostTwin = HostTree->ConstructWidget(UDreamWidget::StaticClass(), CollidingName);
	if (!TestNotNull(TEXT("a host widget could take the inner widget's object name"), (UObject*)HostTwin))return false;
	HostTwin->SetDisplayName(TEXT("HostTwin"));
	HostTwin->SetParentBeforeRegister(HostTree->RootWidget);
	TestEqual(TEXT("and really did take it"), HostTwin->GetFName(), CollidingName);

	UDreamWidget* NestedNode = HostTree->ConstructWidget(TSubclassOf<UDreamWidget>(Inner.Blueprint->GeneratedClass));
	if (!TestNotNull(TEXT("the nested instance node was authored"), (UObject*)NestedNode))return false;
	NestedNode->SetDisplayName(TEXT("NestedControl"));
	NestedNode->SetParentBeforeRegister(HostTree->RootWidget);
	Host.Compile();

	TSharedRef<FDreamWidgetPreviewHost> PreviewHost = MakeShared<FDreamWidgetPreviewHost>();
	PreviewHost->Initialize(Host.Blueprint);
	if (!TestNotNull(TEXT("the preview was built"), PreviewHost->GetPreviewWidget()))
	{
		PreviewHost->Shutdown();
		return false;
	}

	UDreamWidget* PreviewNested = PreviewHost->FindPreviewForTemplate(NestedNode);
	if (!TestNotNull(TEXT("the nested instance itself pairs -- it IS a widget of this asset"), PreviewNested))
	{
		PreviewHost->Shutdown();
		return false;
	}
	TestEqual(TEXT("and pairs back"), PreviewHost->FindTemplateForPreview(PreviewNested), NestedNode);

	// It expanded its own contents, which is what makes the fold necessary rather than cosmetic.
	TestTrue(TEXT("the nested instance built its contents"), PreviewNested->GetChildrenCount() > 0);
	TestFalse(TEXT("but the designer stops there"), DreamWidget_ShouldEditorExpandContents(PreviewNested));
	TestTrue(TEXT("while the host's own widgets still expand"), DreamWidget_ShouldEditorExpandContents(PreviewHost->GetPreviewRoot()));

	TArray<UDreamWidget*> Shown;
	CollectDreamWidgetsToNestedBoundary(PreviewHost->GetPreviewRoot(), Shown);
	TestTrue(TEXT("the nested instance is shown"), Shown.Contains(PreviewNested));
	for (UDreamWidget* Inside : PreviewNested->GetChildren())
	{
		TestFalse(TEXT("nothing inside it is"), Shown.Contains(Inside));
	}

	// The reason the fold had to reach the pairing too. Something inside the nested control shares an
	// object name with a host widget; by name it resolved to the host's HostTwin, so editing a value
	// on it wrote onto an unrelated widget of this asset, silently.
	UDreamWidget* InsideWithCollidingName = nullptr;
	TArray<UDreamWidget*> Everything;
	UDreamWidget::CollectChildrenWidgets(PreviewNested, Everything, false);
	for (UDreamWidget* Candidate : Everything)
	{
		if (IsValid(Candidate) && Candidate->GetFName() == CollidingName)
		{
			InsideWithCollidingName = Candidate;
		}
	}
	if (TestNotNull(TEXT("the collision reached the preview"), (UObject*)InsideWithCollidingName))
	{
		TestNull(TEXT("a widget inside a nested control belongs to no template of this asset"),
			PreviewHost->FindTemplateForPreview(InsideWithCollidingName));
	}
	TestEqual(TEXT("and the host's own widget still pairs with itself"),
		PreviewHost->FindTemplateForPreview(PreviewHost->FindPreviewForTemplate(HostTwin)), HostTwin);

	PreviewHost->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerIdentityCarriesToPreviewTest,
	"DreamGUI.Designer.Identity.InstancingCarriesTheIdToThePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerIdentityCarriesToPreviewTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	// The premise the whole pairing rests on: a preview widget is NewObject'd with its authored
	// counterpart as archetype, so a plain UPROPERTY comes across untouched. If that ever stops being
	// true the designer pairs nothing at all, and this is the test that says so.
	FScopedBlueprint Scoped(TEXT("IdentityCarries"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))return false;
	BuildSampleHierarchy(Scoped.Blueprint);
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);
	if (!TestNotNull(TEXT("the preview was built"), Host->GetPreviewWidget()))
	{
		Host->Shutdown();
		return false;
	}

	TSet<FGuid> SeenIds;
	Scoped.Blueprint->WidgetTree->ForEachWidget([&](UDreamWidget* Template)
	{
		if (!TestTrue(*FString::Printf(TEXT("'%s' has an id"), *Template->GetDisplayName()), Template->GetWidgetGuid().IsValid()))
		{
			return;
		}
		bool bAlreadySeen = false;
		SeenIds.Add(Template->GetWidgetGuid(), &bAlreadySeen);
		TestFalse(TEXT("no two authored widgets share an id"), bAlreadySeen);
		if (UDreamWidget* Preview = Host->FindPreviewForTemplate(Template))
		{
			TestEqual(TEXT("the preview carries the same id"), Preview->GetWidgetGuid(), Template->GetWidgetGuid());
		}
	});

	Host->Shutdown();
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerUndoLeavesNoDoubleTest,
	"DreamGUI.Designer.UndoingADuplicateLeavesNothingReachableTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerUndoLeavesNoDoubleTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}

	FScopedBlueprint Scoped(TEXT("UndoDuplicateNoDouble"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))return false;
	BuildSampleHierarchy(Scoped.Blueprint);
	UDreamWidgetTree* Tree = Scoped.Blueprint->WidgetTree;
	UDreamWidget* First = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("First"));
	if (!TestNotNull(TEXT("the sample tree has a First"), (UObject*)First))return false;

	// Every widget in the tree, counting SLOTS and reporting anything reached twice. CountWidgets
	// keeps a visited set and skips revisits, so it reports the same number for a healthy tree and
	// for one where a widget hangs off two parents -- which is exactly the state under test.
	// Counts SLOTS, and reports a slot holding something that is not a live widget rather than
	// stepping over it. Filtering on IsValid is what made the first version of this test pass on
	// broken data: an object invalidated by undo leaves an entry behind, the entry is invisible to
	// every IsValid-guarded walk in the codebase, and object instancing does not filter at all -- so
	// the next preview rebuilt it into a live duplicate. Count the slots.
	struct FWalk
	{
		TArray<FString> Revisits;
		TArray<FString> DeadSlots;
		int32 Slots = 0;
		TSet<const UDreamWidget*> Seen;
		void Run(UDreamWidget* Widget)
		{
			Slots++;
			bool bAlreadySeen = false;
			Seen.Add(Widget, &bAlreadySeen);
			if (bAlreadySeen)
			{
				Revisits.Add(Widget->GetDisplayName());
				return;
			}
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				if (!IsValid(Child))
				{
					DeadSlots.Add(FString::Printf(TEXT("under '%s'"), *Widget->GetDisplayName()));
					continue;
				}
				Run(Child);
			}
		}
	};

	FWalk Before;
	Before.Run(Tree->RootWidget);
	TestEqual(TEXT("the sample tree starts sound"), Before.Revisits.Num(), 0);

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Duplicate")));
	UDreamWidget* Copy = DreamWidgetTreeEditing::DuplicateWidget(Scoped.Blueprint, First, Tree->RootWidget);
	GEditor->EndTransaction();
	if (!TestNotNull(TEXT("the duplicate happened"), (UObject*)Copy))return false;
	FWalk Duplicated;
	Duplicated.Run(Tree->RootWidget);
	TestEqual(TEXT("and added First plus its child"), Duplicated.Slots, Before.Slots + 2);

	GEditor->UndoTransaction();

	// The claim. Undo restores Children; UDreamWidget::Parent is Transient and is NOT restored, so
	// anything that reads the back-pointer afterwards is reading a pointer the transaction never
	// touched. PostEditUndo did exactly that -- it re-inserted itself into whatever its stale Parent
	// still named -- which put the undone copy back into the tree it had just been removed from.
	// In the editor that surfaced as a cycle out of RestoreParentLinksRecursive on the next preview
	// rebuild, two identical rows in the hierarchy panel, and before the row collection was hardened,
	// an SListView check(false) that took the editor down.
	FWalk Undone;
	Undone.Run(Tree->RootWidget);
	TestEqual(*FString::Printf(TEXT("nothing is reachable twice, saw [%s]"), *FString::Join(Undone.Revisits, TEXT(", "))),
		Undone.Revisits.Num(), 0);
	TestEqual(*FString::Printf(TEXT("and no dead slot is left behind, saw [%s]"), *FString::Join(Undone.DeadSlots, TEXT(", "))),
		Undone.DeadSlots.Num(), 0);
	TestEqual(TEXT("and the tree is the size it started"), Undone.Slots, Before.Slots);

	// The back-pointers have to agree with the restored Children too: every child names the parent
	// that lists it. A tree that walks correctly while Parent points somewhere else is the state the
	// hierarchy panel drops rows over.
	for (const UDreamWidget* Widget : Undone.Seen)
	{
		for (const UDreamWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child))
			{
				TestEqual(*FString::Printf(TEXT("'%s' names the parent that lists it"), *Child->GetDisplayName()),
					(const UDreamWidget*)Child->GetParent(), Widget);
			}
		}
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerUndoWithAPreviewTest,
	"DreamGUI.Designer.UndoingADuplicateWithAPreviewOpenLeavesBothTreesSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerUndoWithAPreviewTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}

	// The same undo as the test above, with the one thing the editor has that it does not: a live
	// preview. The damage seen by hand showed up in the PREVIEW tree -- RestoreParentLinksRecursive
	// reporting a cycle while instancing it -- so the preview is either the victim or the cause, and
	// a test without one cannot tell which.
	FScopedBlueprint Scoped(TEXT("UndoDuplicateWithPreview"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))return false;
	BuildSampleHierarchy(Scoped.Blueprint);
	Scoped.Compile();

	TSharedRef<FDreamWidgetPreviewHost> Host = MakeShared<FDreamWidgetPreviewHost>();
	Host->Initialize(Scoped.Blueprint);
	if (!TestNotNull(TEXT("the preview was built"), Host->GetPreviewWidget()))
	{
		Host->Shutdown();
		return false;
	}

	// Counts SLOTS, and reports a slot holding something that is not a live widget rather than
	// stepping over it. Filtering on IsValid is what made the first version of this test pass on
	// broken data: an object invalidated by undo leaves an entry behind, the entry is invisible to
	// every IsValid-guarded walk in the codebase, and object instancing does not filter at all -- so
	// the next preview rebuilt it into a live duplicate. Count the slots.
	struct FWalk
	{
		TArray<FString> Revisits;
		TArray<FString> DeadSlots;
		int32 Slots = 0;
		TSet<const UDreamWidget*> Seen;
		void Run(UDreamWidget* Widget)
		{
			Slots++;
			bool bAlreadySeen = false;
			Seen.Add(Widget, &bAlreadySeen);
			if (bAlreadySeen)
			{
				Revisits.Add(Widget->GetDisplayName());
				return;
			}
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				if (!IsValid(Child))
				{
					DeadSlots.Add(FString::Printf(TEXT("under '%s'"), *Widget->GetDisplayName()));
					continue;
				}
				Run(Child);
			}
		}
	};

	UDreamWidgetTree* Tree = Scoped.Blueprint->WidgetTree;
	UDreamWidget* First = FindTemplateByDisplayName(Scoped.Blueprint, TEXT("First"));
	FWalk BeforeTemplate;  BeforeTemplate.Run(Tree->RootWidget);
	FWalk BeforePreview;   BeforePreview.Run(Host->GetPreviewRoot());

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Duplicate")));
	UDreamWidget* Copy = DreamWidgetTreeEditing::DuplicateWidget(Scoped.Blueprint, First, Tree->RootWidget);
	GEditor->EndTransaction();
	if (!TestNotNull(TEXT("the duplicate happened"), (UObject*)Copy))
	{
		Host->Shutdown();
		return false;
	}
	Host->RebuildPreview();
	FWalk WithCopy; WithCopy.Run(Host->GetPreviewRoot());
	TestEqual(TEXT("the preview shows the copy"), WithCopy.Slots, BeforePreview.Slots + 2);

	GEditor->UndoTransaction();
	Host->RebuildPreview();

	FWalk AfterTemplate; AfterTemplate.Run(Tree->RootWidget);
	TestEqual(*FString::Printf(TEXT("the authored tree has nothing reachable twice, saw [%s]"),
		*FString::Join(AfterTemplate.Revisits, TEXT(", "))), AfterTemplate.Revisits.Num(), 0);
	TestEqual(*FString::Printf(TEXT("and no dead slot in it, saw [%s]"),
		*FString::Join(AfterTemplate.DeadSlots, TEXT(", "))), AfterTemplate.DeadSlots.Num(), 0);
	TestEqual(TEXT("and is the size it started"), AfterTemplate.Slots, BeforeTemplate.Slots);

	FWalk AfterPreview; AfterPreview.Run(Host->GetPreviewRoot());
	TestEqual(*FString::Printf(TEXT("the preview has nothing reachable twice, saw [%s]"),
		*FString::Join(AfterPreview.Revisits, TEXT(", "))), AfterPreview.Revisits.Num(), 0);
	TestEqual(TEXT("and is the size it started"), AfterPreview.Slots, BeforePreview.Slots);

	Host->Shutdown();
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNamedSlotRoundTripTest,
	"DreamGUI.Designer.NamedSlot.AHostFillsAHoleTheClassDeclared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNamedSlotRoundTripTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	// A shell class with a hole in it: a root and a Body carrying UDreamNamedSlot. This is the case
	// folding alone cannot serve -- a Card or a dialogue shell is only useful when the parent
	// supplies the middle.
	FScopedBlueprint Shell(TEXT("NamedSlotShell"));
	if (!TestNotNull(TEXT("the shell Blueprint was created"), Shell.Blueprint))return false;
	UDreamWidgetTree* ShellTree = Shell.Blueprint->GetOrCreateWidgetTree();
	ShellTree->RootWidget->SetDisplayName(TEXT("ShellRoot"));
	ShellTree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamWidget* Body = DreamWidgetTreeEditing::CreateWidget(Shell.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("Body"));
	if (!TestNotNull(TEXT("the shell has a Body to make a slot of"), (UObject*)Body))return false;
	Body->AddComponent<UDreamNamedSlot>();
	Shell.Compile();

	TArray<FName> Declared;
	UDreamUserWidget::CollectDeclaredSlotNames(ShellTree, Declared);
	if (!TestEqual(TEXT("the class declares one slot"), Declared.Num(), 1))return false;
	TestEqual(TEXT("named after the widget carrying it"), Declared[0], FName(TEXT("Body")));

	// The host places the shell and authors the content that goes in it. The content is the HOST's
	// widget -- that is what keeps this from being a cross-asset difference record.
	FScopedBlueprint Host(TEXT("NamedSlotHostScreen"));
	if (!TestNotNull(TEXT("the host Blueprint was created"), Host.Blueprint))return false;
	UDreamWidgetTree* HostTree = Host.Blueprint->GetOrCreateWidgetTree();
	HostTree->RootWidget->SetDisplayName(TEXT("HostRoot"));
	HostTree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamUserWidget* Placed = Cast<UDreamUserWidget>(HostTree->ConstructWidget(TSubclassOf<UDreamWidget>(Shell.Blueprint->GeneratedClass)));
	if (!TestNotNull(TEXT("the shell was placed in the host"), (UObject*)Placed))return false;
	Placed->SetDisplayName(TEXT("Card"));
	Placed->SetParentBeforeRegister(HostTree->RootWidget);

	UDreamWidget* Filling = HostTree->ConstructWidget<UDreamWidget>();
	Filling->SetDisplayName(TEXT("Filling"));
	TestTrue(TEXT("the host can bind its own widget to the slot"), Placed->SetContentForNamedSlot(TEXT("Body"), Filling));
	TestEqual(TEXT("and reads it back"), Placed->GetContentForNamedSlot(TEXT("Body")), Filling);

	// Content from somewhere else is refused, loudly. A slot filled from a third asset would be the
	// cross-asset difference record the prefab model was, and is not coming back.
	AddExpectedError(TEXT("belongs to another hierarchy"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("but not a widget from another hierarchy"),
		Placed->SetContentForNamedSlot(TEXT("Body"), ShellTree->RootWidget));

	// The tree has to see it, or the compiler declares no variable for it and nothing mints its id.
	int32 Seen = 0;
	HostTree->ForEachWidget([&Seen, Filling](UDreamWidget* W) { if (W == Filling) { Seen++; } });
	TestEqual(TEXT("the host tree walk reaches the slot content"), Seen, 1);
	Host.Compile();

	// And the instance actually puts it there.
	TSharedRef<FDreamWidgetPreviewHost> PreviewHost = MakeShared<FDreamWidgetPreviewHost>();
	PreviewHost->Initialize(Host.Blueprint);
	if (!TestNotNull(TEXT("the preview was built"), PreviewHost->GetPreviewWidget()))
	{
		PreviewHost->Shutdown();
		return false;
	}
	UDreamUserWidget* PreviewCard = Cast<UDreamUserWidget>(PreviewHost->FindPreviewForTemplate(Placed));
	if (!TestNotNull(TEXT("the placed shell has a preview"), (UObject*)PreviewCard))
	{
		PreviewHost->Shutdown();
		return false;
	}
	UDreamWidget* PreviewSlot = PreviewCard->FindSlotWidget(TEXT("Body"));
	UDreamWidget* PreviewFilling = PreviewHost->FindPreviewForTemplate(Filling);
	if (TestNotNull(TEXT("the slot exists in the instance"), (UObject*)PreviewSlot)
		&& TestNotNull(TEXT("and the content was instanced too"), (UObject*)PreviewFilling))
	{
		TestEqual(TEXT("the content hangs in the slot"), PreviewFilling->GetParent(), PreviewSlot);
	}

	// The designer surface: the instance is still one row, but the hole shows, and what is in it is
	// the host's to edit. A slot the author cannot see is a slot nobody uses.
	TArray<UDreamWidget*> Rows;
	CollectDreamEditorChildren(PreviewCard, Rows);
	TestEqual(TEXT("the folded instance shows exactly its slots"), Rows.Num(), 1);
	if (Rows.Num() == 1)
	{
		TestEqual(TEXT("which is the slot widget"), Rows[0], PreviewSlot);
		TArray<UDreamWidget*> Under;
		CollectDreamEditorChildren(Rows[0], Under);
		TestTrue(TEXT("and the content shows under it"), Under.Contains(PreviewFilling));
	}
	// Nothing of the shell own hierarchy is reachable from here.
	TArray<UDreamWidget*> Everything;
	CollectDreamWidgetsToNestedBoundary(PreviewHost->GetPreviewRoot(), Everything);
	TestFalse(TEXT("the shell content root is not shown"),
		Everything.Contains(PreviewCard->GetContentRoot()));
	TestTrue(TEXT("but the host content in the slot is"), Everything.Contains(PreviewFilling));

	PreviewHost->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNamedSlotUndeclaredIsAnErrorTest,
	"DreamGUI.Designer.NamedSlot.BindingASlotTheClassDoesNotDeclareIsACompileError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNamedSlotUndeclaredIsAnErrorTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	FScopedBlueprint Shell(TEXT("NamedSlotNoSlots"));
	if (!TestNotNull(TEXT("the shell Blueprint was created"), Shell.Blueprint))return false;
	Shell.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("ShellRoot"));
	Shell.Compile();

	FScopedBlueprint Host(TEXT("NamedSlotWrongName"));
	if (!TestNotNull(TEXT("the host Blueprint was created"), Host.Blueprint))return false;
	UDreamWidgetTree* HostTree = Host.Blueprint->GetOrCreateWidgetTree();
	HostTree->RootWidget->SetDisplayName(TEXT("HostRoot"));
	HostTree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamUserWidget* Placed = Cast<UDreamUserWidget>(HostTree->ConstructWidget(TSubclassOf<UDreamWidget>(Shell.Blueprint->GeneratedClass)));
	if (!TestNotNull(TEXT("the shell was placed"), (UObject*)Placed))return false;
	Placed->SetDisplayName(TEXT("Card"));
	Placed->SetParentBeforeRegister(HostTree->RootWidget);
	UDreamWidget* Filling = HostTree->ConstructWidget<UDreamWidget>();
	Filling->SetDisplayName(TEXT("Filling"));
	Placed->SetContentForNamedSlot(TEXT("NoSuchSlot"), Filling);

	// A class can drop or rename a slot long after the host bound content to it. Dropping that
	// content silently is how a screen loses a piece with nothing in the log to say why.
	// The compiler routes its errors through the log as well, and this test is asserting that one is
	// raised -- so it has to say it expects it, or the framework counts the very thing under test as
	// a failure. Occurrences 0: skeleton and full compiles each report it.
	AddExpectedError(TEXT("declares no such slot"), EAutomationExpectedErrorFlags::Contains, 0);
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Host.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	bool bReported = false;
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		if (Message->ToText().ToString().Contains(TEXT("declares no such slot")))
		{
			bReported = true;
		}
	}
	TestTrue(TEXT("the compiler says so, naming both the instance and the slot"), bReported);
	TestTrue(TEXT("and it is an error, not a warning"), Results.NumErrors > 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCreateIsUndoableTest,
	"DreamGUI.Designer.CreatingAWidgetInTheTreeIsUndoable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerCreateIsUndoableTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}

	// COVERAGE BOUNDARY: this pins that the WRITE is recorded. The defect found by dragging a
	// control out of the palette was the other half -- FDreamWidgetBlueprintEditor::DesignerCreateWidget
	// opened no transaction at all, so there was nothing on the stack to pop and the user's next
	// Ctrl+Z reached past the drop into their earlier work. That call needs a live editor toolkit,
	// which no test here can build; it is verified by hand in the editor.
	FScopedBlueprint Scoped(TEXT("CreateIsUndoable"));
	if (!TestNotNull(TEXT("Blueprint was created"), Scoped.Blueprint))return false;
	BuildSampleHierarchy(Scoped.Blueprint);
	UDreamWidgetTree* Tree = Scoped.Blueprint->WidgetTree;
	const int32 Before = Tree->RootWidget->GetChildrenCount();

	GEditor->BeginTransaction(FText::FromString(TEXT("Test Create")));
	UDreamWidget* Created = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Tree->RootWidget, -1, TEXT("Fourth"));
	GEditor->EndTransaction();
	if (!TestNotNull(TEXT("the widget was created"), (UObject*)Created))return false;
	TestEqual(TEXT("and attached"), Tree->RootWidget->GetChildrenCount(), Before + 1);

	GEditor->UndoTransaction();
	// Slots, not live children: an object created inside a transaction is invalidated by the undo,
	// so counting only the valid ones reports success while a dead entry sits in the array.
	TestEqual(TEXT("undo takes it back out, leaving no slot behind"), Tree->RootWidget->GetChildrenCount(), Before);
	return true;
}


// Declared here rather than included: it lives beside a Slate class no headless test can construct.
bool DreamWidgetAnimation_CanBindWidgetToSequencer(const UDreamWidget* InWidget);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimationBindingStopsAtTheBoundaryTest,
	"DreamGUI.Designer.Animation.ABindingStopsWhereTheInstanceDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnimationBindingStopsAtTheBoundaryTest::RunTest(const FString&)
{
	using namespace DreamWidgetDesignerHostTestLocal;

	// A shell with a hole, placed in a host: the one shape that has widgets of BOTH kinds sitting
	// inside the same nested instance -- the shell's own, and the host's, in the slot.
	FScopedBlueprint Shell(TEXT("AnimBindShell"));
	if (!TestNotNull(TEXT("the shell Blueprint was created"), Shell.Blueprint))return false;
	UDreamWidgetTree* ShellTree = Shell.Blueprint->GetOrCreateWidgetTree();
	ShellTree->RootWidget->SetDisplayName(TEXT("ShellRoot"));
	ShellTree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamWidget* Body = DreamWidgetTreeEditing::CreateWidget(Shell.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("Body"));
	if (!TestNotNull(TEXT("the shell has a Body"), (UObject*)Body))return false;
	Body->AddComponent<UDreamNamedSlot>();
	UDreamWidget* ShellOwn = DreamWidgetTreeEditing::CreateWidget(Shell.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("ShellOwn"));
	if (!TestNotNull(TEXT("and something of its own"), (UObject*)ShellOwn))return false;
	Shell.Compile();

	FScopedBlueprint Host(TEXT("AnimBindHost"));
	if (!TestNotNull(TEXT("the host Blueprint was created"), Host.Blueprint))return false;
	UDreamWidgetTree* HostTree = Host.Blueprint->GetOrCreateWidgetTree();
	HostTree->RootWidget->SetDisplayName(TEXT("HostRoot"));
	HostTree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamUserWidget* Placed = Cast<UDreamUserWidget>(HostTree->ConstructWidget(TSubclassOf<UDreamWidget>(Shell.Blueprint->GeneratedClass)));
	if (!TestNotNull(TEXT("the shell was placed"), (UObject*)Placed))return false;
	Placed->SetDisplayName(TEXT("Card"));
	Placed->SetParentBeforeRegister(HostTree->RootWidget);
	UDreamWidget* Filling = HostTree->ConstructWidget<UDreamWidget>();
	Filling->SetDisplayName(TEXT("Filling"));
	Placed->SetContentForNamedSlot(TEXT("Body"), Filling);
	Host.Compile();

	TSharedRef<FDreamWidgetPreviewHost> PreviewHost = MakeShared<FDreamWidgetPreviewHost>();
	PreviewHost->Initialize(Host.Blueprint);
	if (!TestNotNull(TEXT("the preview was built"), PreviewHost->GetPreviewWidget()))
	{
		PreviewHost->Shutdown();
		return false;
	}
	UDreamUserWidget* PreviewCard = Cast<UDreamUserWidget>(PreviewHost->FindPreviewForTemplate(Placed));
	UDreamWidget* PreviewFilling = PreviewHost->FindPreviewForTemplate(Filling);
	UDreamWidget* PreviewHostRoot = PreviewHost->GetPreviewRoot();
	if (!TestNotNull(TEXT("the placed shell has a preview"), (UObject*)PreviewCard)
		|| !TestNotNull(TEXT("and so does the slot content"), (UObject*)PreviewFilling))
	{
		PreviewHost->Shutdown();
		return false;
	}
	UDreamWidget* InsideShell = nullptr;
	TArray<UDreamWidget*> Everything;
	UDreamWidget::CollectChildrenWidgets(PreviewCard, Everything, false);
	for (UDreamWidget* W : Everything)
	{
		if (IsValid(W) && W->GetDisplayName() == TEXT("ShellOwn"))
		{
			InsideShell = W;
		}
	}

	// The host's own widgets, including the nested instance itself, are the host's to animate.
	TestTrue(TEXT("the host root is bindable"), DreamWidgetAnimation_CanBindWidgetToSequencer(PreviewHostRoot));
	TestTrue(TEXT("the nested instance itself is bindable -- it IS a widget of this asset"),
		DreamWidgetAnimation_CanBindWidgetToSequencer(PreviewCard));

	// What is inside it is not. A binding is a chain of display names resolved at play time, so one
	// reaching in here is a name path through another asset, and that asset renaming a widget breaks
	// it with nothing in this asset to report it.
	if (TestNotNull(TEXT("the shell's own widget reached the preview"), (UObject*)InsideShell))
	{
		TestFalse(TEXT("but a widget inside the nested instance is not bindable"),
			DreamWidgetAnimation_CanBindWidgetToSequencer(InsideShell));
	}

	// And the exception that makes the rule worth stating: slot content sits inside the nested
	// instance's subtree and is still the HOST's widget. Animating your own widget is the point.
	TestTrue(TEXT("named-slot content stays bindable"),
		DreamWidgetAnimation_CanBindWidgetToSequencer(PreviewFilling));

	// The picker that offers widgets to bind draws the same boundary, which is the half that was
	// missing: the panel folded the instance while this list went on showing its innards.
	TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>> Roots;
	DreamWidgetHierarchyPicker_BuildRoots({ PreviewHostRoot }, UDreamWidget::StaticClass(), Roots);
	TArray<FString> Offered;
	TFunction<void(const TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>&)> Walk =
		[&Offered, &Walk](const TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>& Item)
	{
		if (!Item.IsValid())return;
		Offered.Add(Item->DisplayText);
		for (const TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>& Child : Item->Children)
		{
			Walk(Child);
		}
	};
	for (const TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>& Root : Roots)
	{
		Walk(Root);
	}
	TestTrue(FString::Printf(TEXT("the picker offers the nested instance, saw [%s]"), *FString::Join(Offered, TEXT(", "))),
		Offered.Contains(TEXT("Card")));
	TestFalse(FString::Printf(TEXT("but not what is inside it, saw [%s]"), *FString::Join(Offered, TEXT(", "))),
		Offered.Contains(TEXT("ShellOwn")));

	PreviewHost->Shutdown();
	return true;
}


#endif
