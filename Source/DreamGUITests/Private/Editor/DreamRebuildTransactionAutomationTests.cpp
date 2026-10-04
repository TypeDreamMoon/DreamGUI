// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

/*
 * The rebuild a frame after a widget class compiles, while the user holds a transaction open.
 *
 * Transactions span frames -- a slider dragged, a gizmo moved, a Sequencer key held -- and the rebuild runs from a
 * ticker, so it can land inside one. Whatever it Modify()'d then went into the user's edit, and undoing the edit put it
 * back: the designer's canvas got its old children back, without the preview it had just been given. The rebuild now
 * runs with no transaction to record into, so the step holds the user's edit and nothing of the preview.
 */
namespace DreamRebuildTransactionTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		explicit FScopedDesigner(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				return;
			}
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->SetDisplayName(TEXT("Root"));
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false));
		}

		~FScopedDesigner()
		{
			if (GEditor != nullptr && Blueprint != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				// The close is deferred, and a toolkit still alive still ticks.
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		UDreamWidget* PreviewRoot() const { return Designer != nullptr ? Designer->GetPreviewRootWidget() : nullptr; }
	};

	/**
	 * A tree owner the recompile lets go of and builds again, standing in for all of them -- a level's host, a screen, the
	 * designer's preview -- with a rebuild that Modify()s something undoable, as attaching a tree to a placed canvas does.
	 * Without it the step could hold nothing only because nothing the designer's preview rebuilt happened to be
	 * transactional. Enrolled for as long as it lives.
	 */
	class FRecordingRecompilePreview : public IDreamRecompilePreview
	{
	public:
		FRecordingRecompilePreview(const UDreamWidgetBlueprint* InBlueprint, UObject* InTouched)
			: Blueprint(InBlueprint)
			, Touched(InTouched)
		{
			if (UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get())
			{
				EditorSubsystem->RegisterPreview(this);
			}
		}

		virtual ~FRecordingRecompilePreview() override
		{
			if (UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get())
			{
				EditorSubsystem->UnregisterPreview(this);
			}
		}

		virtual bool UsesClass(const UClass* InClass) const override
		{
			const UDreamWidgetBlueprint* Owner = Blueprint.Get();
			return Owner != nullptr && InClass != nullptr && InClass == Owner->GeneratedClass.Get();
		}

		virtual void ReleaseForRecompile() override
		{
		}

		virtual void RebuildAfterRecompile() override
		{
			if (UObject* Object = Touched.Get())
			{
				Object->Modify();
				++Rebuilds;
			}
		}

		TWeakObjectPtr<const UDreamWidgetBlueprint> Blueprint;
		TWeakObjectPtr<UObject> Touched;
		int32 Rebuilds = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRebuildInsideAnOpenTransactionTest,
	"DreamGUI.Editor.Recompile.TheRebuildRecordsNothingIntoAnOpenTransactionAndSurvivesItsUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRebuildInsideAnOpenTransactionTest::RunTest(const FString& Parameters)
{
	using namespace DreamRebuildTransactionTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	FScopedDesigner Scoped(TEXT("RebuildInsideTransaction"));
	UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	if (!TestNotNull(TEXT("a designer opened on the blueprint"), Scoped.Designer)
		|| !TestNotNull(TEXT("the editor's DreamGUI subsystem exists"), EditorSubsystem)
		|| !TestNotNull(TEXT("the designer shows a preview"), Scoped.PreviewRoot()))
	{
		return false;
	}

	// What the user is in the middle of editing: an object outside the blueprint, so the undo below has a step to take
	// and gives the designer no reason to rebuild its preview over the one under test.
	UPackage* EditedPackage = CreatePackage(TEXT("/Temp/DreamGUITests/RebuildInsideTransactionEdited"));
	TStrongObjectPtr<UDreamGradientAsset> Edited(NewObject<UDreamGradientAsset>(EditedPackage, NAME_None, RF_Transactional));
	const FDreamGradient EditedBefore = Edited->GetGradient();
	// And something the rebuild itself Modify()s, through a tree owner of its own (see FRecordingRecompilePreview).
	UPackage* ProbePackage = CreatePackage(TEXT("/Temp/DreamGUITests/RebuildInsideTransactionProbe"));
	TStrongObjectPtr<UDreamGradientAsset> ProbeTarget(NewObject<UDreamGradientAsset>(ProbePackage, NAME_None, RF_Transactional));
	FRecordingRecompilePreview Probe(Scoped.Blueprint, ProbeTarget.Get());

	// The compile lets the preview go; the rebuild is what the ticker does a frame later -- here, with the edit still open.
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	if (!TestTrue(TEXT("the compile left the preview waiting to be built again"), EditorSubsystem->HasPendingRebuild()))
	{
		return false;
	}
	GEditor->BeginTransaction(FText::FromString(TEXT("Test Drag")));
	Edited->Modify();
	FDreamGradient EditedAfter;
	EditedAfter.Stops.Add(FDreamGradientStop(0.0f, FColor::Red));
	EditedAfter.Stops.Add(FDreamGradientStop(1.0f, FColor::Blue));
	Edited->SetGradient(EditedAfter);
	EditorSubsystem->RebuildReleasedTrees();
	GEditor->EndTransaction();

	UDreamWidget* Rebuilt = Scoped.PreviewRoot();
	if (!TestNotNull(TEXT("the preview was built again"), Rebuilt))
	{
		return false;
	}
	UWorld* PreviewWorld = Rebuilt->GetWorld();
	const TWeakObjectPtr<UDreamWidget> WeakRebuilt(Rebuilt);
	const UDreamWidget* RebuiltParent = Rebuilt->GetParent();

	// The step holds the edit, and nothing the rebuild touched.
	const int32 QueueLength = GEditor->Trans->GetQueueLength();
	const FTransaction* Step = QueueLength > 0 ? GEditor->Trans->GetTransaction(QueueLength - 1) : nullptr;
	if (TestNotNull(TEXT("the edit made an undo step"), Step))
	{
		TArray<UObject*> Recorded;
		Step->GetTransactionObjects(Recorded);
		TestTrue(TEXT("the step holds the edited object"), Recorded.Contains(Edited.Get()));
		TestEqual(TEXT("the probe's tree was built again inside the edit"), Probe.Rebuilds, 1);
		TestFalse(TEXT("and what that rebuild Modify()'d is not in the step"), Recorded.Contains(ProbeTarget.Get()));
		for (const UObject* Object : Recorded)
		{
			// Inside the world, or registered with it from outside (the preview's widgets need not be outered to it).
			const bool bOfPreview = Object != nullptr && PreviewWorld != nullptr
				&& (Object->IsIn(PreviewWorld) || Object->GetWorld() == PreviewWorld);
			TestFalse(*FString::Printf(TEXT("the step holds nothing of the preview world, but holds %s"), *GetPathNameSafe(Object)), bOfPreview);
		}
	}

	GEditor->UndoTransaction();
	TestTrue(TEXT("the undo took the edit back"), Edited->GetGradient() == EditedBefore);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	if (!TestTrue(TEXT("the rebuilt preview outlives the undo and a collection"), WeakRebuilt.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("it is still the designer's preview"), Scoped.PreviewRoot() == WeakRebuilt.Get());
	TestTrue(TEXT("still registered"), WeakRebuilt->HasRegistered());
	TestTrue(TEXT("and still where it was put"), WeakRebuilt->GetParent() == RebuiltParent);
	return true;
}

#endif
