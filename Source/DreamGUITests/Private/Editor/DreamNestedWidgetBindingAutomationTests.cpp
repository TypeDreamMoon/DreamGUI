// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/World.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace DreamNestedWidgetBindingTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s_%s"), InName, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			Package->RemoveFromRoot();
		}

		UDreamWidget* AddWidget(const TCHAR* InName, UClass* InClass = UDreamWidget::StaticClass())
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			UDreamWidget* Widget = Tree->ConstructWidget(InClass);
			Widget->SetDisplayName(InName);
			Widget->SetParentBeforeRegister(Tree->RootWidget);
			return Widget;
		}

		bool Compile(FAutomationTestBase& Test)
		{
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			return Test.TestEqual(TEXT("the authored Blueprint compiles"), Results.NumErrors, 0)
				&& Test.TestNotNull(TEXT("the real compiler produced a DreamGUI class"), Cast<UDreamWidgetGeneratedClass>(Blueprint->GeneratedClass));
		}
	};

	UDreamWidget* DirectChild(UDreamWidget* InRoot, const FString& InName)
	{
		if (InRoot != nullptr)
		{
			for (UDreamWidget* Child : InRoot->GetChildren())
			{
				if (Child != nullptr && Child->GetDisplayName() == InName) return Child;
			}
		}
		return nullptr;
	}

	UObject* BoundVariable(UDreamUserWidget* InWidget, FName InName)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InWidget->GetClass(), InName);
		return Property != nullptr ? Property->GetObjectPropertyValue_InContainer(InWidget) : nullptr;
	}

	UDreamWidgetAnimation* AddIntro(FScopedBlueprint& Fixture)
	{
		UDreamWidgetAnimationComponent* Animator = Fixture.Blueprint->GetOrCreateWidgetTree()->RootWidget->AddComponent<UDreamWidgetAnimationComponent>();
		UDreamWidgetAnimation* Animation = Animator != nullptr ? Animator->AddNewAnimation() : nullptr;
		if (Animation != nullptr) Animation->SetDisplayNameString(TEXT("SharedIntro"));
		return Animation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamNestedWidgetVariableBindingTest,
	"DreamGUI.WidgetBlueprint.ANestedPrivateWidgetDoesNotReplaceTheHostsGeneratedVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNestedWidgetVariableBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNestedWidgetBindingTestLocal;
	FScopedBlueprint Inner(TEXT("BP_PrivateWidget"));
	Inner.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("InnerRoot"));
	Inner.AddWidget(TEXT("SharedHeader"));
	if (!Inner.Compile(*this)) return false;
	FScopedBlueprint Outer(TEXT("BP_WidgetBindingHost"));
	Outer.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("OuterRoot"));
	Outer.AddWidget(TEXT("SharedHeader"));
	Outer.AddWidget(TEXT("Nested"), Inner.Blueprint->GeneratedClass);
	if (!Outer.Compile(*this)) return false;

	DreamTests::FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Outer.Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("the parent Blueprint creates a live instance"), Instance)) return false;
	ON_SCOPE_EXIT { if (IsValid(Instance)) Instance->DestroyWidget(); };
	UDreamWidget* OwnHeader = DirectChild(Instance->GetContentRoot(), TEXT("SharedHeader"));
	UDreamUserWidget* Nested = Cast<UDreamUserWidget>(DirectChild(Instance->GetContentRoot(), TEXT("Nested")));
	if (!TestNotNull(TEXT("the host has its authored header"), OwnHeader)
		|| !TestNotNull(TEXT("the host has its nested Blueprint instance"), Nested)) return false;
	UDreamWidget* PrivateHeader = DirectChild(Nested->GetContentRoot(), TEXT("SharedHeader"));
	if (!TestNotNull(TEXT("the nested Blueprint initialized its private header"), PrivateHeader)) return false;
	TestNotEqual(TEXT("the two equal names denote independent widgets"), OwnHeader, PrivateHeader);
	TestEqual(TEXT("the nested widget variable binds to its private header"), BoundVariable(Nested, TEXT("SharedHeader")), static_cast<UObject*>(PrivateHeader));
	TestEqual(TEXT("the host widget variable stays bound to its own header"), BoundVariable(Instance, TEXT("SharedHeader")), static_cast<UObject*>(OwnHeader));
	TestEqual(TEXT("the host reference and its public tree lookup agree"), BoundVariable(Instance, TEXT("SharedHeader")),
		static_cast<UObject*>(Instance->GetWidgetTree()->FindWidgetByVariableName(TEXT("SharedHeader"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamNestedAnimationVariableBindingTest,
	"DreamGUI.WidgetBlueprint.ANestedPrivateAnimationDoesNotReplaceTheHostsGeneratedVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNestedAnimationVariableBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNestedWidgetBindingTestLocal;
	FScopedBlueprint Inner(TEXT("BP_PrivateAnimation"));
	Inner.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("InnerRoot"));
	if (!TestNotNull(TEXT("the nested Blueprint authors its animation"), AddIntro(Inner)) || !Inner.Compile(*this)) return false;
	FScopedBlueprint Outer(TEXT("BP_AnimationBindingHost"));
	Outer.Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("OuterRoot"));
	if (!TestNotNull(TEXT("the host independently authors an animation with the same display name"), AddIntro(Outer))) return false;
	Outer.AddWidget(TEXT("Nested"), Inner.Blueprint->GeneratedClass);
	if (!Outer.Compile(*this)) return false;

	DreamTests::FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Outer.Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("the host Blueprint creates a live instance"), Instance)) return false;
	ON_SCOPE_EXIT { if (IsValid(Instance)) Instance->DestroyWidget(); };
	UDreamUserWidget* Nested = Cast<UDreamUserWidget>(DirectChild(Instance->GetContentRoot(), TEXT("Nested")));
	if (!TestNotNull(TEXT("the nested Blueprint instance exists"), Nested)) return false;
	UMovieSceneSequence* OwnAnimation = Instance->GetAnimationByName(TEXT("SharedIntro"));
	UMovieSceneSequence* PrivateAnimation = Nested->GetAnimationByName(TEXT("SharedIntro"));
	if (!TestNotNull(TEXT("the host exposes its own animation"), OwnAnimation)
		|| !TestNotNull(TEXT("the nested widget exposes its own animation"), PrivateAnimation)) return false;
	TestNotEqual(TEXT("equal animation names denote separate instances"), OwnAnimation, PrivateAnimation);
	TestTrue(TEXT("the host animation belongs to the host's root component"), OwnAnimation->IsIn(Instance->GetContentRoot()));
	TestTrue(TEXT("the private animation belongs to the nested root component"), PrivateAnimation->IsIn(Nested->GetContentRoot()));
	TestEqual(TEXT("the nested animation variable points to its own sequence"), BoundVariable(Nested, TEXT("SharedIntro")), static_cast<UObject*>(PrivateAnimation));
	TestEqual(TEXT("the host animation variable still names its own sequence"), BoundVariable(Instance, TEXT("SharedIntro")), static_cast<UObject*>(OwnAnimation));
	return true;
}

#endif
