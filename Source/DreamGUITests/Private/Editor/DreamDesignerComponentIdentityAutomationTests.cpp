// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/SDreamWidgetComponentEditor.h"
#include "DreamWidgetBlueprint.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PlayTween/DreamUIPlayTweenComponent.h"
#include "PlayTween/DreamUIPlayTween.h"
#include "PlayTween/DreamUIPlayTween_Params.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace DreamDesignerComponentIdentityTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		explicit FScopedDesigner(bool bSameClass = false)
		{
			const FString Name = TEXT("BP_ComponentIdentity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Package = CreatePackage(*(TEXT("/Temp/DreamGUITests/") + Name));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamUserWidget::StaticClass(),
				Package, FName(*Name), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)return;
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			UDreamWidget* Subject = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), Tree->RootWidget, -1, TEXT("Subject"));
			if (Subject == nullptr)return;
			if (bSameClass)
			{
				for (float Duration : { 1.0f, 2.0f })
				{
					UDreamUIPlayTweenComponent* Component = Subject->AddComponent<UDreamUIPlayTweenComponent>();
					UDreamUIPlayTween_Float* Tween = NewObject<UDreamUIPlayTween_Float>(Component);
					Tween->SetDuration(Duration);
					FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Component->GetClass(), TEXT("PlayTween"));
					if (Property == nullptr)return;
					Property->SetObjectPropertyValue_InContainer(Component, Tween);
				}
			}
			else
			{
				Subject->AddComponent<UUIButton>();
				Subject->AddComponent<UDreamUIPlayTweenComponent>();
			}
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			Editors->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false));
			if (Designer != nullptr)Designer->GetPreviewHost()->RebuildPreviewIfInvalidated();
		}

		~FScopedDesigner()
		{
			if (Designer != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)Package->RemoveFromRoot();
		}

		UDreamWidget* Template() const
		{
			return Blueprint != nullptr && Blueprint->WidgetTree != nullptr
				? Blueprint->WidgetTree->FindWidgetByVariableName(TEXT("Subject")) : nullptr;
		}

		UDreamWidget* Preview() const
		{
			return Designer != nullptr ? Designer->GetPreviewHost()->FindPreviewForTemplate(Template()) : nullptr;
		}
	};

	bool PressCommand(SDreamWidgetComponentEditor& Panel, FKey Key, bool ControlDown)
	{
		const FModifierKeysState Modifiers(false, false, ControlDown, false, false, false, false, false, false);
		return Panel.OnKeyDown(FGeometry(), FKeyEvent(Key, Modifiers, 0, false, 0, 0)).IsEventHandled();
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamDesignerComponentIdentityTest,
	"DreamGUI.Designer.Components.APreviewOnlyComponentCannotEditADifferentAuthoredClassAtTheSameIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamDesignerComponentIdentityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Delete cannot remove the authored button"));
	OutTestCommands.Add(TEXT("Delete"));
	OutBeautifiedNames.Add(TEXT("Duplicate cannot copy the authored button"));
	OutTestCommands.Add(TEXT("Duplicate"));
	OutBeautifiedNames.Add(TEXT("Reorder cannot move the authored button"));
	OutTestCommands.Add(TEXT("Move"));
}

bool FDreamDesignerComponentIdentityTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerComponentIdentityTestLocal;
	if (!TestTrue(TEXT("the real editor and Slate are available"),
		GEditor != nullptr && GEditor->Trans != nullptr && FSlateApplication::IsInitialized()))return false;
	FScopedDesigner Scoped;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)
		|| !TestNotNull(TEXT("the subject has an authored widget"), Scoped.Template())
		|| !TestNotNull(TEXT("the subject has a live preview"), Scoped.Preview()))return false;
	UDreamWidget* Context = Scoped.Preview();
	const TArray<UDreamUIBehaviour*> AuthoredBefore = Scoped.Template()->GetAllComponents();
	if (!TestEqual(TEXT("the asset has the two authored behaviours"), AuthoredBefore.Num(), 2))return false;

	// User initialization code can add and order behaviours through these public Blueprint-callable
	// operations. The preview then has a legal component that has never existed in the asset.
	UDreamUIPlayTweenComponent* RuntimeOnly = Context->AddComponent<UDreamUIPlayTweenComponent>();
	if (!TestNotNull(TEXT("the public API adds the preview-only tween"), RuntimeOnly))return false;
	Context->MoveComponentToIndex(RuntimeOnly, 0);
	TestEqual(TEXT("the selected preview slot contains the tween"), Context->GetAllComponents()[0], static_cast<UDreamUIBehaviour*>(RuntimeOnly));
	TestTrue(TEXT("the same authored slot contains a different class"), AuthoredBefore[0]->IsA<UUIButton>());
	const int32 UndoEntriesBefore = GEditor->Trans->GetQueueLength();
	TWeakObjectPtr<UDreamWidget> PreviewBefore = Context;
	TWeakObjectPtr<UDreamUIBehaviour> RuntimeBefore = RuntimeOnly;
	{
		TSharedRef<SDreamWidgetComponentEditor> Panel = SNew(SDreamWidgetComponentEditor)
			.GetWidgetContext_Lambda([&Context] { return Context; })
			.CanEdit_Lambda([] { return true; });
		Panel->SelectComponent(RuntimeOnly);
		if (Parameters == TEXT("Move"))
		{
			FScopedTransaction Transaction(FText::FromString(TEXT("Reorder a preview-only component")));
			const bool Moved = Scoped.Designer->DesignerMoveComponent(Context, RuntimeOnly, 1);
			TestFalse(TEXT("the designer refuses to move another authored component instead"), Moved);
			if (!Moved)Transaction.Cancel();
		}
		else
		{
			TestTrue(TEXT("the real panel handles the selected component command"), PressCommand(*Panel,
				Parameters == TEXT("Delete") ? EKeys::Delete : EKeys::D, Parameters == TEXT("Duplicate")));
		}
		Context = Scoped.Preview();
		Panel->RefreshComponents();
	}
	TestTrue(TEXT("the operation preserves every authored component in its original order"), Scoped.Template()->GetAllComponents() == AuthoredBefore);
	TestTrue(TEXT("a refused operation keeps the live preview"), PreviewBefore.IsValid() && Scoped.Preview() == PreviewBefore.Get());
	TestTrue(TEXT("a refused operation keeps the selected runtime component"), RuntimeBefore.IsValid());
	TestEqual(TEXT("a refused operation creates no undo entry"), GEditor->Trans->GetQueueLength(), UndoEntriesBefore);

	// A normal authored selection still supports the same command after a clean projection.
	Scoped.Designer->GetPreviewHost()->RebuildPreview();
	Context = Scoped.Preview();
	if (!TestNotNull(TEXT("the subject can be projected again"), Context))return false;
	if (Scoped.Template()->GetAllComponents() != AuthoredBefore)return true;
	{
		TSharedRef<SDreamWidgetComponentEditor> Panel = SNew(SDreamWidgetComponentEditor)
			.GetWidgetContext_Lambda([&Context] { return Context; })
			.CanEdit_Lambda([] { return true; });
		UDreamUIBehaviour* Selected = Parameters == TEXT("Duplicate")
			? static_cast<UDreamUIBehaviour*>(Context->GetComponent<UDreamUIPlayTweenComponent>())
			: static_cast<UDreamUIBehaviour*>(Context->GetComponent<UUIButton>());
		Panel->SelectComponent(Selected);
		if (Parameters == TEXT("Move"))
		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("Reorder an authored component")));
			TestTrue(TEXT("the designer still accepts an authored selection"), Scoped.Designer->DesignerMoveComponent(Context, Selected, 1));
		}
		else
		{
			TestTrue(TEXT("the real panel still handles the authored command"), PressCommand(*Panel,
				Parameters == TEXT("Delete") ? EKeys::Delete : EKeys::D, Parameters == TEXT("Duplicate")));
		}
		Context = Scoped.Preview();
		Panel->RefreshComponents();
	}
	const TArray<UDreamUIBehaviour*>& AuthoredAfter = Scoped.Template()->GetAllComponents();
	if (Parameters == TEXT("Delete"))
	{
		TestEqual(TEXT("the intended authored button was removed"), AuthoredAfter.Num(), 1);
		TestFalse(TEXT("the removed authored button is absent"), AuthoredAfter.Contains(AuthoredBefore[0]));
	}
	else if (Parameters == TEXT("Duplicate"))
	{
		TestEqual(TEXT("the authored tween was duplicated"), AuthoredAfter.Num(), 3);
		TestEqual(TEXT("the button was not duplicated"), Scoped.Template()->GetComponents(UUIButton::StaticClass()).Num(), 1);
		TestEqual(TEXT("there are two authored tweens"), Scoped.Template()->GetComponents(UDreamUIPlayTweenComponent::StaticClass()).Num(), 2);
	}
	else
	{
		TestEqual(TEXT("the intended authored button moved"), AuthoredAfter.IndexOfByKey(AuthoredBefore[0]), 1);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamDesignerReorderedSameClassComponentIdentityTest,
	"DreamGUI.Designer.Components.AReorderedSameClassSelectionCannotEditTheOtherAuthoredInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamDesignerReorderedSameClassComponentIdentityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Delete follows the selected tween or refuses"));
	OutTestCommands.Add(TEXT("Delete"));
	OutBeautifiedNames.Add(TEXT("Duplicate follows the selected tween or refuses"));
	OutTestCommands.Add(TEXT("Duplicate"));
}

