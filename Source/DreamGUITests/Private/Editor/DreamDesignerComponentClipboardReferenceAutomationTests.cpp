// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/SDreamWidgetComponentEditor.h"
#include "DreamWidgetBlueprint.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

namespace DreamDesignerComponentClipboardReferenceTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		FScopedDesigner()
		{
			const FString Name = TEXT("BP_ComponentClipboard_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Package = CreatePackage(*(TEXT("/Temp/DreamGUITests/") + Name));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamUserWidget::StaticClass(),
				Package, FName(*Name), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr) return;
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->SetDisplayName(TEXT("Root"));
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			for (const TCHAR* Id : { TEXT("Source"), TEXT("Target"), TEXT("Receiver") })
			{
				UDreamWidget* Widget = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), Tree->RootWidget, -1, Id);
				if (Widget == nullptr) return;
				Widget->SetIsFocusable(true);
			}
			UUIButton* Source = Template(TEXT("Source"))->AddComponent<UUIButton>();
			UUIButton* Receiver = Template(TEXT("Receiver"))->AddComponent<UUIButton>();
			UDreamVisual* Visual = Template(TEXT("Receiver"))->CreateNewVisual<UDreamVisualEmpty>();
			if (Source == nullptr || Receiver == nullptr || Visual == nullptr) return;
			Source->SetNavigationRight(EUISelectableNavigationMode::Explicit);
			Source->SetNavigationRightExplicit(Receiver);
			Source->SetTransitionTarget(Visual);
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			Editors->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false));
			if (Designer != nullptr) Designer->GetPreviewHost()->RebuildPreviewIfInvalidated();
		}

		~FScopedDesigner()
		{
			// The component clipboard owns its own objects; release this test's copy while the editor
			// and object system are still up, exactly as the editor module does on shutdown.
			DreamUIWidgetComponentClipboard().Reset();
			if (Designer != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr) Package->RemoveFromRoot();
		}

		UDreamWidget* Template(const TCHAR* Id) const
		{
			return Blueprint != nullptr && Blueprint->WidgetTree != nullptr
				? Blueprint->WidgetTree->FindWidgetByVariableName(FName(Id)) : nullptr;
		}

		UDreamWidget* Preview(const TCHAR* Id) const
		{
			return Designer != nullptr ? Designer->GetPreviewHost()->FindPreviewForTemplate(Template(Id)) : nullptr;
		}
	};

	bool PressCommand(SDreamWidgetComponentEditor& Panel, FKey Key)
	{
		const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
		return Panel.OnKeyDown(FGeometry(), FKeyEvent(Key, Control, 0, false, 0, 0)).IsEventHandled();
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamDesignerComponentClipboardPreservesReferencesTest,
	"DreamGUI.Designer.ComponentClipboard.AuthoredReferencesSurviveCopyOrCutAndPreviewRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamDesignerComponentClipboardPreservesReferencesTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Copy and paste"));
	OutTestCommands.Add(TEXT("Copy"));
	OutBeautifiedNames.Add(TEXT("Cut and paste"));
	OutTestCommands.Add(TEXT("Cut"));
}

