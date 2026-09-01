// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayoutFragment.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetPlacement.h"
#include "Engine/World.h"
#include "Interaction/DreamResponsiveBinding.h"
#include "UObject/UnrealType.h"

/*
 * Four types that decide something before anything is drawn: which breakpoint a widget is in, which
 * property a binding is allowed to copy, what a measurement constraint means, and what a slot looked
 * like before something moved it. All four answer questions that are pure arithmetic or pure
 * bookkeeping, which is why they can be pinned at all in a suite that runs -nullrhi with no viewport.
 *
 * A word on where the assertions are aimed, because most of what these types expose is a setter and
 * a getter and testing those proves nothing. The parts with an opinion in them are:
 *
 *   FDreamResponsiveRule    - an interval whose edges are inclusive and whose ceiling is optional,
 *                             expressed as "zero or less means no ceiling". Getting the boundary
 *                             wrong is a widget that flickers between two layouts at exactly the
 *                             window width someone will actually use.
 *   UDreamResponsiveBehaviour - first match wins; the size it matches against is the PARENT's, not
 *                             the widget's own; and a size that matches NO rule restores the
 *                             appearance the widget had before any rule took over, because the
 *                             rules are overrides rather than a complete description. All three are
 *                             choices a reader would guess wrong.
 *   UDreamDataBinding       - five widget properties go through the widget's setter and everything
 *                             else is a raw memory copy. Which side of that line a property falls on
 *                             changes whether the children of the target hear about the change.
 *   FDreamMeasureSpec       - a number plus a mode, where the mode is the whole point: Undefined
 *                             specs compare equal to each other regardless of their value.
 *   FDreamWidgetPlacement   - already has a round-trip test next door
 *                             (DreamWidgetPlacementAutomationTests.cpp). What is asserted here is
 *                             the two cases that one does not reach: re-using one record for a
 *                             second widget, and restoring a widget that never actually left.
 *
 * What is deliberately NOT asserted here:
 *
 *   UDreamDataBinding::Awake and ::Tick. Awake decides whether to apply immediately and whether to
 *   keep ticking, and both are unreachable from a test: Awake is protected and is driven by
 *   UDreamUIManagerWorldSubsystem, and SetCanExecuteTick's effect is only visible to the same
 *   driver. ApplyBinding is the whole of the decision-making and it is public, so that is what is
 *   exercised. A test that spun a manager just to observe a tick flag would be testing the manager.
 *
 *   UDreamResponsiveBehaviour::OnDimensionsChanged and ::OnAttachmentChanged. Both arrive through
 *   UDreamUIBehaviour::Call_*, which under WITH_EDITOR opens with "if (!GetWorld()) return;" and
 *   otherwise defers everything until Awake. So in any fixture the callbacks either never fire or
 *   fire at a moment a test cannot reach. The parent's dimension delegate, which OnRegister
 *   subscribes to directly, has no such gate -- that is the path exercised below, and it is also the
 *   path that actually drives the feature at runtime.
 */