bool FDreamDesignerReorderedSameClassComponentIdentityTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerComponentIdentityTestLocal;
	if (!TestTrue(TEXT("the real editor and Slate are available"),
		GEditor != nullptr && GEditor->Trans != nullptr && FSlateApplication::IsInitialized()))return false;
	FScopedDesigner Scoped(true);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)
		|| !TestNotNull(TEXT("the subject has a live preview"), Scoped.Preview()))return false;
	UDreamWidget* Context = Scoped.Preview();
	const TArray<UDreamUIBehaviour*> AuthoredBefore = Scoped.Template()->GetAllComponents();
	if (!TestEqual(TEXT("two independently configured tweens were authored"), AuthoredBefore.Num(), 2)
		|| !TestEqual(TEXT("the preview has both tweens"), Context->GetAllComponents().Num(), 2))return false;
	UDreamUIPlayTweenComponent* Selected = Cast<UDreamUIPlayTweenComponent>(Context->GetAllComponents()[1]);
	if (!TestNotNull(TEXT("the selected second component is a tween"), Selected)
		|| !TestNotNull(TEXT("the selected tween has its own inline settings"), Selected->GetPlayTween()))return false;
	TestEqual(TEXT("the selected tween has the second authored duration"), Selected->GetPlayTween()->GetDuration(), 2.0f);
	AddInfo(FString::Printf(TEXT("Selected preview %s; archetype %s; authored counterpart %s"),
		*Selected->GetPathName(), *GetPathNameSafe(Selected->GetArchetype()), *AuthoredBefore[1]->GetPathName()));
	Context->MoveComponentToIndex(Selected, 0);
	TestEqual(TEXT("the same component is now the preview's first row"), Context->GetAllComponents()[0], static_cast<UDreamUIBehaviour*>(Selected));
	{
		TSharedRef<SDreamWidgetComponentEditor> Panel = SNew(SDreamWidgetComponentEditor)
			.GetWidgetContext_Lambda([&Context] { return Context; })
			.CanEdit_Lambda([] { return true; });
		Panel->SelectComponent(Selected);
		TestTrue(TEXT("the real panel handles the selected tween command"), PressCommand(*Panel,
			Parameters == TEXT("Delete") ? EKeys::Delete : EKeys::D, Parameters == TEXT("Duplicate")));
		Context = Scoped.Preview();
		Panel->RefreshComponents();
	}
	const TArray<UDreamUIBehaviour*>& After = Scoped.Template()->GetAllComponents();
	TestTrue(TEXT("the unselected first authored tween is always preserved"), After.Contains(AuthoredBefore[0]));
	if (After == AuthoredBefore)
	{
		// Refusing an unresolvable selection is safe too; never substitute another instance.
		return true;
	}
	if (Parameters == TEXT("Delete"))
	{
		TestEqual(TEXT("only the selected authored tween was removed"), After.Num(), 1);
		TestFalse(TEXT("the selected second authored tween is the one removed"), After.Contains(AuthoredBefore[1]));
	}
	else
	{
		TestEqual(TEXT("exactly one tween was duplicated"), After.Num(), 3);
		TestTrue(TEXT("the selected authored tween is preserved by duplication"), After.Contains(AuthoredBefore[1]));
		if (After.Num() == 3)
		{
			UDreamUIPlayTweenComponent* Added = Cast<UDreamUIPlayTweenComponent>(After.Last());
			if (!TestNotNull(TEXT("the new component is a tween"), Added)
				|| !TestNotNull(TEXT("the duplicate has inline settings"), Added->GetPlayTween()))return false;
			TestEqual(TEXT("the duplicate uses the selected tween's settings"), Added->GetPlayTween()->GetDuration(), 2.0f);
		}
	}
	return true;
}
#endif
