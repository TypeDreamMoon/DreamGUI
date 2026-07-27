// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Tests/LexUIPrefabBehaviourTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabEditor/LexUIPrefabBehaviourUtils.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"

// AutoBindAndValidate is the fork's BindWidget: it runs on every prefab save and silently rewrites
// behaviour references. Its failure mode is invisible at author time -- a wrong or unsavable bind
// reports success and comes back null at runtime -- so each branch is pinned here.
namespace LexUIPrefabBehaviourUtilsTestLocal
{
	bool HasProblemContaining(const TArray<FString>& Problems, const TCHAR* Needle)
	{
		return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
	}

	/** Root widget carrying the companion, plus one child per branch the pass can take. */
	struct FAutoBindFixture
	{
		UWorld* World = nullptr;
		ULexUIPrefab* Prefab = nullptr;
		ULexWidget* Root = nullptr;
		ULexUIAutoBindTestBehaviour* Companion = nullptr;

		FAutoBindFixture()
		{
			World = UWorld::CreateWorld(EWorldType::None, false);
			Prefab = NewObject<ULexUIPrefab>();
			// The explicit behaviour class is what lets a NATIVE companion take part; without it
			// the pass falls back to the BP_<PrefabName> blueprint convention.
			Prefab->SetBehaviourClass(ULexUIAutoBindTestBehaviour::StaticClass());
			Root = MakeWidget(TEXT("Root"));
			Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
			Companion = Root->AddComponent<ULexUIAutoBindTestBehaviour>();
		}
		~FAutoBindFixture()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
			}
		}

		ULexWidget* MakeWidget(const TCHAR* DisplayName)
		{
			ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(DisplayName);
			return Widget;
		}
		ULexWidget* AddChild(const TCHAR* DisplayName)
		{
			ULexWidget* Child = MakeWidget(DisplayName);
			Child->TrySetParent(Root, false);
			return Child;
		}
		/** Every branch-covering child at once, for tests that assert the whole result set. */
		void AddStandardChildren()
		{
			AddChild(TEXT("PlayButton"));
			AddChild(TEXT("Ambiguous"));
			AddChild(TEXT("Ambiguous"));
			AddChild(TEXT("NotInstanceEditable"));
			AddChild(TEXT("RuntimeCache"));
			AddChild(TEXT("Unrelated"));
			AddChild(TEXT("Scoreboard"))->AddComponent<ULexUIAutoBindTargetBehaviour>();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindByDisplayNameTest,
	"LGUI.Prefab.Behaviour.AutoBindsWidgetAndBehaviourByDisplayName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindByDisplayNameTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();
	// Canary for the whole file: a rejected attach would surface as a pile of "did not bind"
	// failures across every test, pointing at the pass instead of at the fixture.
	TestEqual(TEXT("Every fixture child attached to the root"), Fixture.Root->GetChildren().Num(), 7);

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	TestNotNull(TEXT("A widget property binds to the same-named widget"), Fixture.Companion->PlayButton.Get());
	TestEqual(TEXT("The widget bind resolves to the named child"),
		Fixture.Companion->PlayButton ? Fixture.Companion->PlayButton->GetDisplayName() : FString(), FString(TEXT("PlayButton")));
	TestNotNull(TEXT("A behaviour property binds to a component on the same-named widget"), Fixture.Companion->Scoreboard.Get());
	TestTrue(TEXT("Bound details name the widget bind"),
		BoundDetails.ContainsByPredicate([](const FString& Detail) { return Detail.Contains(TEXT("PlayButton -> PlayButton")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindAmbiguousNameTest,
	"LGUI.Prefab.Behaviour.AmbiguousDisplayNameIsReportedNotGuessed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindAmbiguousNameTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	TestNull(TEXT("A name matching two widgets binds neither"), Fixture.Companion->Ambiguous.Get());
	TestTrue(TEXT("The ambiguity is reported with its match count"),
		HasProblemContaining(Problems, TEXT("matches 2 widgets by name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindUnsavablePropertiesTest,
	"LGUI.Prefab.Behaviour.TransientAndUnrelatedPropertiesAreLeftAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindUnsavablePropertiesTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	// Positive control first: every assertion below is negative, so without proof that the pass
	// actually ran and bound something, a no-op AutoBindAndValidate would satisfy all of them.
	TestNotNull(TEXT("The pass ran and bound the property it should have"), Fixture.Companion->PlayButton.Get());
	// Exactly two: the widget bind and the behaviour bind. The base class contributes no editable
	// object properties, so this count is a real ceiling, not a floor.
	TestEqual(TEXT("Exactly the two bindable properties were bound"), BoundDetails.Num(), 2);

	// All three have a same-named widget waiting; none may be touched, and none is a problem
	// while null -- they are the designer's business, not a broken binding.
	TestNull(TEXT("A transient property is never auto-bound"), Fixture.Companion->RuntimeCache.Get());
	TestNull(TEXT("A non-Instance-Editable property is never auto-bound"), Fixture.Companion->NotInstanceEditable.Get());
	TestNull(TEXT("A property of an unbindable type is never auto-bound"), Fixture.Companion->Unrelated.Get());
	// Problems quote property names, so the quoted form is the collision-free needle -- a bare
	// "Absent" would also match prose, and matches nothing at all on an empty problem list.
	TestFalse(TEXT("A name with no widget of its own raises no problem"),
		HasProblemContaining(Problems, TEXT("'Absent'")));
	TestTrue(TEXT("...while the fixture's real problems were still reported"), Problems.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindValidateOnlyTest,
	"LGUI.Prefab.Behaviour.ValidateOnlyReportsWithoutBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindValidateOnlyTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, false);

	TestNull(TEXT("Validation alone binds nothing"), Fixture.Companion->PlayButton.Get());
	TestEqual(TEXT("Validation alone reports no binds"), BoundDetails.Num(), 0);
	TestTrue(TEXT("Validation still surfaces ambiguity"),
		HasProblemContaining(Problems, TEXT("matches 2 widgets by name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindForeignReferenceTest,
	"LGUI.Prefab.Behaviour.ReferenceOutsidePrefabIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindForeignReferenceTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();
	// A widget authored outside this prefab's hierarchy: the writer cannot reference it.
	Fixture.Companion->PlayButton = Fixture.MakeWidget(TEXT("Outsider"));

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	TestTrue(TEXT("A reference outside the prefab is reported as unsavable"),
		HasProblemContaining(Problems, TEXT("which is not in this prefab")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindBoundButUnsavableTest,
	"LGUI.Prefab.Behaviour.BoundNonInstanceEditablePropertyIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindBoundButUnsavableTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();
	// Bound by hand to a widget in this prefab, but the writer drops CPF_DisableEditOnInstance,
	// so the value would silently come back null after save.
	Fixture.Companion->NotInstanceEditable = Fixture.Root->FindChildByDisplayName(TEXT("NotInstanceEditable"), true);

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	TestNotNull(TEXT("The hand-made binding is left in place"), Fixture.Companion->NotInstanceEditable.Get());
	TestTrue(TEXT("A bound but non-Instance-Editable property is reported"),
		HasProblemContaining(Problems, TEXT("not Instance Editable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindAmbiguousCompanionTest,
	"LGUI.Prefab.Behaviour.AmbiguousCompanionBindsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindAmbiguousCompanionTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();
	// A second component of the companion class makes the companion ambiguous. Rewriting either
	// one's references would be a guess, so the pass must decline to act at all.
	ULexUIAutoBindTestBehaviour* Duplicate = Fixture.Root->AddComponent<ULexUIAutoBindTestBehaviour>();
	// Pin the fixture shape: without this, a companion that failed to attach at all would produce
	// the same empty result and the test would pass for entirely the wrong reason.
	TestEqual(TEXT("The root carries exactly two companion-class components"),
		Fixture.Root->GetAllComponents().Num(), 2);

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	TestNull(TEXT("An ambiguous companion binds nothing"), Fixture.Companion->PlayButton.Get());
	TestEqual(TEXT("An ambiguous companion reports no binds"), BoundDetails.Num(), 0);

	// Control: an asserted absence proves nothing by itself, because a pass that does nothing at
	// all produces the same empty result. Removing the ambiguity must make this fixture bind.
	Duplicate->DestroyComponent();
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);
	TestNotNull(TEXT("The same fixture binds once the companion is unambiguous"), Fixture.Companion->PlayButton.Get());
	return true;
}

// The sub-prefab branches need a real helper object, which only exists once the prefab has been
// serialized -- LexUIPrefab lazily builds it, and only for a stamped prefab version.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindSubPrefabWidgetTest,
	"LGUI.Prefab.Behaviour.SubPrefabWidgetIsReportedNotBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindSubPrefabWidgetTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSystem;
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();
	ULexWidget* NestedWidget = Fixture.AddChild(TEXT("InsideSubPrefab"));

	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Fixture.Root, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Fixture prefab serialized"),
		WidgetSerializer::SavePrefab(Fixture.Root, Fixture.Prefab, ObjectToGuid, EmptySubPrefabs, true));

	ULexUIPrefabHelperObject* Helper = Fixture.Prefab->GetPrefabHelperObject();
	TestNotNull(TEXT("Serialized prefab has a helper object"), Helper);
	if (Helper == nullptr)
	{
		return false;
	}
	// Register the widget as sub-prefab content, the same shape MakePrefabAsSubPrefab produces.
	// The pass reads exactly this map to decide what the writer will refuse to reference.
	FLexUISubPrefabData NestedData;
	NestedData.MapGuidToObject.Add(FGuid::NewGuid(), NestedWidget);
	Helper->SubPrefabMap.Add(NestedWidget, MoveTemp(NestedData));

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	TestNull(TEXT("A widget inside a sub-prefab is never auto-bound"), Fixture.Companion->InsideSubPrefab.Get());
	// Match the unbound-name branch specifically: the existing-reference branch emits its own
	// "points to ... inside a sub-prefab instance" wording, so a generic needle cannot tell them apart.
	TestTrue(TEXT("The sub-prefab-only name match is reported with its count"),
		HasProblemContaining(Problems, TEXT("matches 1 widget(s) inside a sub-prefab instance")));
	// Positive control: without this the null assertion above would also pass if the pass never ran.
	TestNotNull(TEXT("Widgets outside the sub-prefab still bind normally"), Fixture.Companion->PlayButton.Get());

	Helper->ClearLoadedPrefab();
	Fixture.Prefab->ClearPrefabInstanceScene();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIAutoBindExistingSubPrefabReferenceTest,
	"LGUI.Prefab.Behaviour.ExistingSubPrefabReferenceIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIAutoBindExistingSubPrefabReferenceTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSystem;
	using namespace LexUIPrefabBehaviourUtilsTestLocal;
	FAutoBindFixture Fixture;
	Fixture.AddStandardChildren();
	// Deliberately named after NO property: a widget named "InsideSubPrefab" would also trip the
	// unbound-name branch, whose message shares the "inside a sub-prefab instance" wording and
	// would keep this test green even with the branch it targets deleted.
	ULexWidget* NestedWidget = Fixture.AddChild(TEXT("NestedOnly"));

	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Fixture.Root, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Fixture prefab serialized"),
		WidgetSerializer::SavePrefab(Fixture.Root, Fixture.Prefab, ObjectToGuid, EmptySubPrefabs, true));

	ULexUIPrefabHelperObject* Helper = Fixture.Prefab->GetPrefabHelperObject();
	TestNotNull(TEXT("Serialized prefab has a helper object"), Helper);
	if (Helper == nullptr)
	{
		return false;
	}
	FLexUISubPrefabData NestedData;
	NestedData.MapGuidToObject.Add(FGuid::NewGuid(), NestedWidget);
	Helper->SubPrefabMap.Add(NestedWidget, MoveTemp(NestedData));
	// Already wired by hand -- the pass must still call it out, because the writer drops it.
	Fixture.Companion->PlayButton = NestedWidget;

	TArray<FString> BoundDetails, Problems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(Fixture.Root, Fixture.Prefab, BoundDetails, Problems, true);

	// Name both the property and its target, so only the existing-reference branch can satisfy it.
	TestTrue(TEXT("An existing reference into a sub-prefab is reported as unsavable"),
		HasProblemContaining(Problems, TEXT("'PlayButton' points to 'NestedOnly' inside a sub-prefab instance")));
	// Pinning the count keeps the isolation honest: if the unbound-name branch ever fires here
	// again, this fixture is no longer testing only the branch it claims to.
	TestEqual(TEXT("Only the name clash and this reference are reported"), Problems.Num(), 2);

	Helper->ClearLoadedPrefab();
	Fixture.Prefab->ClearPrefabInstanceScene();
	return true;
}

#endif