namespace DreamBindingPlacementTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/**
	 * Reach a protected UPROPERTY the way the Details panel does.
	 *
	 * Every field these tests need to set is EditAnywhere or VisibleAnywhere and protected, so the
	 * only two ways in are reflection or a Blueprint. Reflection is the one the rest of this suite
	 * already uses. The point of routing it through a helper is the error: a rename that moves the
	 * field silently turns an assertion into a no-op, and a no-op that reports success is worse than
	 * no test at all.
	 */
	template<typename T>
	T* FieldPtr(FAutomationTestBase& Test, UObject* Object, const TCHAR* PropertyName)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(
				TEXT("%s no longer has a property named '%s', so this test is asserting against nothing."),
				*Object->GetClass()->GetName(), PropertyName));
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<T>(Object);
	}

	/** The rule list, with the element type checked as well as the name -- see FieldPtr on why. */
	TArray<FDreamResponsiveRule>* RulesOf(FAutomationTestBase& Test, UDreamResponsiveBehaviour* Behaviour)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(
			UDreamResponsiveBehaviour::StaticClass()->FindPropertyByName(FName(TEXT("Rules"))));
		const FStructProperty* Inner = Property != nullptr ? CastField<FStructProperty>(Property->Inner) : nullptr;
		if (Inner == nullptr || Inner->Struct != FDreamResponsiveRule::StaticStruct())
		{
			Test.AddError(TEXT("UDreamResponsiveBehaviour no longer holds a TArray<FDreamResponsiveRule> named 'Rules'."));
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<TArray<FDreamResponsiveRule>>(Behaviour);
	}

	FDreamResponsiveRule MakeRule(const TCHAR* Name, float MinWidth, float MaxWidth)
	{
		FDreamResponsiveRule Rule;
		Rule.Name = FName(Name);
		Rule.MinWidth = MinWidth;
		Rule.MaxWidth = MaxWidth;
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamResponsiveRuleEdgesTest,
	"DreamGUI.Responsive.ABreakpointIncludesItsOwnEdgesAndAZeroCeilingMeansNoCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamResponsiveRuleEdgesTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;

	// The edges are where a breakpoint is felt. A viewport parked at exactly 900 is not an edge case
	// anyone contrived -- it is a common window size, a common canvas reference resolution, and the
	// number an author types into the field. An exclusive edge there is a widget that changes layout
	// when nothing changed, or refuses to at the one width the author was looking at.
	{
		FDreamResponsiveRule Rule = MakeRule(TEXT("Medium"), 600.0f, 900.0f);
		TestTrue(TEXT("exactly at the floor is inside the rule"), Rule.Matches(FVector2D(600.0, 0.0)));
		TestFalse(TEXT("a hair under the floor is outside"), Rule.Matches(FVector2D(599.9, 0.0)));
		TestTrue(TEXT("exactly at the ceiling is inside the rule"), Rule.Matches(FVector2D(900.0, 0.0)));
		TestFalse(TEXT("a hair over the ceiling is outside"), Rule.Matches(FVector2D(900.1, 0.0)));
	}

	// A ceiling is optional and its absence is spelled zero, because the field is a float with a
	// ClampMin of 0 and there is no other way to say "open ended" in a Details panel. This is the
	// part a reader guesses wrong: an author who leaves MaxWidth at its default has not written a
	// rule that matches nothing wider than zero, they have written the topmost breakpoint.
	{
		FDreamResponsiveRule Open = MakeRule(TEXT("Wide"), 1200.0f, 0.0f);
		TestTrue(TEXT("an unset ceiling does not cap anything"), Open.Matches(FVector2D(100000.0, 0.0)));
		TestFalse(TEXT("but the floor still holds"), Open.Matches(FVector2D(1199.0, 0.0)));

		// The test is "<= 0", not "== 0", so a negative typed past the clamp reads the same way
		// rather than becoming a rule that can never match.
		Open.MaxWidth = -1.0f;
		TestTrue(TEXT("a negative ceiling is also no ceiling"), Open.Matches(FVector2D(100000.0, 0.0)));
	}

	// Both axes have to agree. A rule is an AND across width and height, so a tall narrow phone does
	// not satisfy a rule written for a short wide one just because one number happens to fit.
	{
		FDreamResponsiveRule Landscape;
		Landscape.Name = TEXT("Landscape");
		Landscape.MinWidth = 600.0f;
		Landscape.MinHeight = 400.0f;
		TestTrue(TEXT("both floors cleared is a match"), Landscape.Matches(FVector2D(800.0, 500.0)));
		TestFalse(TEXT("wide enough but too short is not"), Landscape.Matches(FVector2D(800.0, 300.0)));
		TestFalse(TEXT("tall enough but too narrow is not"), Landscape.Matches(FVector2D(500.0, 500.0)));

		Landscape.MaxHeight = 600.0f;
		TestFalse(TEXT("and a height ceiling excludes on its own"), Landscape.Matches(FVector2D(800.0, 700.0)));
	}

	// A rule with nothing filled in matches everything. That is the catch-all idiom, and it is the
	// reason ordering matters so much in the test below: a default-constructed rule sitting at the
	// top of the list makes every rule after it unreachable, and nothing complains.
	{
		const FDreamResponsiveRule Any;
		TestTrue(TEXT("an empty rule matches a zero-sized parent"), Any.Matches(FVector2D::ZeroVector));
		TestTrue(TEXT("an empty rule matches anything at all"), Any.Matches(FVector2D(4000.0, 4000.0)));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamResponsiveFirstMatchTest,
	"DreamGUI.Responsive.TheFirstMatchingRuleWinsSoRulesAreOrderedRatherThanScored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamResponsiveFirstMatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Frame = MakeWidget(TestWorld.World, nullptr, TEXT("Frame"), 1000.0f, 600.0f);
	UDreamWidget* Panel = MakeWidget(TestWorld.World, Frame, TEXT("Panel"), 100.0f, 100.0f);
	UDreamResponsiveBehaviour* Responsive = Panel->AddComponent<UDreamResponsiveBehaviour>();
	if (!TestNotNull(TEXT("the panel carries a responsive behaviour"), Responsive))
	{
		return false;
	}
	TArray<FDreamResponsiveRule>* Rules = RulesOf(*this, Responsive);
	if (Rules == nullptr)
	{
		return false;
	}

	// Two rules whose ranges overlap at 1000 wide. There is no "most specific wins" here and no
	// score: the loop returns on the first Matches that answers true, so the answer is entirely a
	// function of where the author dragged the array entries.
	*Rules = { MakeRule(TEXT("Wide"), 800.0f, 0.0f), MakeRule(TEXT("Any"), 0.0f, 0.0f) };
	Responsive->EvaluateResponsiveRules();
	TestEqual(TEXT("the earlier of two matching rules is the one that wins"),
		Responsive->GetActiveRule(), FName(TEXT("Wide")));

	// Swap them and the same parent size gives a different answer. This is the assertion that makes
	// the previous one mean something: without it, "Wide" could have been chosen because it fits
	// better rather than because it is first.
	*Rules = { MakeRule(TEXT("Any"), 0.0f, 0.0f), MakeRule(TEXT("Wide"), 800.0f, 0.0f) };
	Responsive->EvaluateResponsiveRules();
	TestEqual(TEXT("a catch-all placed first makes every rule after it unreachable"),
		Responsive->GetActiveRule(), FName(TEXT("Any")));

	// The rules also carry the visibility and opacity to apply, and applying them is the point.
	{
		FDreamResponsiveRule Hide = MakeRule(TEXT("TooNarrow"), 0.0f, 500.0f);
		Hide.Visibility = EDreamWidgetVisibility::Collapsed;
		Hide.RenderOpacity = 0.25f;
		*Rules = { Hide };
		Frame->SetWidth(400.0f);
		Responsive->EvaluateResponsiveRules();
		TestEqual(TEXT("the matching rule's visibility is applied to the widget"),
			Panel->GetVisibility(), EDreamWidgetVisibility::Collapsed);
		TestEqual(TEXT("and so is its opacity"), Panel->GetRenderOpacity(), 0.25f);
	}

	// And what a size that matches NOTHING means. The rules are overrides on the appearance the
	// widget was authored with rather than a complete description of it, so falling out of every one
	// of them gives that appearance back instead of leaving the widget in the last match's costume
	// with no rule claiming responsibility for it. The state is reached two ways that are
	// indistinguishable from inside the component -- a gap left in the middle of a rule set by
	// accident, and a rule set that deliberately only overrides at some sizes -- which is why the
	// benign reading is the one implemented.
	//
	// The baseline here is Visible at full opacity because that is what the Panel was built with,
	// several blocks above, before the first rule ever ran.
	{
		*Rules = { MakeRule(TEXT("OnlyWide"), 900.0f, 0.0f) };
		Frame->SetWidth(400.0f);
		Responsive->EvaluateResponsiveRules();
		TestEqual(TEXT("no matching rule clears the active rule"), Responsive->GetActiveRule(), FName(NAME_None));
		TestEqual(TEXT("...and restores the visibility from before any rule took over"),
			Panel->GetVisibility(), EDreamWidgetVisibility::Visible);
		TestEqual(TEXT("...and that opacity too"), Panel->GetRenderOpacity(), 1.0f);
	}

	// The assertion above cannot tell a restored baseline from a hardcoded "Visible, 1.0", and the
	// difference is the whole feature: a widget authored dimmed and non-hit-testable must come back
	// dimmed and non-hit-testable. This block gives the panel an appearance nothing would guess,
	// lets a rule take it away, and takes the rule away again.
	{
		Panel->SetVisibility(EDreamWidgetVisibility::HitTestInvisible);
		Panel->SetRenderOpacity(0.6f);

		FDreamResponsiveRule Hide = MakeRule(TEXT("TooNarrow"), 0.0f, 500.0f);
		Hide.Visibility = EDreamWidgetVisibility::Collapsed;
		Hide.RenderOpacity = 0.1f;
		*Rules = { Hide };
		Responsive->EvaluateResponsiveRules();
		TestEqual(TEXT("the rule takes the widget over"), Panel->GetVisibility(), EDreamWidgetVisibility::Collapsed);

		*Rules = { MakeRule(TEXT("OnlyWide"), 900.0f, 0.0f) };
		Responsive->EvaluateResponsiveRules();
		TestEqual(TEXT("what comes back is the authored appearance, not a default"),
			Panel->GetVisibility(), EDreamWidgetVisibility::HitTestInvisible);
		TestEqual(TEXT("...including an opacity nobody would have guessed"),
			Panel->GetRenderOpacity(), 0.6f);
	}

	// And the other half of the bargain, which is why the baseline is captured at takeover rather
	// than when the component is created: a component whose rules have never matched anything must
	// be indistinguishable from no component at all. It has borrowed nothing, so it writes nothing
	// -- otherwise merely dropping a responsive behaviour onto a widget would rewrite that widget's
	// appearance the first time a parent happened to resize.
	{
		*Rules = { MakeRule(TEXT("OnlyWide"), 900.0f, 0.0f) };
		Panel->SetVisibility(EDreamWidgetVisibility::Hidden);
		Panel->SetRenderOpacity(0.35f);
		Responsive->EvaluateResponsiveRules();
		TestEqual(TEXT("a behaviour that never matched leaves the visibility alone"),
			Panel->GetVisibility(), EDreamWidgetVisibility::Hidden);
		TestEqual(TEXT("...and the opacity alone"), Panel->GetRenderOpacity(), 0.35f);
	}

	Frame->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamResponsiveReferenceSizeTest,
	"DreamGUI.Responsive.ARuleIsChosenByTheParentsSizeRatherThanTheWidgetsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamResponsiveReferenceSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	// The widget and its parent are given deliberately contradictory sizes, because a rule set that
	// happens to give the same answer for both cannot tell which one was read. This matters in
	// practice: the widget's own size is usually the thing the rule is about to CHANGE, so matching
	// against it would make the breakpoint depend on its own last answer.
	UDreamWidget* Frame = MakeWidget(TestWorld.World, nullptr, TEXT("Frame"), 1000.0f, 600.0f);
	UDreamWidget* Panel = MakeWidget(TestWorld.World, Frame, TEXT("Panel"), 100.0f, 100.0f);
	UDreamResponsiveBehaviour* Responsive = Panel->AddComponent<UDreamResponsiveBehaviour>();
	if (!TestNotNull(TEXT("the panel carries a responsive behaviour"), Responsive))
	{
		return false;
	}
	TArray<FDreamResponsiveRule>* Rules = RulesOf(*this, Responsive);
	if (Rules == nullptr)
	{
		return false;
	}

	*Rules = { MakeRule(TEXT("Wide"), 800.0f, 0.0f), MakeRule(TEXT("Narrow"), 0.0f, 799.0f) };
	Responsive->EvaluateResponsiveRules();
	TestEqual(TEXT("the parent's 1000 decides, not the widget's own 100"),
		Responsive->GetActiveRule(), FName(TEXT("Wide")));

	Panel->SetWidth(2000.0f);
	Responsive->EvaluateResponsiveRules();
	TestEqual(TEXT("growing the widget itself changes nothing"),
		Responsive->GetActiveRule(), FName(TEXT("Wide")));

	Frame->SetWidth(400.0f);
	Responsive->EvaluateResponsiveRules();
	TestEqual(TEXT("shrinking the parent is what moves the breakpoint"),
		Responsive->GetActiveRule(), FName(TEXT("Narrow")));

	// A root has no parent to measure against, so it falls back to its own size. Without the
	// fallback the reference would be zero and a root would permanently sit in the narrowest rule,
	// which is exactly what a full-screen root is not.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1600.0f, 900.0f);
	UDreamResponsiveBehaviour* RootResponsive = Root->AddComponent<UDreamResponsiveBehaviour>();
	if (!TestNotNull(TEXT("the root carries a responsive behaviour"), RootResponsive))
	{
		return false;
	}
	TArray<FDreamResponsiveRule>* RootRules = RulesOf(*this, RootResponsive);
	if (RootRules == nullptr)
	{
		return false;
	}
	*RootRules = { MakeRule(TEXT("Wide"), 800.0f, 0.0f), MakeRule(TEXT("Narrow"), 0.0f, 799.0f) };
	RootResponsive->EvaluateResponsiveRules();
	TestEqual(TEXT("a parentless widget measures itself"), RootResponsive->GetActiveRule(), FName(TEXT("Wide")));

	Frame->DestroyWidget();
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamResponsiveResizeSubscriptionTest,
	"DreamGUI.Responsive.AParentResizeReEvaluatesAndRemovingTheComponentStopsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamResponsiveResizeSubscriptionTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Frame = MakeWidget(TestWorld.World, nullptr, TEXT("Frame"), 1000.0f, 600.0f);
	UDreamWidget* Panel = MakeWidget(TestWorld.World, Frame, TEXT("Panel"), 100.0f, 100.0f);

	// AddComponent runs the behaviour's OnRegister because the widget is already registered, and
	// OnRegister is where the subscription to the PARENT's dimension event is made. That ordering is
	// the whole reason the parent is attached before the component is added here.
	UDreamResponsiveBehaviour* Responsive = Panel->AddComponent<UDreamResponsiveBehaviour>();
	if (!TestNotNull(TEXT("the panel carries a responsive behaviour"), Responsive))
	{
		return false;
	}
	TArray<FDreamResponsiveRule>* Rules = RulesOf(*this, Responsive);
	FName* ActiveRule = FieldPtr<FName>(*this, Responsive, TEXT("ActiveRule"));
	if (Rules == nullptr || ActiveRule == nullptr)
	{
		return false;
	}
	*Rules = { MakeRule(TEXT("Wide"), 800.0f, 0.0f), MakeRule(TEXT("Narrow"), 0.0f, 799.0f) };
	Responsive->EvaluateResponsiveRules();
	TestEqual(TEXT("the starting size picks the wide rule"), Responsive->GetActiveRule(), FName(TEXT("Wide")));

	// Nothing here calls EvaluateResponsiveRules. If the subscription made in OnRegister is not
	// live, the rule below simply does not move and the assertion fails -- which is the point: this
	// is the path that makes the feature work at runtime, and it is the only one of the behaviour's
	// three re-evaluation triggers that is not gated behind Awake.
	Frame->SetWidth(400.0f);
	TestEqual(TEXT("resizing the parent re-evaluates without anyone asking"),
		Responsive->GetActiveRule(), FName(TEXT("Narrow")));

	// Height matters as much as width, and a delegate that read only the width flag would leave
	// every height-driven rule dead. The sentinel is what makes this an assertion rather than a
	// coincidence: these rules constrain width only, so without wiping the answer first this would
	// read "Narrow" whether the re-evaluation ran or not.
	*ActiveRule = FName(TEXT("Sentinel"));
	Frame->SetHeight(200.0f);
	TestEqual(TEXT("a height-only change re-evaluates too"),
		Responsive->GetActiveRule(), FName(TEXT("Narrow")));

	// A pivot change comes down the same delegate with both size flags false, and must not trigger
	// anything. Every widget in a hierarchy that moves its pivot would otherwise re-run every
	// descendant's rule list for a change that cannot alter the answer.
	*ActiveRule = FName(TEXT("Sentinel"));
	Frame->SetPivot(FVector2D(0.25, 0.75));
	TestEqual(TEXT("a pivot-only change does not re-evaluate"),
		Responsive->GetActiveRule(), FName(TEXT("Sentinel")));

	// And the other half of the bargain. RemoveComponent runs OnUnregister, which is where the
	// handle is given back. A behaviour that unsubscribed only on destruction would keep answering
	// resizes for a component the widget no longer has.
	Panel->RemoveComponent(Responsive);
	*ActiveRule = FName(TEXT("Sentinel"));
	Frame->SetWidth(1600.0f);
	TestEqual(TEXT("a removed behaviour stops hearing about resizes"),
		Responsive->GetActiveRule(), FName(TEXT("Sentinel")));

	Frame->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDataBindingRefusalTest,
	"DreamGUI.Binding.ABindingItCannotSatisfyRefusesRatherThanGuessing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDataBindingRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Host = MakeWidget(TestWorld.World, nullptr, TEXT("Host"), 100.0f, 100.0f);
	UDreamWidget* Source = MakeWidget(TestWorld.World, nullptr, TEXT("Source"), 100.0f, 100.0f);
	UDreamDataBinding* Binding = Host->AddComponent<UDreamDataBinding>();
	if (!TestNotNull(TEXT("the host carries a data binding"), Binding))
	{
		return false;
	}

	TObjectPtr<UObject>* SourceObject = FieldPtr<TObjectPtr<UObject>>(*this, Binding, TEXT("SourceObject"));
	TObjectPtr<UObject>* TargetObject = FieldPtr<TObjectPtr<UObject>>(*this, Binding, TEXT("TargetObject"));
	FName* SourceProperty = FieldPtr<FName>(*this, Binding, TEXT("SourceProperty"));
	FName* TargetProperty = FieldPtr<FName>(*this, Binding, TEXT("TargetProperty"));
	if (SourceObject == nullptr || TargetObject == nullptr || SourceProperty == nullptr || TargetProperty == nullptr)
	{
		return false;
	}

	// Nothing configured at all. This is the state every freshly added component is in, and it will
	// be asked to apply on the frame it Awakes, so refusing quietly is the only tolerable answer.
	TestFalse(TEXT("a binding with nothing filled in refuses"), Binding->ApplyBinding());

	// A source with no property named on it.
	*SourceObject = Source;
	TestFalse(TEXT("a source object with no property named refuses"), Binding->ApplyBinding());

	// Named on one side only. Half-configured is the normal state while an author is typing, and it
	// is also what a rename leaves behind.
	*SourceProperty = FName(TEXT("RenderOpacity"));
	TestFalse(TEXT("a source property with no target property refuses"), Binding->ApplyBinding());

	// A name that resolves to nothing. This is the failure mode a rename produces, and it must not
	// be mistaken for a valid binding of zero bytes.
	*TargetProperty = FName(TEXT("NoSuchPropertyExists"));
	TestFalse(TEXT("a target property that does not resolve refuses"), Binding->ApplyBinding());

	// Both sides resolve but the types disagree. Without the SameType gate this is the branch that
	// writes a float's bits into an enum, which is not a compile error and not a crash -- it is a
	// widget in a state its own enum has no name for.
	*SourceProperty = FName(TEXT("Visibility"));
	*TargetProperty = FName(TEXT("RenderOpacity"));
	TestFalse(TEXT("a type mismatch refuses rather than copying bytes"), Binding->ApplyBinding());

	// The positive control, without which every assertion above could be satisfied by an
	// ApplyBinding that returns false unconditionally. Note what is NOT set here: TargetObject is
	// still null, and the binding falls back to the owning widget. That fallback is the ordinary
	// case -- a binding usually drives the widget it is sitting on -- so a version that required an
	// explicit target would look correct in a header and be useless in a prefab.
	*SourceProperty = FName(TEXT("RenderOpacity"));
	*TargetProperty = FName(TEXT("RenderOpacity"));
	Source->SetRenderOpacity(0.5f);
	TestTrue(TEXT("a well-formed binding applies"), Binding->ApplyBinding());
	TestEqual(TEXT("...to the owning widget, because no target object was named"),
		Host->GetRenderOpacity(), 0.5f);

	// An explicitly named target wins over the owning-widget default, which is what lets one
	// behaviour drive a sibling rather than itself.
	UDreamWidget* Other = MakeWidget(TestWorld.World, nullptr, TEXT("Other"), 100.0f, 100.0f);
	*TargetObject = Other;
	Source->SetRenderOpacity(0.25f);
	TestTrue(TEXT("a binding with an explicit target applies"), Binding->ApplyBinding());
	TestEqual(TEXT("the named target received it"), Other->GetRenderOpacity(), 0.25f);
	TestEqual(TEXT("and the owning widget was left alone"), Host->GetRenderOpacity(), 0.5f);

	Host->DestroyWidget();
	Source->DestroyWidget();
	Other->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDataBindingSetterPathTest,
	"DreamGUI.Binding.AWidgetPropertyGoesThroughTheWidgetsSetterRatherThanARawCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDataBindingSetterPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	// A parent with a child, because the difference between the two paths is only visible one level
	// down. Visibility, WidgetActive, Interactable and Raycastable all keep a cached
	// "...InHierarchy" answer on every descendant, recomputed by the setter. A raw
	// CopyCompleteValue into the parent's field leaves every one of those caches stale, and the
	// widget then LOOKS right in the Details panel while its children behave as though nothing
	// happened. That is the failure this test exists to catch, so every assertion below reads the
	// child rather than the target.
	UDreamWidget* Target = MakeWidget(TestWorld.World, nullptr, TEXT("Target"), 400.0f, 400.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Target, TEXT("Child"), 100.0f, 100.0f);
	UDreamWidget* Source = MakeWidget(TestWorld.World, nullptr, TEXT("Source"), 100.0f, 100.0f);

	UDreamWidget* Host = MakeWidget(TestWorld.World, nullptr, TEXT("Host"), 100.0f, 100.0f);
	UDreamDataBinding* Binding = Host->AddComponent<UDreamDataBinding>();
	if (!TestNotNull(TEXT("the host carries a data binding"), Binding))
	{
		return false;
	}
	TObjectPtr<UObject>* SourceObject = FieldPtr<TObjectPtr<UObject>>(*this, Binding, TEXT("SourceObject"));
	TObjectPtr<UObject>* TargetObject = FieldPtr<TObjectPtr<UObject>>(*this, Binding, TEXT("TargetObject"));
	FName* SourceProperty = FieldPtr<FName>(*this, Binding, TEXT("SourceProperty"));
	FName* TargetProperty = FieldPtr<FName>(*this, Binding, TEXT("TargetProperty"));
	if (SourceObject == nullptr || TargetObject == nullptr || SourceProperty == nullptr || TargetProperty == nullptr)
	{
		return false;
	}
	*SourceObject = Source;
	*TargetObject = Target;

	// All four cached answers start true, so each assertion below is a change from a known state
	// rather than a reading of whatever the fixture happened to leave behind.
	TestTrue(TEXT("the child starts out render-visible"), Child->GetRenderVisibleInHierarchy());
	TestTrue(TEXT("the child starts out active in hierarchy"), Child->GetWidgetActiveInHierarchy());
	TestTrue(TEXT("the child starts out interactable in hierarchy"), Child->GetInteractableInHierarchy());
	TestTrue(TEXT("the child starts out raycastable in hierarchy"), Child->GetRaycastableInHierarchy());

	// Visibility is one of the five names the binding special-cases by hand.
	*SourceProperty = FName(TEXT("Visibility"));
	*TargetProperty = FName(TEXT("Visibility"));
	Source->SetVisibility(EDreamWidgetVisibility::Collapsed);
	TestTrue(TEXT("the visibility binding applies"), Binding->ApplyBinding());
	TestEqual(TEXT("the target's own visibility followed"), Target->GetVisibility(), EDreamWidgetVisibility::Collapsed);
	TestFalse(TEXT("and so did the child's cached answer, which only the setter recomputes"),
		Child->GetRenderVisibleInHierarchy());

	// bWidgetActive takes a different route again: the source value is read through an FBoolProperty
	// rather than reinterpreted, because a bool UPROPERTY may be a bitfield and its address is not
	// its value.
	*SourceProperty = UDreamWidget::GetPropertyName_WidgetActive();
	*TargetProperty = UDreamWidget::GetPropertyName_WidgetActive();
	Source->SetWidgetActive(false);
	TestTrue(TEXT("the widget-active binding applies"), Binding->ApplyBinding());
	TestFalse(TEXT("the target went inactive"), Target->GetWidgetActive());
	TestFalse(TEXT("and the child heard about it"), Child->GetWidgetActiveInHierarchy());

	// All five names ApplyBinding matches now come from a GetPropertyName_ accessor, so a rename
	// cannot compile without visiting the branch as well. What is left to check is the OTHER half of
	// that guarantee: an accessor is only as good as the property it names still existing, and
	// GET_MEMBER_NAME_CHECKED verifies the member at compile time but says nothing about whether the
	// UPROPERTY macro is still on it. A property that lost its UPROPERTY compiles here and resolves
	// to nothing at runtime, which is the same silent fall-through by another road.
	const FName MatchedNames[] =
	{
		UDreamWidget::GetPropertyName_RenderOpacity(),
		UDreamWidget::GetPropertyName_Interactable(),
		UDreamWidget::GetPropertyName_Raycastable(),
		UDreamWidget::GetPropertyName_Visibility(),
		UDreamWidget::GetPropertyName_WidgetActive(),
	};
	for (const FName& MatchedName : MatchedNames)
	{
		TestTrue(
			FString::Printf(TEXT("'%s' is still a reflected property, which is what ApplyBinding matches against"), *MatchedName.ToString()),
			UDreamWidget::StaticClass()->FindPropertyByName(MatchedName) != nullptr);
	}

	// And the damage a retired branch does, which is why this matters at all: it is not an error
	// and not a refusal. The generic CopyCompleteValue at the bottom of ApplyBinding takes over,
	// the value lands, the Details panel
	// looks right, and the setter never runs -- so the only visible symptom is a descendant whose
	// cached "...InHierarchy" answer quietly stops following its parent, several screens away from
	// the rename. That is what these two read the CHILD for.
	*SourceProperty = FName(TEXT("Interactable"));
	*TargetProperty = FName(TEXT("Interactable"));
	Source->SetInteractable(EDreamWidgetInteractableType::Disabled);
	TestTrue(TEXT("the interactable binding applies"), Binding->ApplyBinding());
	TestFalse(TEXT("the child's cached interactable answer followed"), Child->GetInteractableInHierarchy());

	*SourceProperty = FName(TEXT("Raycastable"));
	*TargetProperty = FName(TEXT("Raycastable"));
	Source->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	TestTrue(TEXT("the raycastable binding applies"), Binding->ApplyBinding());
	TestFalse(TEXT("the child's cached raycastable answer followed"), Child->GetRaycastableInHierarchy());

	// And the other side of the line, so the assertions above are not merely describing "widgets
	// have setters". bIgnoreLayout is a widget property with a real setter that is NOT in the
	// special-cased five, so it takes the generic path: the bytes are copied and SetIgnoreLayout
	// never runs. The value lands, which is all the binding promises -- but anything that setter
	// would have done does not happen, and that is worth knowing before binding a sixth property.
	*SourceProperty = FName(TEXT("bIgnoreLayout"));
	*TargetProperty = FName(TEXT("bIgnoreLayout"));
	bool* SourceIgnoreLayout = FieldPtr<bool>(*this, Source, TEXT("bIgnoreLayout"));
	if (SourceIgnoreLayout == nullptr)
	{
		return false;
	}
	*SourceIgnoreLayout = true;
	TestFalse(TEXT("the target starts out honouring its layout"), Target->GetIgnoreLayout());
	TestTrue(TEXT("a property outside the special-cased five still applies"), Binding->ApplyBinding());
	TestTrue(TEXT("...by copying the value straight in"), Target->GetIgnoreLayout());

	Target->DestroyWidget();
	Source->DestroyWidget();
	Host->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeasureSpecTest,
	"DreamGUI.Layout.MeasureSpec.AConstraintSaysWhetherItIsACeilingOrAnInstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeasureSpecTest::RunTest(const FString& Parameters)
{
	// The number alone is ambiguous, which is the entire reason the mode exists. 300 can mean "you
	// have 300 to play with", "you are 300", or nothing at all, and a layout handed the bare number
	// has to guess. Guessing is what made wrapping text need a second pass: measured with no
	// constraint it reports its one-line width, and the panel that already committed to a row height
	// finds out next frame.

	// Undefined ignores its own value entirely and reports the content back. The value is carried
	// rather than forbidden, so this is worth pinning: a caller that builds Undefined by hand with a
	// leftover number must not have that number quietly applied.
	{
		const FDreamMeasureSpec Spec(300.0f, EDreamMeasureMode::Undefined);
		TestEqual(TEXT("an unconstrained spec reports the content unchanged"), Spec.Resolve(500.0f), 500.0f);
		TestEqual(TEXT("...whatever value it happens to be carrying"), Spec.Resolve(50.0f), 50.0f);
		TestEqual(TEXT("and a negative content size is floored rather than passed on"), Spec.Resolve(-10.0f), 0.0f);
	}

	// AtMost is a ceiling: content that fits is untouched, content that does not is clipped to the
	// ceiling. The equal case belongs to "fits".
	{
		const FDreamMeasureSpec Spec = FDreamMeasureSpec::AtMost(300.0f);
		TestEqual(TEXT("content under the ceiling is reported as it is"), Spec.Resolve(120.0f), 120.0f);
		TestEqual(TEXT("content exactly at the ceiling is reported as it is"), Spec.Resolve(300.0f), 300.0f);
		TestEqual(TEXT("content over the ceiling is cut down to it"), Spec.Resolve(900.0f), 300.0f);
		TestEqual(TEXT("and a negative content size is floored"), Spec.Resolve(-10.0f), 0.0f);

		// A negative ceiling is not a negative size. Zero-sized is the honest answer; a negative one
		// would propagate through a panel's remaining-space arithmetic and hand the next child MORE
		// room than the panel has.
		const FDreamMeasureSpec Impossible = FDreamMeasureSpec::AtMost(-50.0f);
		TestEqual(TEXT("a negative ceiling is no room rather than negative room"), Impossible.Resolve(100.0f), 0.0f);
	}

	// Exactly is an instruction: the content is not consulted at all. This is the mode a stretched
	// child is measured with, and a version that quietly took the larger of the two would make a
	// stretch fail to shrink anything.
	{
		const FDreamMeasureSpec Spec = FDreamMeasureSpec::Exactly(300.0f);
		TestEqual(TEXT("smaller content is still the instructed size"), Spec.Resolve(50.0f), 300.0f);
		TestEqual(TEXT("larger content is still the instructed size"), Spec.Resolve(9000.0f), 300.0f);

		const FDreamMeasureSpec Negative = FDreamMeasureSpec::Exactly(-50.0f);
		TestEqual(TEXT("a negative instruction is floored at zero"), Negative.Resolve(100.0f), 0.0f);
	}

	// Equality is what a memo compares against to decide whether a cached measurement can be reused,
	// so the two things it forgives matter as much as the things it does not.
	{
		// Two Undefined specs are the same request even when their values differ, because the value
		// is not read. Comparing the number would miss cache hits for no reason.
		TestTrue(TEXT("two unconstrained specs are the same request whatever they carry"),
			FDreamMeasureSpec(300.0f, EDreamMeasureMode::Undefined) == FDreamMeasureSpec(0.0f, EDreamMeasureMode::Undefined));

		// The mode is compared first and is never forgiven: the same number under two modes is two
		// different questions, and answering the second with the first's cached answer is exactly
		// how a stretched child ends up wearing its natural size.
		TestFalse(TEXT("the same number under two modes is not the same request"),
			FDreamMeasureSpec::AtMost(300.0f) == FDreamMeasureSpec::Exactly(300.0f));

		// Within a constrained mode the value is compared with a tolerance, because these numbers
		// arrive from float arithmetic and a cache that misses on the last bit never hits.
		TestTrue(TEXT("a sub-thousandth difference is the same request"),
			FDreamMeasureSpec::AtMost(300.0f) == FDreamMeasureSpec::AtMost(300.0005f));
		TestFalse(TEXT("but a difference past the tolerance is not"),
			FDreamMeasureSpec::AtMost(300.0f) == FDreamMeasureSpec::AtMost(300.002f));

		TestTrue(TEXT("and the inequality operator agrees with all of it"),
			FDreamMeasureSpec::AtMost(300.0f) != FDreamMeasureSpec::Exactly(300.0f));
	}

	// The named constructors are the intended way in and must agree with the raw one, since half the
	// call sites use each.
	{
		TestTrue(TEXT("Undefined() builds an unconstrained spec"),
			FDreamMeasureSpec::Undefined() == FDreamMeasureSpec(0.0f, EDreamMeasureMode::Undefined));
		TestTrue(TEXT("AtMost() builds a ceiling"),
			FDreamMeasureSpec::AtMost(120.0f) == FDreamMeasureSpec(120.0f, EDreamMeasureMode::AtMost));
		TestTrue(TEXT("Exactly() builds an instruction"),
			FDreamMeasureSpec::Exactly(120.0f) == FDreamMeasureSpec(120.0f, EDreamMeasureMode::Exactly));

		// A default-constructed spec asks for nothing, which is what makes "no constraint" the
		// cost-free default for every caller that has not thought about it yet.
		const FDreamMeasureSpec Fresh;
		TestTrue(TEXT("a default-constructed spec is unconstrained"), Fresh == FDreamMeasureSpec::Undefined());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPlacementReuseTest,
	"DreamGUI.Placement.AReusedRecordIsReplacedRatherThanMergedWithTheLastOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPlacementReuseTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	// One record, two widgets, one after the other -- which is what a drag layer actually does. It
	// holds one placement and re-captures into it on every pick-up, so every field Capture does not
	// write is a field the PREVIOUS card left behind. Capture opens with Reset for exactly this
	// reason, and nothing else in the suite would notice if it stopped.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Hand = MakeWidget(TestWorld.World, Root, TEXT("Hand"), 400.0f, 200.0f);
	Hand->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamWidget* DragLayer = MakeWidget(TestWorld.World, Root, TEXT("DragLayer"), 800.0f, 600.0f);

	UDreamWidget* First = MakeWidget(TestWorld.World, Hand, TEXT("First"), 100.0f, 100.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Hand, TEXT("Second"), 100.0f, 100.0f);

	// Give the first card a slot nothing like a default, so anything of it that survives into the
	// second card's restore is unmistakable.
	UDreamPanelSlot* FirstSlot = First->GetPanelSlot();
	if (!TestNotNull(TEXT("the first card has a panel slot"), FirstSlot))
	{
		return false;
	}
	FirstSlot->SetPadding(FMargin(21.0f));
	FirstSlot->SetFillWeight(9.5f);
	FirstSlot->SetZOrder(41);
	First->SetPivot(FVector2D(0.1, 0.9));

	// The second card is left almost entirely default, so a leaked field shows up as a value it
	// never had rather than as a value that happens to match.
	UDreamPanelSlot* SecondSlot = Second->GetPanelSlot();
	if (!TestNotNull(TEXT("the second card has a panel slot"), SecondSlot))
	{
		return false;
	}
	SecondSlot->SetPadding(FMargin(0.0f));
	SecondSlot->SetFillWeight(1.0f);
	SecondSlot->SetZOrder(0);
	Second->SetPivot(FVector2D(0.5, 0.5));

	FDreamWidgetPlacement Placement;
	Placement.Capture(First);
	Placement.Capture(Second);
	TestTrue(TEXT("the second capture is a valid record"), Placement.IsValid());

	Second->TrySetParent(DragLayer, true);
	if (!TestTrue(TEXT("the second card can be put back"), Placement.Restore(Second)))
	{
		return false;
	}

	UDreamPanelSlot* Restored = Second->GetPanelSlot();
	if (!TestNotNull(TEXT("the second card has a slot again"), Restored))
	{
		return false;
	}
	TestEqual(TEXT("the padding is the second card's, not the first's"), Restored->Padding, FMargin(0.0f));
	TestEqual(TEXT("the fill weight is the second card's"), Restored->FillWeight, 1.0f);
	TestEqual(TEXT("the ZOrder is the second card's"), Restored->ZOrder, 0);
	TestEqual(TEXT("and so is the pivot, which lives outside the slot"), Second->GetPivot(), FVector2D(0.5, 0.5));

	// The first card is untouched by any of this; a record that had merged the two would be just as
	// happy to put the second card's parent onto the first.
	TestEqual(TEXT("the first card never moved"), First->GetParent(), Hand);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPlacementNoOpRestoreTest,
	"DreamGUI.Placement.RestoringAWidgetThatNeverLeftLeavesItExactlyWhereItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPlacementNoOpRestoreTest::RunTest(const FString& Parameters)
{
	using namespace DreamBindingPlacementTestLocal;
	FScopedGameWorld TestWorld;

	// A drag that is cancelled before the widget is lifted, or a zoom that is dismissed on the same
	// frame it opened, ends in a Restore against a widget that still has its original parent. That
	// path skips the reparent entirely and takes the "just fix the sibling index" branch instead, so
	// it is a different route through Restore than the one the round-trip test covers -- and it is
	// by far the more common one, because most drags are cancelled.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Hand = MakeWidget(TestWorld.World, Root, TEXT("Hand"), 400.0f, 200.0f);
	Hand->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();

	UDreamWidget* Leading = MakeWidget(TestWorld.World, Hand, TEXT("Leading"), 100.0f, 100.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Hand, TEXT("Card"), 100.0f, 100.0f);
	UDreamWidget* Trailing = MakeWidget(TestWorld.World, Hand, TEXT("Trailing"), 100.0f, 100.0f);

	UDreamPanelSlot* Slot = Card->GetPanelSlot();
	if (!TestNotNull(TEXT("the card has a panel slot"), Slot))
	{
		return false;
	}
	Slot->SetPadding(FMargin(2.0f, 4.0f, 6.0f, 8.0f));
	Slot->SetFillWeight(3.5f);
	Slot->SetZOrder(7);
	Card->SetPivot(FVector2D(0.2, 0.8));
	Card->SetIgnoreLayout(true);
	const int32 OriginalSiblingIndex = Card->GetSiblingIndex();

	FDreamWidgetPlacement Placement;
	Placement.Capture(Card);

	// No lift, no move. Restore anyway, the way a cancel path does without checking.
	if (!TestTrue(TEXT("restoring a widget that never moved still succeeds"), Placement.Restore(Card)))
	{
		return false;
	}

	TestEqual(TEXT("the parent is unchanged"), Card->GetParent(), Hand);
	TestEqual(TEXT("the sibling index is unchanged"), Card->GetSiblingIndex(), OriginalSiblingIndex);
	TestEqual(TEXT("its neighbours did not shuffle"), Leading->GetSiblingIndex(), 0);
	TestEqual(TEXT("...on either side"), Trailing->GetSiblingIndex(), 2);
	TestEqual(TEXT("the pivot is unchanged"), Card->GetPivot(), FVector2D(0.2, 0.8));
	TestTrue(TEXT("and so is IgnoreLayout"), Card->GetIgnoreLayout());

	// The slot is the same object it always was -- nothing was destroyed, so nothing was rebuilt --
	// and its authored values are still on it. Restore rewrites them all regardless, which is fine
	// as long as it rewrites the right ones.
	UDreamPanelSlot* After = Card->GetPanelSlot();
	TestEqual(TEXT("the slot is the same object, not a fresh one"), (void*)After, (void*)Slot);
	TestEqual(TEXT("the padding survived"), After->Padding, FMargin(2.0f, 4.0f, 6.0f, 8.0f));
	TestEqual(TEXT("the fill weight survived"), After->FillWeight, 3.5f);
	TestEqual(TEXT("the ZOrder survived"), After->ZOrder, 7);

	Root->DestroyWidget();
	return true;
}

#endif
