// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/*
 * The gate: what the designer stops being able to do once a hierarchy comes from a `.dui`.
 *
 * Two claims, and the second is the one worth the most.
 *
 * THE FIRST is that every structural entry point refuses, AND SAYS SO. Those are two facts, not one.
 * A silent refusal is a menu item that does nothing when clicked -- indistinguishable, from where the
 * author is sitting, from a bug in the drag -- and a test that only asserts "returned false" passes
 * on the silent version. So each refusal is declared through AddExpectedError, which fails the test
 * when the message does NOT appear (an expected message with zero occurrences is an error, see
 * FAutomationTestBase), and the declared pattern includes the .dui's file name, because a refusal
 * that does not name the file leaves the author with nowhere to go.
 *
 * THE SECOND is the negative control: an ordinary widget blueprint, one that names no .dui, still
 * takes all six edits AND is untouched by all five property refusals -- outside the writable set, no
 * `.dui` spelling for the value, written as a binding, expanded from a loop, and binding authoring.
 * Every gate is one bad predicate away from locking the editor for assets that have nothing to do
 * with it, and that failure would be invisible to a suite that only ever tested the locked case. The
 * control test declares no expected errors at all, which means any refusal it provokes fails it --
 * silence there is the assertion.
 *
 * The file goes through the file system for the same reason DreamUITextCompileAutomationTests does:
 * the criterion is a path on a class default object, and a fixture that set the property without a
 * file behind it would pass whether or not the compile could ever have read one.
 */

