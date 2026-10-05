// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "DreamUIComponentSupport.h"
#include "Extensions/DreamRetainerBox.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/DreamResponsiveBinding.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISlider.h"
#include "MeshModifier/DreamMeshModifierOutline.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "UMG/DreamUMGWidgetInteraction.h"

/*
 * What the designer's Add Component offers a widget: only the components that work there. It used to list every
 * component class on every widget, and a sprite player on a text or a second canvas sat on the widget doing nothing.
 */
namespace DreamComponentSupportTestLocal
{
	UDreamWidget* MakeWidget(UDreamWidgetTree* InTree, const TCHAR* InName)
	{
		return InTree->ConstructWidget(UDreamWidget::StaticClass(), InName);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamComponentSupportTest,
	"DreamGUI.Editor.AddComponent.OnlyWhatTheWidgetSupportsIsOffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamComponentSupportTest::RunTest(const FString& Parameters)
{
	using namespace DreamComponentSupportTestLocal;
	const FDreamUIComponentSupport& Support = FDreamUIComponentSupport::Get();
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());

	// A visual decides what reads it.
	UDreamWidget* Label = MakeWidget(Tree, TEXT("Label"));
	Label->CreateNewVisual<UDreamText>();
	FText Reason;
	TestFalse(TEXT("a sprite player is not offered on a text"),
		Support.IsSupported(UUISpriteSequencePlayer::StaticClass(), Label, &Reason));
	TestFalse(TEXT("...and says what is missing"), Reason.IsEmpty());
	TestTrue(TEXT("a mesh modifier is, a text builds a mesh"), Support.IsSupported(UDreamMeshModifierOutline::StaticClass(), Label));
	TestTrue(TEXT("and the text animation, which needs a text"), Support.IsSupported(UDreamMeshModifierTextAnimation::StaticClass(), Label));
	TestFalse(TEXT("a UMG interaction is not, it needs a UMG widget visual"),
		Support.IsSupported(UDreamUMGWidgetInteraction::StaticClass(), Label));
	TestTrue(TEXT("a component that needs nothing is"), Support.IsSupported(UDreamDataBinding::StaticClass(), Label));

	UDreamWidget* Empty = MakeWidget(Tree, TEXT("Empty"));
	TestFalse(TEXT("a mesh modifier is not offered on a widget with no visual"),
		Support.IsSupported(UDreamMeshModifierOutline::StaticClass(), Empty));

	// One per widget, and what needs one first.
	TestFalse(TEXT("a retainer needs the widget's canvas"), Support.IsSupported(UDreamRetainerBox::StaticClass(), Empty));
	TestTrue(TEXT("a first canvas is offered"), Support.IsSupported(UDreamCanvas::StaticClass(), Empty));
	Empty->AddComponent(UDreamCanvas::StaticClass());
	TestFalse(TEXT("a second is not: nothing reads it"), Support.IsSupported(UDreamCanvas::StaticClass(), Empty));
	TestTrue(TEXT("and with the canvas there, the retainer is"), Support.IsSupported(UDreamRetainerBox::StaticClass(), Empty));

	// A family: a widget is one kind of selectable.
	UDreamWidget* Control = MakeWidget(Tree, TEXT("Control"));
	TestTrue(TEXT("a button is offered"), Support.IsSupported(UUIButton::StaticClass(), Control));
	Control->AddComponent(UUIButton::StaticClass());
	TestFalse(TEXT("then a slider is not, on the same widget"), Support.IsSupported(UUISlider::StaticClass(), Control));

	// A content widget holds one child.
	UDreamWidget* Host = MakeWidget(Tree, TEXT("Host"));
	MakeWidget(Tree, TEXT("ChildA"))->SetParentBeforeRegister(Host);
	TestTrue(TEXT("a content widget fits a widget with one child"), Support.IsSupported(UDreamContentWidget::StaticClass(), Host));
	MakeWidget(Tree, TEXT("ChildB"))->SetParentBeforeRegister(Host);
	TestFalse(TEXT("but not one with two"), Support.IsSupported(UDreamContentWidget::StaticClass(), Host));

	// An unloaded Blueprint is asked by ancestry: one derived from the sprite player inherits its rule.
	TestFalse(TEXT("a class known only by what it derives from follows its ancestors' rules"),
		Support.IsSupported([](const UClass* InBase) { return UUISpriteSequencePlayer::StaticClass()->IsChildOf(InBase); }, Label));

	// A project adds its own, and takes them away again.
	FDreamUIComponentSupport& Writable = FDreamUIComponentSupport::Get();
	Writable.OnePerWidget(UDreamDataBinding::StaticClass());
	Label->AddComponent(UDreamDataBinding::StaticClass());
	TestFalse(TEXT("a rule a project registers applies"), Support.IsSupported(UDreamDataBinding::StaticClass(), Label));
	Writable.RemoveRules(UDreamDataBinding::StaticClass());
	TestTrue(TEXT("and stops applying when removed"), Support.IsSupported(UDreamDataBinding::StaticClass(), Label));

	TestTrue(TEXT("no widget, no rules"), Support.IsSupported(UUISpriteSequencePlayer::StaticClass(), nullptr));
	return true;
}

#endif
