#include "Misc/AutomationTest.h"

#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "Interaction/UIScrollView.h"
#include "Tests/LexWidgetLifecycleTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetTreeLifecycleTest,
	"LGUI.Lifecycle.RootOwnedTreeTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetTreeLifecycleTest::RunTest(const FString& Parameters)
{
	ULexWidget* Root = NewObject<ULexWidget>(GetTransientPackage());
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	ULexWidget* Grandchild = NewObject<ULexWidget>(Child);
	Child->SetParent(Root);
	Grandchild->SetParent(Child);

	Root->OnRegister();
	Child->OnRegister();
	Grandchild->OnRegister();
	Root->BeginPlay();
	Child->BeginPlay();
	Grandchild->BeginPlay();

	TestEqual(TEXT("Child resolves the hierarchy root"), Child->GetRootWidgetInHierarchy(), Root);
	TestEqual(TEXT("Grandchild resolves the hierarchy root"), Grandchild->GetRootWidgetInHierarchy(), Root);

	Root->DestroyWidget();
	TestFalse(TEXT("Root is unregistered"), Root->HasRegistered());
	TestFalse(TEXT("Child is unregistered"), Child->HasRegistered());
	TestFalse(TEXT("Grandchild is unregistered"), Grandchild->HasRegistered());
	TestFalse(TEXT("Root ended play"), Root->HasBegunPlay());
	TestFalse(TEXT("Child ended play"), Child->HasBegunPlay());
	TestFalse(TEXT("Grandchild ended play"), Grandchild->HasBegunPlay());

	Root->DestroyWidget();
	TestFalse(TEXT("Repeated teardown remains harmless"), Root->HasRegistered() || Root->HasBegunPlay());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetComponentMutationDuringUnregisterTest,
	"LGUI.Lifecycle.ComponentMutationDuringUnregister",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetComponentMutationDuringUnregisterTest::RunTest(const FString& Parameters)
{
	ULexWidget* Root = NewObject<ULexWidget>(GetTransientPackage());
	ULexWidget* Content = NewObject<ULexWidget>(Root);
	TestTrue(TEXT("Scroll content joins the host widget"), Content->TrySetParent(Root, false));

	UUIScrollView* ScrollView = Root->AddComponent<UUIScrollView>();
	TestNotNull(TEXT("Scroll view component is created"), ScrollView);
	if (!ScrollView)
	{
		return false;
	}
	ScrollView->SetContent(Content);
	TestNotNull(TEXT("Scroll view creates its range helper on the host"),
		Root->GetComponent<UUIScrollViewHelper>());

	Root->OnRegister();
	Root->DestroyWidget();
	TestFalse(TEXT("Host is unregistered after helper removal"), Root->HasRegistered());
	TestNull(TEXT("Range helper is removed during scroll view teardown"),
		Root->GetComponent<UUIScrollViewHelper>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetHierarchyMutationDuringTeardownTest,
	"LGUI.Lifecycle.HierarchyMutationDuringTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetHierarchyMutationDuringTeardownTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	ULexWidget* Root = World ? NewObject<ULexWidget>(World) : nullptr;
	ULexWidget* ExternalParent = World ? NewObject<ULexWidget>(World) : nullptr;
	if (!World || !Root || !ExternalParent)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	ULexWidget* OriginalChild = NewObject<ULexWidget>(Root);
	ULexWidget* LateChild = NewObject<ULexWidget>(ExternalParent);
	TestTrue(TEXT("Original child joins the teardown tree"), OriginalChild->TrySetParent(Root, false));
	TestTrue(TEXT("Late child starts outside the teardown tree"), LateChild->TrySetParent(ExternalParent, false));

	ULexWidgetHierarchyMutationBehaviour* MutationBehaviour =
		Root->AddComponent<ULexWidgetHierarchyMutationBehaviour>();
	TestNotNull(TEXT("Hierarchy mutation behaviour is created"), MutationBehaviour);
	if (!MutationBehaviour)
	{
		World->DestroyWorld(false);
		return false;
	}
	MutationBehaviour->Configure(OriginalChild, LateChild, ExternalParent);

	Root->OnRegister();
	OriginalChild->OnRegister();
	LateChild->OnRegister();
	Root->BeginPlay();
	OriginalChild->BeginPlay();
	LateChild->BeginPlay();

	Root->DestroyWidget();
	TestEqual(TEXT("Behaviour detaches the original child"), OriginalChild->GetParent(), ExternalParent);
	TestEqual(TEXT("Behaviour attaches the late child"), LateChild->GetParent(), Root);
	TestFalse(TEXT("Detached original child is still unregistered"), OriginalChild->HasRegistered());
	TestFalse(TEXT("Detached original child still ends play"), OriginalChild->HasBegunPlay());
	TestFalse(TEXT("Newly attached child is unregistered"), LateChild->HasRegistered());
	TestFalse(TEXT("Newly attached child ends play"), LateChild->HasBegunPlay());
	World->DestroyWorld(false);
	return true;
}

#endif
