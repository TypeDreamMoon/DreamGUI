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

#endif
