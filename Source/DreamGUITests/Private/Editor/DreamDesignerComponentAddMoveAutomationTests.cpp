// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/SDreamWidgetComponentEditor.h"
#include "DreamDesignerComponentOrderTestTypes.h"
#include "DreamWidgetBlueprint.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PlayTween/DreamUIPlayTweenComponent.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

namespace DreamDesignerComponentAddMoveTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		FScopedDesigner()
		{
			const FString Name = TEXT("BP_ComponentAddMove_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Package = CreatePackage(*(TEXT("/Temp/DreamGUITests/") + Name));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamDesignerComponentOrderTestWidget::StaticClass(),
				Package, FName(*Name), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)return;
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			UDreamWidget* Subject = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), Tree->RootWidget, -1, TEXT("Subject"));
			if (Subject == nullptr)return;
			for (int32 Index = 0; Index < 3; ++Index)Subject->AddComponent<UDreamUIPlayTweenComponent>();
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
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamDesignerComponentAddMoveTest,
	"DreamGUI.Designer.Components.InitializationReorderingPreservesAddResultsAndMoveDestinations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamDesignerComponentAddMoveTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Add returns the newly authored component"));
	OutTestCommands.Add(TEXT("Add"));
	OutBeautifiedNames.Add(TEXT("Duplicate returns the newly authored component"));
	OutTestCommands.Add(TEXT("Duplicate"));
	OutBeautifiedNames.Add(TEXT("Move before follows the authored target"));
	OutTestCommands.Add(TEXT("Before"));
	OutBeautifiedNames.Add(TEXT("Move after follows the authored target"));
	OutTestCommands.Add(TEXT("After"));
	OutBeautifiedNames.Add(TEXT("A runtime-only destination is refused"));
	OutTestCommands.Add(TEXT("RuntimeTarget"));
}

bool FDreamDesignerComponentAddMoveTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerComponentAddMoveTestLocal;
	if (!TestTrue(TEXT("the real editor and Slate are available"),
		GEditor != nullptr && GEditor->Trans != nullptr && FSlateApplication::IsInitialized()))return false;
	FScopedDesigner Scoped;
	if (!TestNotNull(TEXT("the designer opened the compiled blueprint"), Scoped.Designer)
		|| !TestNotNull(TEXT("the template subject exists"), Scoped.Template())
		|| !TestNotNull(TEXT("the preview subject exists"), Scoped.Preview()))return false;
	TSharedPtr<FDreamWidgetPreviewHost> Host = Scoped.Designer->GetPreviewHost();
	UDreamWidget* Context = Scoped.Preview();
	const TArray<UDreamUIBehaviour*> Before = Scoped.Template()->GetAllComponents();
	if (!TestEqual(TEXT("three components were authored"), Before.Num(), 3)
		|| !TestEqual(TEXT("initialization preserved all three components"), Context->GetAllComponents().Num(), 3))return false;
	TestEqual(TEXT("NativeOnInitialized moved C to the first preview row"), Host->FindTemplateComponentForPreview(Context->GetAllComponents()[0]), Before[2]);
	TestEqual(TEXT("the second preview row contains A"), Host->FindTemplateComponentForPreview(Context->GetAllComponents()[1]), Before[0]);
	TestEqual(TEXT("the third preview row contains B"), Host->FindTemplateComponentForPreview(Context->GetAllComponents()[2]), Before[1]);

	if (Parameters == TEXT("Add") || Parameters == TEXT("Duplicate"))
	{
		UDreamUIBehaviour* Added = nullptr;
		UDreamUIBehaviour* Result = nullptr;
		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("Add a component to a reordered preview")));
			Result = Scoped.Designer->DesignerAddComponentBy(Context, [&](UDreamWidget* Template)
			{
				Added = Parameters == TEXT("Add") ? static_cast<UDreamUIBehaviour*>(Template->AddComponent<UDreamUIPlayTweenComponent>())
					: DreamUIWidgetComponentClipboard_PasteOnto(Template, Before[1]);
				return Added;
			});
		}
		if (!TestNotNull(TEXT("the operation added an authored component"), Added)
			|| !TestNotNull(TEXT("the operation returned a preview component"), Result))return false;
		TestEqual(TEXT("the authored list gained exactly one component"), Scoped.Template()->GetAllComponents().Num(), 4);
		TestEqual(TEXT("the returned component belongs to the newly rebuilt preview"), Result->GetOuter(), static_cast<UObject*>(Scoped.Preview()));
		TestEqual(TEXT("the returned preview component maps to the newly added authored component"), Host->FindTemplateComponentForPreview(Result), Added);
		TestEqual(TEXT("the returned component follows its initialization reorder to row zero"), Scoped.Preview()->GetAllComponents()[0], Result);
		return true;
	}

	if (Parameters == TEXT("RuntimeTarget"))
	{
		UDreamUIBehaviour* Selected = Context->GetAllComponents()[2];
		UDreamUIBehaviour* Runtime = Context->AddComponent<UDreamUIPlayTweenComponent>();
		if (!TestNotNull(TEXT("a live-only component can be added"), Runtime))return false;
		Context->MoveComponentToIndex(Runtime, 0);
		const int32 UndoBefore = GEditor->Trans->GetQueueLength();
		{
			FScopedTransaction Transaction(FText::FromString(TEXT("Refuse a live-only destination")));
			const bool Moved = Scoped.Designer->DesignerMoveComponent(Context, Selected, 0);
			TestFalse(TEXT("a live-only destination cannot stand in for an authored row"), Moved);
			if (!Moved)Transaction.Cancel();
		}
		TestTrue(TEXT("refusal preserves authored components and their order"), Scoped.Template()->GetAllComponents() == Before);
		TestEqual(TEXT("refusal keeps the live preview"), Scoped.Preview(), Context);
		TestEqual(TEXT("refusal leaves no undo entry"), GEditor->Trans->GetQueueLength(), UndoBefore);
		return true;
	}

	// The public command takes the final preview index. [C,A,B] -> [C,B,A] moves B before A;
	// [C,A,B] -> [A,C,B] moves C after A. Both destinations refer to A, whose authored index is zero.
	const bool bBefore = Parameters == TEXT("Before");
	UDreamUIBehaviour* Selected = Context->GetAllComponents()[bBefore ? 2 : 0];
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Move relative to a reordered target")));
		TestTrue(TEXT("the designer accepts the authored source and target"), Scoped.Designer->DesignerMoveComponent(Context, Selected, 1));
	}
	const TArray<UDreamUIBehaviour*>& After = Scoped.Template()->GetAllComponents();
	if (!TestEqual(TEXT("moving preserves the component count"), After.Num(), 3))return false;
	TestEqual(TEXT("the source reaches its intended authored position"), After[bBefore ? 0 : 1], Before[bBefore ? 1 : 2]);
	TestEqual(TEXT("the intended target remains adjacent on the correct side"), After[bBefore ? 1 : 0], Before[0]);
	TestEqual(TEXT("the unrelated authored component keeps its identity"), After[2], Before[bBefore ? 2 : 1]);
	return true;
}

#endif