namespace DreamUITextGateTestLocal
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
			FileName = FPaths::GetCleanFilename(FilePath);
		}

		~FScopedDuiFile()
		{
			// EvenReadOnly, and quiet: a leftover file is read by the NEXT run of this test, which
			// turns a failure here into a failure over there with nothing connecting them.
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}

		FString FilePath;
		FString FileName;
	};

	/**
	 * A widget blueprint with a designer open on it, of either kind.
	 *
	 * One fixture for both the gated case and the control, deliberately: the two tests below have to
	 * differ in exactly ONE thing -- whether the class names a .dui -- and two fixtures is how a
	 * control quietly stops being a control.
	 *
	 * InDuiFilePath empty means a hand-authored hierarchy, and then the root is given a canvas panel
	 * and a child the way DreamDesignerEditingAutomationTests does, so the control has something to
	 * delete, move and duplicate.
	 */
	struct FScopedGatedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		FScopedGatedDesigner(const TCHAR* InName, UClass* InParentClass, const FString& InDuiFilePath)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				return;
			}

			if (InDuiFilePath.IsEmpty())
			{
				UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
				Tree->RootWidget->SetDisplayName(TEXT("Root"));
				// A panel on the root, so it can accept children at all.
				Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
				FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
				DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("Child"));
			}
			else
			{
				// On the CDO, because SourceFile is EditDefaultsOnly and a class default is what a
				// CDO IS. CreateBlueprint has already compiled once, so there is one to write to.
				if (UDreamTextUserWidget* Defaults = Blueprint->GeneratedClass != nullptr
					? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr)
				{
					Defaults->SourceFile.FilePath = InDuiFilePath;
				}
			}

			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			CompileErrors = Results.NumErrors;

			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false));
		}

		~FScopedGatedDesigner()
		{
			if (GEditor != nullptr && Blueprint != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				// The close is deferred, and a toolkit that is still alive still ticks. Letting the
				// package leave the root set first put a rebuild on a half-collected asset.
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedGatedDesigner(const FScopedGatedDesigner&) = delete;
		FScopedGatedDesigner& operator=(const FScopedGatedDesigner&) = delete;

		int32 CompileErrors = 0;

		UDreamWidget* TemplateRoot() const
		{
			return Blueprint != nullptr && IsValid(Blueprint->WidgetTree) ? Blueprint->WidgetTree->RootWidget.Get() : nullptr;
		}

		int32 TemplateCount() const
		{
			return Blueprint != nullptr && IsValid(Blueprint->WidgetTree) ? Blueprint->WidgetTree->CountWidgets() : 0;
		}

		UDreamWidget* FindTemplate(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && IsValid(Blueprint->WidgetTree))
			{
				Blueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}

		UDreamWidget* FindPreview(const FString& InDisplayName) const
		{
			UDreamWidget* Template = FindTemplate(InDisplayName);
			return Designer != nullptr && Designer->GetPreviewHost().IsValid() && Template != nullptr
				? Designer->GetPreviewHost()->FindPreviewForTemplate(Template) : nullptr;
		}
	};

	/** The gate's own answer, so a test asserts against the verdict rather than against a bool. */
	using EVerdict = DreamUITextAuthoring::EPropertyEditVerdict;

	/**
	 * A spelling probe for the duration of one test, removed on the way out.
	 *
	 * The probe is process-wide -- it is installed once by whoever owns the write-back -- so a test
	 * that left one behind would decide the verdicts of every test that ran after it, in whatever
	 * order the runner happened to pick. That is the shape of failure that gets diagnosed as
	 * flakiness for a week.
	 */
	struct FScopedSpellingProbe
	{
		explicit FScopedSpellingProbe(DreamUITextAuthoring::FLiteralSpellingProbe InProbe)
		{
			DreamUITextAuthoring::SetLiteralSpellingProbe(MoveTemp(InProbe));
		}

		~FScopedSpellingProbe()
		{
			DreamUITextAuthoring::SetLiteralSpellingProbe(nullptr);
		}

		FScopedSpellingProbe(const FScopedSpellingProbe&) = delete;
		FScopedSpellingProbe& operator=(const FScopedSpellingProbe&) = delete;
	};

	const FProperty* FindProperty(const UClass* InClass, const TCHAR* InName)
	{
		return InClass != nullptr ? InClass->FindPropertyByName(FName(InName)) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIDuiBackedTreeRefusesStructuralEditsTest,
	"DreamGUI.Designer.ADuiBackedTreeRefusesStructuralEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIDuiBackedTreeRefusesStructuralEditsTest::RunTest(const FString&)
{
	using namespace DreamUITextGateTestLocal;

	FScopedDuiFile Source(TEXT("GateStructural.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Child {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	// Six patterns, one per entry point, each demanding the file name as well as the refusal. This is
	// the whole assertion that the refusals are AUDIBLE: an expected message that never occurs is
	// reported as a test error, so a gate that returned false without logging fails right here.
	// Occurrences 0 -- "at least once" -- rather than an exact count: what is being asserted is that
	// each refusal was said, and pinning the number would make the test fail the day a call site
	// gains a second route to the same gate.
	// No literal space before the file name: the message spells it `in 'GateStructural.dui'`, so the
	// character in front of it is an apostrophe. The first version of these patterns asked for a
	// space, matched nothing, and every refusal counted as an unexpected error -- the gates all fired
	// correctly and the test failed anyway.
	AddExpectedError(TEXT("Refusing to create a .*GateStructural\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to delete .*GateStructural\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to move .*GateStructural\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to rename .*GateStructural\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to duplicate .*GateStructural\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to paste widgets .*GateStructural\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedGatedDesigner Scoped(TEXT("BP_GateStructural"), UDreamTextUserWidget::StaticClass(), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestTrue(TEXT("the gate sees this as text-authored"), DreamUITextAuthoring::IsTextAuthored(Scoped.Blueprint))) return false;

	UDreamWidget* Root = Scoped.TemplateRoot();
	UDreamWidget* Child = Scoped.FindTemplate(TEXT("Child"));
	if (!TestNotNull(TEXT("the file's root is in the asset"), Root)) return false;
	if (!TestNotNull(TEXT("and so is its child"), Child)) return false;
	const int32 CountBefore = Scoped.TemplateCount();

	// ---- the five in DreamWidgetTreeEditing
	TestNull(TEXT("Creating a widget is refused"),
		DreamWidgetTreeEditing::CreateWidget(Scoped.Blueprint, UDreamWidget::StaticClass(), Root, -1, TEXT("Added")));
	TestFalse(TEXT("Deleting one is refused"), DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Child));
	TestFalse(TEXT("Moving one is refused"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, Child, Root, 0));
	TestEqual(TEXT("Renaming one is refused"),
		DreamWidgetTreeEditing::RenameWidget(Scoped.Blueprint, Child, TEXT("Renamed")), FString());
	TestNull(TEXT("Duplicating one is refused"),
		DreamWidgetTreeEditing::DuplicateWidget(Scoped.Blueprint, Child, Root, -1));

	// ---- the sixth, which is not in that namespace at all
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	UDreamWidget* PreviewChild = Scoped.FindPreview(TEXT("Child"));
	if (!TestNotNull(TEXT("the child has a preview to copy"), PreviewChild)) return false;
	// Copy is deliberately NOT gated: it writes nothing to the asset, and refusing it would stop an
	// author lifting a subtree out of a .dui-backed screen to paste into a hand-authored one.
	const TArray<UDreamWidget*> ToCopy{ PreviewChild };
	Scoped.Designer->DesignerCopyWidgets(ToCopy);
	if (!TestTrue(TEXT("copying a widget out of a .dui-backed tree still works"),
		FDreamWidgetBlueprintEditor::DesignerHasClipboardContent()))
	{
		return false;
	}
	TestEqual(TEXT("Pasting into it is refused"),
		Scoped.Designer->DesignerPasteWidgets(Scoped.FindPreview(TEXT("Root"))).Num(), 0);

	// And nothing moved. Six refusals that each returned the right value while quietly doing the work
	// anyway is a state every assertion above passes in.
	TestEqual(TEXT("the hierarchy is exactly what the file says"), Scoped.TemplateCount(), CountBefore);
	TestNotNull(TEXT("the child is still there"), Scoped.FindTemplate(TEXT("Child")));
	TestNull(TEXT("under the name the file gives it"), Scoped.FindTemplate(TEXT("Renamed")));
	TestNull(TEXT("and nothing was added"), Scoped.FindTemplate(TEXT("Added")));

	// The sentence itself, once: every one of the six is built by this function, so this is where the
	// claim "the message tells the author where to go" is actually checked rather than pattern-matched.
	const FString Refusal = DreamUITextAuthoring::DescribeStructuralRefusal(Scoped.Blueprint, TEXT("delete 'Child'")).ToString();
	TestTrue(TEXT("the refusal names the file"), Refusal.Contains(Source.FileName));
	TestTrue(TEXT("and says where the structure lives"), Refusal.Contains(TEXT("text")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIHandAuthoredTreeIsUnaffectedByTheGateTest,
	"DreamGUI.Designer.AHandAuthoredTreeStillTakesEveryStructuralEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIHandAuthoredTreeIsUnaffectedByTheGateTest::RunTest(const FString&)
{
	using namespace DreamUITextGateTestLocal;

	// THE CONTROL, and the most important test in this file. It declares no expected errors on
	// purpose: the gate logs at Error, so a predicate that answered "text-authored" for an ordinary
	// widget blueprint would fail this test on the log alone, before any assertion below ran.
	FScopedGatedDesigner Scoped(TEXT("BP_GateControl"), UDreamUserWidget::StaticClass(), FString());
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestFalse(TEXT("it names no .dui"), DreamUITextAuthoring::IsTextAuthored(Scoped.Blueprint))) return false;

	UDreamWidget* Root = Scoped.TemplateRoot();
	if (!TestNotNull(TEXT("it has a root"), Root)) return false;
	UDreamWidget* Child = Scoped.FindTemplate(TEXT("Child"));
	if (!TestNotNull(TEXT("and the fixture's child"), Child)) return false;

	// ---- all five still work
	UDreamWidget* Created = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Root, -1, TEXT("Added"));
	if (!TestNotNull(TEXT("Creating a widget still works"), Created)) return false;
	TestEqual(TEXT("Renaming still works"),
		DreamWidgetTreeEditing::RenameWidget(Scoped.Blueprint, Created, TEXT("Renamed")), FString(TEXT("Renamed")));
	TestTrue(TEXT("Moving still works"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint, Created, Child, -1));
	TestNotNull(TEXT("Duplicating still works"),
		DreamWidgetTreeEditing::DuplicateWidget(Scoped.Blueprint, Created, Root, -1));
	TestTrue(TEXT("Deleting still works"), DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Created));

	// ---- and so does the sixth
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	UDreamWidget* PreviewChild = Scoped.FindPreview(TEXT("Child"));
	if (!TestNotNull(TEXT("the child has a preview"), PreviewChild)) return false;
	const TArray<UDreamWidget*> ToCopy{ PreviewChild };
	Scoped.Designer->DesignerCopyWidgets(ToCopy);
	const int32 CountBeforePaste = Scoped.TemplateCount();
	TestTrue(TEXT("Pasting still works"),
		Scoped.Designer->DesignerPasteWidgets(Scoped.FindPreview(TEXT("Root"))).Num() > 0);
	TestTrue(TEXT("and the paste reached the asset"), Scoped.TemplateCount() > CountBeforePaste);

	// The property half of the gate has to be inert here too, or every details panel in the project
	// goes grey. The properties chosen are the ones a text-authored widget refuses for four different
	// reasons -- outside the writable set (Visibility, Children), no `.dui` spelling for the value
	// (the probe below), and the transform the write-back cannot carry (RelativeRotation /
	// RelativeScale) -- so a predicate that leaked would show up on at least one of them.
	{
		// A probe that refuses EVERYTHING, so the control also proves the newest of the refusals
		// cannot reach a hand-authored asset. A gate that asked the probe before asking whether the
		// hierarchy is text-authored would fail exactly here and nowhere else.
		const FScopedSpellingProbe Probe([](const FProperty*, const void*) { return false; });

		for (const TCHAR* PropertyName : { TEXT("Visibility"), TEXT("RelativeRotation"), TEXT("RelativeScale"), TEXT("Children") })
		{
			if (const FProperty* Found = FindProperty(UDreamWidget::StaticClass(), PropertyName))
			{
				TestFalse(FString::Printf(TEXT("%s is editable on a hand-authored widget"), PropertyName),
					DreamUITextAuthoring::IsPropertyReadOnly(Child, Found, {}));
			}
		}
		TestFalse(TEXT("Custom rows stay live too, in a category a .dui would have locked"),
			DreamUITextAuthoring::IsCustomRowReadOnly(Child, NAME_None, FName(TEXT("Visual"))));
	}
	TestTrue(TEXT("Bindings can still be authored here"), DreamUITextAuthoring::CanAuthorBindingsOn(Child));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUINonWritablePropertyIsShownReadOnlyTest,
	"DreamGUI.Designer.ANonWritablePropertyIsShownReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUINonWritablePropertyIsShownReadOnlyTest::RunTest(const FString&)
{
	using namespace DreamUITextGateTestLocal;

	// A vertical box rather than a canvas: `@slot` needs a parent that hands out panel slots at all,
	// and a slot property on a parent that does not is NoPanelSlotForProperty -- a compile error that
	// would make this test fail for a reason that has nothing to do with the gate.
	FScopedDuiFile Source(TEXT("GateProperties.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox"),
		TEXT("    Text Title {"),
		TEXT("        FontSize = 24"),
		TEXT("        @slot Padding = (8, 8, 8, 8)"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	// No designer: the criterion is read off the class default object and the owning Blueprint is
	// found through the widget's outer chain, both of which a template answers on its own. Opening one
	// would add a preview world and a toolkit to a test that is about a predicate.
	FScopedGatedDesigner Scoped(TEXT("BP_GateProperties"), UDreamTextUserWidget::StaticClass(), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;

	UDreamWidget* Title = Scoped.FindTemplate(TEXT("Title"));
	if (!TestNotNull(TEXT("the file's text node is in the asset"), Title)) return false;

	// ---- the writable set: the anchor block, whole and by dotted path
	const FProperty* AnchorData = FindProperty(UDreamWidget::StaticClass(), TEXT("AnchorData"));
	if (!TestNotNull(TEXT("UDreamWidget declares AnchorData"), AnchorData)) return false;
	TestFalse(TEXT("The anchor block stays editable"),
		DreamUITextAuthoring::IsPropertyReadOnly(Title, AnchorData, {}));
	if (const FStructProperty* AsStruct = CastField<FStructProperty>(AnchorData))
	{
		// The shape the details panel actually hands over for `AnchorData.SizeDelta`: the leaf is
		// SizeDelta and only the parent chain says whose. A gate that matched on the leaf alone would
		// have locked this, and locking the size field is locking the designer.
		if (const FProperty* SizeDelta = AsStruct->Struct->FindPropertyByName(FName(TEXT("SizeDelta"))))
		{
			const TArray<const FProperty*> Chain{ AnchorData };
			TestFalse(TEXT("and so does a field inside it, reached by its chain"),
				DreamUITextAuthoring::IsPropertyReadOnly(Title, SizeDelta, Chain));
		}
	}

	// ---- the transform the viewport writes but the file cannot hold
	// CommitWidgetGeometryToTemplate mirrors these three alongside AnchorData on every mouse move,
	// and the write-back covers none of them: the language has no spelling for an FQuat or an FVector.
	// A move survives because SetRelativeLocation recomputes the anchors and the anchors ARE written;
	// a rotate or a scale would not, which is why the viewport offers no gizmo for them either.
	for (const TCHAR* TransformProperty : { TEXT("RelativeLocation"), TEXT("RelativeRotation"), TEXT("RelativeScale") })
	{
		if (const FProperty* Found = FindProperty(UDreamWidget::StaticClass(), TransformProperty))
		{
			TestTrue(FString::Printf(TEXT("%s has no .dui spelling, so it is read-only"), TransformProperty),
				DreamUITextAuthoring::IsPropertyReadOnly(Title, Found, {}));
		}
		else
		{
			AddError(FString::Printf(TEXT("UDreamWidget no longer declares %s"), TransformProperty));
		}
	}

	// ---- outside it
	const FProperty* Visibility = FindProperty(UDreamWidget::StaticClass(), TEXT("Visibility"));
	if (TestNotNull(TEXT("UDreamWidget declares Visibility"), Visibility))
	{
		TestEqual(TEXT("A widget property the write-back has no home for is read-only"),
			(uint8)DreamUITextAuthoring::GetPropertyEditVerdict(Title, Visibility, {}),
			(uint8)EVerdict::OutsideTheWritableSet);
	}
	const FProperty* Children = FindProperty(UDreamWidget::StaticClass(), TEXT("Children"));
	if (TestNotNull(TEXT("UDreamWidget declares Children"), Children))
	{
		// The hierarchy itself, which is the thing this whole gate exists to keep in the file.
		TestTrue(TEXT("and so is the child list"), DreamUITextAuthoring::IsPropertyReadOnly(Title, Children, {}));
	}

	// ---- the objects the language addresses in full stay live
	UDreamVisual* Visual = Title->GetVisual();
	if (TestNotNull(TEXT("the text node has a visual"), Visual))
	{
		const FProperty* FontSize = FindProperty(Visual->GetClass(), TEXT("FontSize"));
		if (TestNotNull(TEXT("whose class declares FontSize"), FontSize))
		{
			TestFalse(TEXT("A visual's style property is editable -- a bare line in the file writes it"),
				DreamUITextAuthoring::IsPropertyReadOnly(Visual, FontSize, {}));
		}
	}
	UDreamPanelSlot* Slot = Title->GetPanelSlot();
	if (TestNotNull(TEXT("the child of a panel has a panel slot"), Slot))
	{
		const FProperty* Padding = FindProperty(Slot->GetClass(), TEXT("Padding"));
		if (TestNotNull(TEXT("whose class declares Padding"), Padding))
		{
			TestFalse(TEXT("A slot parameter is editable -- an @slot line writes it"),
				DreamUITextAuthoring::IsPropertyReadOnly(Slot, Padding, {}));
		}
	}

	// ---- and the value that cannot be written down even though its object and its property both can
	//
	// The Font next to the FontSize. Both live on the visual, both are in the writable set, and the
	// only difference is that one of them has a `.dui` literal and the other is an object reference
	// the write-back skips rather than guessing at -- so without this, picking a new font changes the
	// preview, changes nothing in the file, and is gone at the next compile.
	//
	// The probe stands in for FDreamUITextWriteBack's PrintLiteral, which is the real judge and is
	// file-local to the write-back today. What is asserted here is the WIRING: that a probe which
	// refuses a property turns into a read-only row, and that the verdict names the reason. When the
	// real one is exported and installed, this test keeps checking the same seam.
	{
		const FScopedSpellingProbe Probe(
			[](const FProperty* InLeaf, const void*) { return CastField<FObjectPropertyBase>(InLeaf) == nullptr; });
		if (!TestTrue(TEXT("the probe is installed"), DreamUITextAuthoring::HasLiteralSpellingProbe()))
		{
			return false;
		}
		if (Visual != nullptr)
		{
			const FProperty* Font = FindProperty(Visual->GetClass(), TEXT("Font"));
			if (TestNotNull(TEXT("the visual declares Font"), Font))
			{
				TestEqual(TEXT("A value with no .dui literal is read-only"),
					(uint8)DreamUITextAuthoring::GetPropertyEditVerdict(Visual, Font, {}),
					(uint8)EVerdict::HasNoTextSpelling);
			}
			// The neighbour is untouched: this has to be per property, not per object, or greying the
			// font would grey the whole text style with it.
			if (const FProperty* FontSize = FindProperty(Visual->GetClass(), TEXT("FontSize")))
			{
				TestFalse(TEXT("while the spellable neighbour stays editable"),
					DreamUITextAuthoring::IsPropertyReadOnly(Visual, FontSize, {}));
			}
		}
		const FString Reason = DreamUITextAuthoring::DescribePropertyVerdict(
			EVerdict::HasNoTextSpelling, Source.FileName).ToString();
		TestTrue(TEXT("and the reason names the file"), Reason.Contains(Source.FileName));
	}
	TestFalse(TEXT("the probe is gone again once the test's scope ends"),
		DreamUITextAuthoring::HasLiteralSpellingProbe());

	// ---- and the custom-row half, which is the one a property-only gate misses
	TestTrue(TEXT("A custom row on the widget is read-only by default"),
		DreamUITextAuthoring::IsCustomRowReadOnly(Title, NAME_None, FName(TEXT("Visual"))));
	TestFalse(TEXT("except in the categories the write-back covers"),
		DreamUITextAuthoring::IsCustomRowReadOnly(Title, NAME_None, FName(TEXT("DreamLayout"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIBoundPropertyIsShownReadOnlyTest,
	"DreamGUI.Designer.ABoundPropertyIsShownReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIBoundPropertyIsShownReadOnlyTest::RunTest(const FString&)
{
	using namespace DreamUITextGateTestLocal;

	FScopedDuiFile Source(TEXT("GateBinding.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- GetTitleText()"),
		TEXT("        FontSize = 24"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	// The parent declares GetTitleText natively, so the `<-` line resolves and reaches
	// UDreamWidgetBlueprint::PropertyBindings -- which is the list this half of the gate reads.
	FScopedGatedDesigner Scoped(TEXT("BP_GateBinding"), UDreamTextUserWidgetBindingBase::StaticClass(), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestEqual(TEXT("the file's binding reached the asset"), Scoped.Blueprint->PropertyBindings.Num(), 1)) return false;

	UDreamWidget* Title = Scoped.FindTemplate(TEXT("Title"));
	if (!TestNotNull(TEXT("the text node is in the asset"), Title)) return false;
	UDreamVisual* Visual = Title->GetVisual();
	if (!TestNotNull(TEXT("with a visual"), Visual)) return false;

	// Text is on the VISUAL, and it is inside the writable set -- a bare `Text = "OK"` line would be
	// perfectly writable. What makes it read-only is the binding, which is why this cannot be answered
	// by the writable set alone and why the binding is tested first.
	const FProperty* Text = FindProperty(Visual->GetClass(), TEXT("Text"));
	if (TestNotNull(TEXT("the visual declares Text"), Text))
	{
		TestEqual(TEXT("A property the file drives with <- is read-only"),
			(uint8)DreamUITextAuthoring::GetPropertyEditVerdict(Visual, Text, {}),
			(uint8)EVerdict::WrittenAsABinding);
		TestTrue(TEXT("and the gate says so as a binding, not as an unwritable value"),
			DreamUITextAuthoring::IsPropertyWrittenAsABinding(Visual, Text));
		// The reason has to be visible, not merely true: a row that is grey for a reason nobody can
		// read is the same failure as a row that is grey for no reason.
		const FString Reason = DreamUITextAuthoring::DescribePropertyVerdict(
			EVerdict::WrittenAsABinding, Source.FileName).ToString();
		TestTrue(TEXT("the reason names the file"), Reason.Contains(Source.FileName));
	}

	// Its neighbour on the same object is not bound, and must stay editable -- a gate that locked the
	// whole object once one property was bound would be indistinguishable, from the outside, from one
	// that worked.
	const FProperty* FontSize = FindProperty(Visual->GetClass(), TEXT("FontSize"));
	if (TestNotNull(TEXT("the visual declares FontSize"), FontSize))
	{
		TestFalse(TEXT("An unbound property on the same visual stays editable"),
			DreamUITextAuthoring::IsPropertyReadOnly(Visual, FontSize, {}));
	}

	// The third block: authoring a NEW binding here. Not a write-back problem at all -- the compiler
	// replaces the whole binding list from the file, so a binding made in the panel is deleted rather
	// than merely not saved, and the Create Binding entry would leave an orphan function behind.
	TestFalse(TEXT("The details panel cannot author bindings on a text-authored class"),
		DreamUITextAuthoring::CanAuthorBindingsOn(Visual));
	TestFalse(TEXT("nor on the widget itself"), DreamUITextAuthoring::CanAuthorBindingsOn(Title));

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextSetSourceFileTest,
	"DreamGUI.WidgetBlueprint.TheSourceFileCanBeSetFromTheEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The empty state, and the way out of it.
 *
 * A Blueprint parented to UDreamTextUserWidget with no path set is where every text-backed widget
 * starts, and for a while it was a dead end: the designer showed a blank hierarchy, the gate did not
 * lock (correctly -- there is no text to be the truth yet), and the one property that would fix it
 * was three clicks away in another mode. The two claims here are what make the toolbar entry work:
 * that a class with no path is still recognised as text-capable, and that setting a path recompiles
 * rather than merely storing a string.
 */
bool FDreamUITextSetSourceFileTest::RunTest(const FString&)
{
	using namespace DreamUITextGateTestLocal;

	FScopedDuiFile File(TEXT("GateSetSource.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.Write({
		TEXT("Widget Root {"),
		TEXT("  + CanvasPanel { }"),
		TEXT("  Text Title { }"),
		TEXT("}"),
	})))
	{
		return false;
	}

	// Created with NO path, which is the state the toolbar entry exists for.
	FScopedGatedDesigner Scoped(TEXT("GateSetSource"), UDreamTextUserWidget::StaticClass(), FString());
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer))
	{
		return false;
	}

	TestTrue(TEXT("a text class with no path is still text-CAPABLE"),
		DreamUITextAuthoring::CanAuthorFromText(Scoped.Blueprint));
	TestFalse(TEXT("but is not yet text-AUTHORED, so the structural gate stays open"),
		DreamUITextAuthoring::IsTextAuthored(Scoped.Blueprint));

	TestTrue(TEXT("the path was accepted"),
		DreamUITextAuthoring::SetAuthoredSourcePath(Scoped.Blueprint, File.FilePath));
	TestTrue(TEXT("and the class now reads as text-authored"),
		DreamUITextAuthoring::IsTextAuthored(Scoped.Blueprint));

	// The claim that matters: SetAuthoredSourcePath compiles. Storing the string and stopping is the
	// shape that produces "I set the file and nothing happened".
	TestNotNull(TEXT("and the compile it ran built the file's hierarchy"),
		Scoped.FindTemplate(TEXT("Title")));

	// Setting the same path again is a no-op that still reports success: the caller asked for a
	// state and got it. Asserted because the alternative -- returning false -- would read as an
	// error at the call site and put a failure notification on an idempotent click.
	TestTrue(TEXT("setting the same path again is accepted and changes nothing"),
		DreamUITextAuthoring::SetAuthoredSourcePath(Scoped.Blueprint, File.FilePath));

	// A hand-authored class cannot hold one, and must say so rather than writing to its parent's CDO
	// -- which would set the source file of every Blueprint deriving from UDreamUserWidget.
	FScopedGatedDesigner Plain(TEXT("GateSetSourcePlain"), UDreamUserWidget::StaticClass(), FString());
	if (TestNotNull(TEXT("the control designer opened"), Plain.Designer))
	{
		TestFalse(TEXT("a hand-authored class is not text-capable"),
			DreamUITextAuthoring::CanAuthorFromText(Plain.Blueprint));
		TestFalse(TEXT("and refuses the path rather than writing it somewhere shared"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Plain.Blueprint, File.FilePath));
		TestTrue(TEXT("leaving UDreamTextUserWidget's own default untouched"),
			GetDefault<UDreamTextUserWidget>()->SourceFile.FilePath.IsEmpty());
	}
	return true;
}

#endif