bool FDreamDesignerComponentClipboardPreservesReferencesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerComponentClipboardReferenceTestLocal;
	if (!TestTrue(TEXT("the real editor and Slate are available"), GEditor != nullptr && FSlateApplication::IsInitialized())) return false;
	FScopedDesigner Scoped;
	if (!TestNotNull(TEXT("the real designer opened"), Scoped.Designer)
		|| !TestNotNull(TEXT("the source has a live preview"), Scoped.Preview(TEXT("Source")))
		|| !TestNotNull(TEXT("the receiver has a live preview"), Scoped.Preview(TEXT("Receiver")))) return false;
	UDreamWidget* Context = Scoped.Preview(TEXT("Source"));
	UUIButton* OriginalPreviewButton = Context->GetComponent<UUIButton>();
	UUIButton* OriginalPreviewReceiver = Scoped.Preview(TEXT("Receiver"))->GetComponent<UUIButton>();
	if (!TestNotNull(TEXT("the native source button was instanced"), OriginalPreviewButton)
		|| !TestNotNull(TEXT("the native receiver button was instanced"), OriginalPreviewReceiver)) return false;
	TestEqual(TEXT("the initial preview's authored navigation resolves within that preview"),
		OriginalPreviewButton->GetNavigationRightExplicit(), static_cast<UUISelectable*>(OriginalPreviewReceiver));
	TestEqual(TEXT("the initial explicit navigation actually reaches the receiver"),
		OriginalPreviewButton->FindSelectableOnRight(), static_cast<UUISelectable*>(OriginalPreviewReceiver));
	TWeakObjectPtr<UDreamWidget> OldReceiver = Scoped.Preview(TEXT("Receiver"));

	{
		TSharedRef<SDreamWidgetComponentEditor> Panel = SNew(SDreamWidgetComponentEditor)
			.GetWidgetContext_Lambda([&Context] { return Context; })
			.CanEdit_Lambda([] { return true; });
		Panel->SelectComponent(OriginalPreviewButton);
		TestTrue(TEXT("the real component panel handles the copy/cut shortcut"),
			PressCommand(*Panel, Parameters == TEXT("Cut") ? EKeys::X : EKeys::C));
		if (!TestTrue(TEXT("the command populated the plugin's component clipboard"), DreamUIWidgetComponentClipboard().IsValid())) return false;
		if (Parameters == TEXT("Cut"))
		{
			TestEqual(TEXT("cut removed the authored source component"), Scoped.Template(TEXT("Source"))->GetAllComponents().Num(), 0);
		}
		Context = Scoped.Preview(TEXT("Target"));
		Panel->RefreshComponents();
		TestTrue(TEXT("the real component panel handles Paste"), PressCommand(*Panel, EKeys::V));
		Context = Scoped.Preview(TEXT("Target"));
		Panel->RefreshComponents();
	}
	TestFalse(TEXT("the operation destroyed the outgoing preview receiver"), OldReceiver.IsValid());
	UUIButton* Authored = Scoped.Template(TEXT("Target"))->GetComponent<UUIButton>();
	UUIButton* AuthoredReceiver = Scoped.Template(TEXT("Receiver"))->GetComponent<UUIButton>();
	if (!TestNotNull(TEXT("paste created a saved-template component"), Authored)) return false;
	TestEqual(TEXT("the authored navigation names the asset's own receiver"), Authored->GetNavigationRightExplicit(), static_cast<UUISelectable*>(AuthoredReceiver));
	TestEqual(TEXT("the authored transition names the asset's own visual"), Authored->GetTransitionTarget(), Scoped.Template(TEXT("Receiver"))->GetVisual());
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (Pass != 0) Scoped.Designer->GetPreviewHost()->RebuildPreview();
		UUIButton* Preview = Scoped.Preview(TEXT("Target"))->GetComponent<UUIButton>();
		UUIButton* PreviewReceiver = Scoped.Preview(TEXT("Receiver"))->GetComponent<UUIButton>();
		if (!TestNotNull(TEXT("each rebuilt preview retains the pasted component"), Preview)) return false;
		TestEqual(TEXT("each rebuilt navigation names the new preview receiver"), Preview->GetNavigationRightExplicit(), static_cast<UUISelectable*>(PreviewReceiver));
		TestEqual(TEXT("each rebuilt transition names the new preview visual"), Preview->GetTransitionTarget(), Scoped.Preview(TEXT("Receiver"))->GetVisual());
		TestEqual(TEXT("actual explicit navigation follows the pasted route after rebuilding"), Preview->FindSelectableOnRight(), static_cast<UUISelectable*>(PreviewReceiver));
	}
	return true;
}

#endif
