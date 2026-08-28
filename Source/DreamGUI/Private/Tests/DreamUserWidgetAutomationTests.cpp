// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUserWidgetTestTypes.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "UObject/Package.h"

/*
 * A hierarchy instantiated from a CLASS rather than read back from a blob.
 *
 * InitializeWidgetStatic takes its archetype as a parameter rather than reading it off the class, and
 * that shape is deliberate: it means this whole half -- instancing, back-pointer rebuild, by-name
 * binding, attachment -- is provable before a Blueprint compiler exists to produce a real generated
 * class. What the compiler adds later is where the tree and the property names come FROM, not what
 * happens to them.
 *
 * Binding matches a widget's sanitized display name against the class's object properties, so the
 * fixture's property names are its contract; see DreamUserWidgetTestTypes.h.
 */

namespace DreamUserWidgetTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/**
	 * Root / Header / Mismatched, with Caption under Header.
	 *
	 * Caption sits a level down so binding has to reach past the first level to find it, and
	 * Mismatched is a plain widget whose name collides with a property that cannot hold one.
	 */
	UDreamWidgetTree* BuildTemplate(UObject* InOuter)
	{
		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(InOuter);
		UDreamWidget* Root = Tree->ConstructWidget<UDreamWidget>();
		Root->SetDisplayName(TEXT("Root"));
		Tree->RootWidget = Root;

		UDreamWidget* Header = Tree->ConstructWidget<UDreamWidget>();
		Header->SetDisplayName(TEXT("Header"));
		Header->TrySetParent(Root, false);

		UDreamWidget* Caption = Tree->ConstructWidget<UDreamWidget>();
		Caption->SetDisplayName(TEXT("Caption"));
		Caption->TrySetParent(Header, false);

		UDreamWidget* Mismatched = Tree->ConstructWidget<UDreamWidget>();
		Mismatched->SetDisplayName(TEXT("Mismatched"));
		Mismatched->TrySetParent(Root, false);

		return Tree;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetInitializeBuildsAndBindsTest,
	"DreamGUI.UserWidget.InitializeBuildsTheTreeAndBindsByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetInitializeBuildsAndBindsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(GetTransientPackage()));
	TStrongObjectPtr<UDreamUserWidgetBindFixture> UserWidget(
		NewObject<UDreamUserWidgetBindFixture>(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass()));

	AddExpectedError(TEXT("matches property 'Mismatched'"), EAutomationExpectedErrorFlags::Contains, 1);
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(UserWidget.Get(), UDreamUserWidgetBindFixture::StaticClass(), Template.Get());

	// The contents exist and are the widget's own, not the template's.
	if (!TestNotNull(TEXT("the user widget got a tree of its own"), UserWidget->GetWidgetTree()))
	{
		return false;
	}
	TestNotEqual(TEXT("and it is not the template"), (const UDreamWidgetTree*)UserWidget->GetWidgetTree(), (const UDreamWidgetTree*)Template.Get());
	TestEqual(TEXT("the whole template came across"), UserWidget->GetWidgetTree()->CountWidgets(), 4);

	// The contents hang beneath the user widget, which is itself a widget.
	UDreamWidget* ContentRoot = UserWidget->GetContentRoot();
	if (TestNotNull(TEXT("the contents have a root"), ContentRoot))
	{
		TestEqual(TEXT("the content root is parented to the user widget"), ContentRoot->GetParent(), (UDreamWidget*)UserWidget.Get());
		TestTrue(TEXT("and the user widget lists it as a child"), UserWidget->GetChildren().Contains(ContentRoot));
	}

	// Binding, one property per branch.
	TestNotNull(TEXT("a same-named widget binds"), UserWidget->Header.Get());
	if (UserWidget->Header != nullptr)
	{
		TestEqual(TEXT("and it is the instanced one, not the template's"), UserWidget->Header->GetDisplayName(), FString(TEXT("Header")));
		TestTrue(TEXT("bound widgets belong to the instance's tree"), UserWidget->Header->IsIn(UserWidget->GetWidgetTree()));
	}
	TestNotNull(TEXT("binding reaches past the first level"), UserWidget->Caption.Get());
	TestNull(TEXT("a property with no matching widget stays null"), UserWidget->Absent.Get());
	TestNull(TEXT("a name match with the wrong type does not bind"), UserWidget->Mismatched.Get());

	// The template must survive being instanced -- it gets instanced once per instance.
	TestEqual(TEXT("the template is untouched"), Template->CountWidgets(), 4);
	TestNull(TEXT("and it did not get attached to anything"), Template->RootWidget->GetParent());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetInitializeIsIdempotentTest,
	"DreamGUI.UserWidget.InitializeRunsOnceAndSkipsTemplates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetInitializeIsIdempotentTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(GetTransientPackage()));
	TStrongObjectPtr<UDreamUserWidgetBindFixture> UserWidget(
		NewObject<UDreamUserWidgetBindFixture>(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass()));

	AddExpectedError(TEXT("matches property 'Mismatched'"), EAutomationExpectedErrorFlags::Contains, 1);
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(UserWidget.Get(), UDreamUserWidgetBindFixture::StaticClass(), Template.Get());
	const UDreamWidgetTree* FirstTree = UserWidget->GetWidgetTree();
	const int32 ChildrenAfterFirst = UserWidget->GetChildren().Num();

	// Initialize guards on its own flag; InitializeWidgetStatic does not, and calling it twice would
	// build a second hierarchy and leave the first orphaned under the same widget. Going through
	// Initialize is what callers do, so that is what has to be safe.
	UserWidget->Initialize();
	UserWidget->Initialize();
	TestEqual(TEXT("re-initializing does not rebuild the tree"), (const UDreamWidgetTree*)UserWidget->GetWidgetTree(), FirstTree);
	TestEqual(TEXT("nor add a second set of contents"), UserWidget->GetChildren().Num(), ChildrenAfterFirst);

	// A CDO is the template, not an instance. Building into it would give the class contents that
	// every later instance would then copy.
	UDreamUserWidgetBindFixture* ClassDefault = GetMutableDefault<UDreamUserWidgetBindFixture>();
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(ClassDefault, UDreamUserWidgetBindFixture::StaticClass(), Template.Get());
	TestNull(TEXT("a class default object is left alone"), ClassDefault->GetWidgetTree());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetInheritsTemplateTest,
	"DreamGUI.UserWidget.ASubclassWithNoTemplateInstancesItsParents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetInheritsTemplateTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// Subclassing a screen to override one function must not produce an empty screen. The lookup walks
	// up for the nearest class that declares a tree, which is the behaviour being pinned here -- with a
	// native subclass, which never has a generated class at all.
	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(GetTransientPackage()));
	TStrongObjectPtr<UDreamUserWidgetBindFixtureSubclass> UserWidget(
		NewObject<UDreamUserWidgetBindFixtureSubclass>(TestWorld.World, UDreamUserWidgetBindFixtureSubclass::StaticClass()));

	AddExpectedError(TEXT("matches property 'Mismatched'"), EAutomationExpectedErrorFlags::Contains, 1);
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(UserWidget.Get(), UDreamUserWidgetBindFixtureSubclass::StaticClass(), Template.Get());

	TestNotNull(TEXT("the subclass built the inherited hierarchy"), UserWidget->GetWidgetTree());
	TestNotNull(TEXT("and inherited properties still bind"), UserWidget->Header.Get());

	// No generated class in the chain, so nothing declares a tree and the lookup must say so rather
	// than invent one.
	TestNull(TEXT("a native class chain declares no template"),
		UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(UDreamUserWidgetBindFixtureSubclass::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetVariableNameIsSharedTest,
	"DreamGUI.UserWidget.TheVariableNameRuleIsOneFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetVariableNameIsSharedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// The compiler declares properties from this rule and the runtime resolves bindings with it. The
	// two agreeing is the entire contract, and a second near-identical copy elsewhere is how a binding
	// reports success and comes back null. These cases are the ones a careless reimplementation gets
	// wrong.
	TStrongObjectPtr<UDreamWidgetTree> Tree(NewObject<UDreamWidgetTree>(TestWorld.World));
	UDreamWidget* Widget = Tree->ConstructWidget<UDreamWidget>();
	Tree->RootWidget = Widget;

	Widget->SetDisplayName(TEXT("Play Button"));
	TestEqual(TEXT("spaces become underscores"), UDreamWidgetTree::MakeWidgetVariableName(Widget), FName(TEXT("Play_Button")));

	Widget->SetDisplayName(TEXT("2ndPanel"));
	TestEqual(TEXT("a leading digit gets a prefix"), UDreamWidgetTree::MakeWidgetVariableName(Widget), FName(TEXT("_2ndPanel")));

	Widget->SetDisplayName(TEXT("确定按钮"));
	TestEqual(TEXT("non-ASCII names survive intact"), UDreamWidgetTree::MakeWidgetVariableName(Widget), FName(TEXT("确定按钮")));

	Widget->SetDisplayName(FString());
	TestEqual(TEXT("an empty name still yields an identifier"), UDreamWidgetTree::MakeWidgetVariableName(Widget), FName(TEXT("Element")));

	Widget->SetDisplayName(TEXT("Header"));
	TestEqual(TEXT("lookup by variable name finds the widget"), Tree->FindWidgetByVariableName(FName(TEXT("Header"))), Widget);
	TestNull(TEXT("and reports nothing for a name no widget carries"), Tree->FindWidgetByVariableName(FName(TEXT("Nope"))));

	return true;
}

#endif
