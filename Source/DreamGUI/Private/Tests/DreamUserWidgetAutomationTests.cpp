// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUserWidgetTestTypes.h"
#include "Core/DreamUIManager.h"
#include "Core/IDreamUICultureChangedInterface.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetDestroyedStopsBeingPolledTest,
	"DreamGUI.UserWidget.ADestroyedWidgetStopsBeingPolledForItsBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetDestroyedStopsBeingPolledTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("the manager exists for the test world"), Manager))
	{
		return false;
	}

	// A destroyed widget comes off the per-frame polled-binding visit AT TEARDOWN, not at some later
	// moment. The list's only sweep used to be an !IsValid check inside the manager's own tick, which
	// left a destroyed widget on the visit for at least one more frame -- calling its binding source
	// functions on a widget that had already run EndPlay. The fix moved the removal to the one place
	// that KNOWS: UDreamWidget::OnUnregister, through the manager's RemoveWidget.
	//
	// Deliberately not asserted through IsValid. DestroyWidget marks the subtree garbage now, so
	// IsValid would answer correctly here for a reason that has nothing to do with this list -- and it
	// would still answer it a frame too late, because a weak entry is only swept when the tick reaches
	// it. Nothing below ticks: the count is read immediately after teardown, so a count of zero can
	// only mean the unregister path took it off.
	TStrongObjectPtr<UDreamUserWidget> UserWidget(
		NewObject<UDreamUserWidget>(TestWorld.World, UDreamUserWidget::StaticClass()));
	UserWidget->OnRegister();
	Manager->AddPropertyBindingUser(UserWidget.Get());
	if (!TestEqual(TEXT("the widget is on the polled-binding visit"), Manager->GetPropertyBindingUserCount(), 1))
	{
		return false;
	}

	UserWidget->DestroyWidget();
	TestFalse(TEXT("teardown unregistered it"), UserWidget->HasRegistered());
	TestEqual(TEXT("and took it off the visit there and then, with no tick in between"),
		Manager->GetPropertyBindingUserCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetRebuildsFromRecompiledClassTest,
	"DreamGUI.UserWidget.ARecompiledInstanceRebuildsItsContentsInsteadOfStayingHalfDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetRebuildsFromRecompiledClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// Recompiling a Blueprint hands every live instance to a fresh copy of the new class, and the copy
	// arrives with its old contents still attached, WidgetTree null (DuplicateTransient) and
	// bInitialized false. Nothing re-ran Initialize, so the widget had contents, a null content root
	// and no resolved bindings -- and the next Initialize would have hung a SECOND tree on it.
	//
	// Driven through the named-archetype road, exactly as the tests above drive InitializeWidgetStatic:
	// this fixture is a NATIVE class, so there is no generated class to resolve a hierarchy from, and
	// the whole rebuild is provable before a Blueprint compiler exists to produce one.
	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(GetTransientPackage()));
	TStrongObjectPtr<UDreamUserWidgetBindFixture> UserWidget(
		NewObject<UDreamUserWidgetBindFixture>(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass()));
	AddExpectedError(TEXT("matches property 'Mismatched'"), EAutomationExpectedErrorFlags::Contains, 0);
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(UserWidget.Get(), UDreamUserWidgetBindFixture::StaticClass(), Template.Get());
	UserWidget->OnRegister();

	UDreamWidget* OriginalContentRoot = UserWidget->GetContentRoot();
	if (!TestNotNull(TEXT("the instance starts with contents"), OriginalContentRoot))
	{
		return false;
	}
	TestFalse(TEXT("a healthy instance needs no rebuild"), UserWidget->NeedsReinitializeFromClass());

	// Exactly what reinstancing leaves behind: the contents are still there, the tree object is not.
	UserWidget->WidgetTree = nullptr;
	TestNull(TEXT("and with the tree gone it has no content root at all"), UserWidget->GetContentRoot());

	// A class with no hierarchy to rebuild from must be LEFT ALONE. Emptying a widget that still works
	// is strictly worse than the half-dead state this repairs, and this fixture's native class is
	// exactly that case, so it is the one to prove it on.
	AddExpectedError(TEXT("has no hierarchy to rebuild from"), EAutomationExpectedErrorFlags::Contains, 1);
	UserWidget->ReinitializeFromClass();
	TestEqual(TEXT("a rebuild with nothing to rebuild from keeps the contents it had"),
		UserWidget->GetChildrenCount(), 1);
	TestNull(TEXT("and does not invent a tree"), UserWidget->GetWidgetTree());

	UserWidget->ReinitializeFromArchetype(Template.Get());

	if (!TestNotNull(TEXT("the rebuild gave it a tree again"), UserWidget->GetWidgetTree()))
	{
		return false;
	}
	UDreamWidget* RebuiltRoot = UserWidget->GetContentRoot();
	if (!TestNotNull(TEXT("and a content root"), RebuiltRoot))
	{
		return false;
	}
	TestNotEqual(TEXT("built fresh from the class rather than the stale one kept"),
		(const UDreamWidget*)RebuiltRoot, (const UDreamWidget*)OriginalContentRoot);
	TestEqual(TEXT("the whole template came across once, not twice"), UserWidget->GetWidgetTree()->CountWidgets(), 4);
	// The one that would have shown a second hierarchy: the user widget has exactly one child, the
	// rebuilt content root, and not the old one beside it.
	TestEqual(TEXT("and the instance has one set of contents under it"), UserWidget->GetChildrenCount(), 1);
	TestTrue(TEXT("the by-name binding resolved against the new tree"),
		UserWidget->Header != nullptr && UserWidget->Header->IsIn(UserWidget->GetWidgetTree()));

	UserWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetCultureRegistrationTolerantTest,
	"DreamGUI.Manager.RegisteringACultureListenerWithNoObjectIsRefusedRatherThanCrashing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetCultureRegistrationTolerantTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// Both of these are BlueprintCallable statics and the only way in to culture change notification,
	// and both dereferenced the interface's object without checking it. An interface pin left empty
	// in a graph arrives here as null, which made the one API every localised widget has to call a
	// guaranteed crash.
	AddExpectedError(TEXT("Register culture changed event was given no object"), EAutomationExpectedErrorFlags::Contains, 1);
	UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface>());
	UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface>());
	// Reaching this line at all is the assertion; the checks below only keep it from reading as dead.
	TestNotNull(TEXT("the manager is still there afterwards"),
		UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetDuplicateOwnsItsContentsTest,
	"DreamGUI.UserWidget.ADuplicatedWidgetOwnsItsOwnTreeRatherThanTheTemplates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetDuplicateOwnsItsContentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetTestLocal;
	FScopedGameWorld TestWorld;

	// A list cell is made this way: build ONE initialized instance, then duplicate it per row. The
	// copy's WidgetTree is a plain object property that subtree duplication has no counterpart for,
	// so it used to come out pointing at the SOURCE's tree -- which made GetContentRoot,
	// FindSlotWidget and every animation verb answer about the template, and left the copy
	// uninitialized, so none of its own bindings were ever resolved.
	TStrongObjectPtr<UDreamWidgetTree> Template(BuildTemplate(GetTransientPackage()));
	TStrongObjectPtr<UDreamUserWidgetBindFixture> Source(
		NewObject<UDreamUserWidgetBindFixture>(TestWorld.World, UDreamUserWidgetBindFixture::StaticClass()));
	AddExpectedError(TEXT("matches property 'Mismatched'"), EAutomationExpectedErrorFlags::Contains, 1);
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(Source.Get(), UDreamUserWidgetBindFixture::StaticClass(), Template.Get());
	Source->OnRegister();
	if (!TestNotNull(TEXT("the source has contents of its own"), Source->GetContentRoot()))
	{
		return false;
	}

	// The host a duplicated cell is parented to, the way a list's Content is.
	UDreamWidget* Host = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Host->SetDisplayName(TEXT("Host"));
	Host->OnRegister();

	UDreamUserWidget* Copy = Cast<UDreamUserWidget>(
		DuplicateDreamWidgetHierarchy(Source->GetOuter(), Source.Get(), Host));
	if (!TestNotNull(TEXT("the copy is a user widget"), Copy))
	{
		return false;
	}

	TestTrue(TEXT("the copy was initialized"), Copy->IsInitialized());
	if (!TestNotNull(TEXT("the copy has a tree"), Copy->GetWidgetTree()))
	{
		return false;
	}
	TestNotEqual(TEXT("and it is its own, not the source's"),
		(const UDreamWidgetTree*)Copy->GetWidgetTree(), (const UDreamWidgetTree*)Source->GetWidgetTree());

	UDreamWidget* CopyRoot = Copy->GetContentRoot();
	if (TestNotNull(TEXT("the copy's content root exists"), CopyRoot))
	{
		TestNotEqual(TEXT("and is not the source's content root"), CopyRoot, Source->GetContentRoot());
		TestEqual(TEXT("it is parented to the copy"), CopyRoot->GetParent(), (UDreamWidget*)Copy);
		TestTrue(TEXT("and the copy lists it as a child"), Copy->GetChildren().Contains(CopyRoot));
	}

	// The source must survive being used as a template -- it is used once per row.
	TestEqual(TEXT("the source's content root is still parented to the source"),
		Source->GetContentRoot()->GetParent(), (UDreamWidget*)Source.Get());
	TestEqual(TEXT("and the source's tree still holds it"),
		(const UDreamWidget*)Source->GetWidgetTree()->RootWidget, (const UDreamWidget*)Source->GetContentRoot());

	// Registered widgets that reach the collector log an error from an unrelated stack, so tear the
	// hierarchies down the way their owner would.
	Copy->DestroyWidget();
	Source->DestroyWidget();
	Host->DestroyWidget();
	return true;
}

#endif
