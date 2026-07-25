#include "Misc/AutomationTest.h"

#include "Core/Components/LexWidget.h"

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

#endif
