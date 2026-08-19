// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIBehaviour.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "PrefabEditor/LexWidgetDetailPropertyExtensionHandler.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// The details panel and the animation panel are Slate, and neither can be built headlessly, so each
// one's decision lives in a function taking the objects it decides about: the component clipboard,
// the property-row predicate, the sequencer's sub-prefab gate, and the animation binding paths.
//
// The three free functions below are defined in the panels' own .cpp files -- their headers describe
// Slate classes and are no place for them -- so this file declares the prototypes it needs. A
// signature that drifts apart from the definition is a link error, not a silent pass.
ULexUIBehaviour* LexUIWidgetComponentClipboard_Snapshot(ULexUIBehaviour* InSource);
ULexUIBehaviour* LexUIWidgetComponentClipboard_PasteOnto(ULexWidget* InTargetWidget, ULexUIBehaviour* InSource);
bool LexUIPrefabSequence_CanBindWidgetToSequencer(ULexUIPrefabHelperObject* InPrefabHelper, const ULexWidget* InWidget);

namespace LexPrefabPanelsTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A widget that can take children: a panel-less widget refuses attachment. */
	ULexWidget* MakeWidget(UObject* Outer, const TCHAR* DisplayName)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Outer, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		Widget->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
		return Widget;
	}

	/**
	 * Register InWidgets as one sub-prefab instance rooted at the first, as MakePrefabAsSubPrefab
	 * would. The asset has to be a real one: the helper drops any entry whose asset went away, and
	 * it does that sweep on the way into every sub-prefab query.
	 */
	void RegisterAsSubPrefab(ULexUIPrefabHelperObject* Helper, ULexUIPrefab* SubPrefabAsset, const TArray<ULexWidget*>& InWidgets)
	{
		FLexUISubPrefabData Data;
		Data.PrefabAsset = SubPrefabAsset;
		for (ULexWidget* Widget : InWidgets)
		{
			Data.MapGuidToObject.Add(FGuid::NewGuid(), Widget);
		}
		Helper->SubPrefabMap.Add(InWidgets[0], MoveTemp(Data));
	}
}

// P9. Cut has to leave something to paste, which is the whole reason the clipboard holds a
// stand-alone copy rather than a pointer to the component the user just deleted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexComponentClipboardSurvivesTheCutTest,
	"LGUI.Editor.ComponentClipboard.ACutComponentCanStillBePasted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexComponentClipboardSurvivesTheCutTest::RunTest(const FString& Parameters)
{
	using namespace LexPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	// GC follows UPROPERTY references, not outer chains, so a widget whose only tie to the world is
	// being outered to it can be collected in the middle of a test.
	TStrongObjectPtr<ULexWidget> Source(MakeWidget(TestWorld.World, TEXT("Source")));
	TStrongObjectPtr<ULexWidget> Target(MakeWidget(TestWorld.World, TEXT("Target")));

	UUIButton* Button = Source->AddComponent<UUIButton>();
	TestNotNull(TEXT("The source widget carries a button"), Button);
	if (Button == nullptr)
	{
		return false;
	}
	const FColor TunedColor(11, 22, 33, 44);
	Button->SetNormalColor(TunedColor);

	ULexUIBehaviour* Clipboard = LexUIWidgetComponentClipboard_Snapshot(Button);
	TestNotNull(TEXT("Copying produced a clipboard component"), Clipboard);
	if (Clipboard == nullptr)
	{
		return false;
	}
	TStrongObjectPtr<ULexUIBehaviour> ClipboardGuard(Clipboard);

	// The rest of Cut: the component the copy came from is gone before the paste happens.
	Source->RemoveComponent(Button);
	TestEqual(TEXT("The cut emptied the source widget"), Source->GetAllComponents().Num(), 0);

	ULexUIBehaviour* Pasted = LexUIWidgetComponentClipboard_PasteOnto(Target.Get(), Clipboard);
	TestNotNull(TEXT("Pasting produced a component"), Pasted);
	if (Pasted == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("...of the class that was copied"), Pasted->GetClass() == UUIButton::StaticClass());
	TestEqual(TEXT("...carrying the value that was tuned on the original"),
		CastChecked<UUIButton>(Pasted)->GetNormalColor(), TunedColor);
	TestTrue(TEXT("...and it landed on the widget that was pasted into"), Pasted->GetWidget() == Target.Get());
	return true;
}

// P9. A behaviour caches the widget it was registered against, and GetWidget() trusts that cache
// ahead of its own outer -- so a wholesale property copy hands the new component the old widget,
// which it then reports and listens to for the rest of its life. Duplicate is the path that hits
// this, because there the source is a live component rather than a detached clipboard copy.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexComponentPasteDoesNotInheritSourceWidgetTest,
	"LGUI.Editor.ComponentClipboard.APastedComponentBelongsToTheWidgetItWasPastedOnto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexComponentPasteDoesNotInheritSourceWidgetTest::RunTest(const FString& Parameters)
{
	using namespace LexPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexWidget> Source(MakeWidget(TestWorld.World, TEXT("Source")));
	TStrongObjectPtr<ULexWidget> Target(MakeWidget(TestWorld.World, TEXT("Target")));

	UUIButton* Button = Source->AddComponent<UUIButton>();
	TestNotNull(TEXT("The source widget carries a button"), Button);
	if (Button == nullptr)
	{
		return false;
	}
	// Populate the cache the copy would otherwise carry across.
	TestTrue(TEXT("...whose widget is the source"), Button->GetWidget() == Source.Get());

	ULexUIBehaviour* Pasted = LexUIWidgetComponentClipboard_PasteOnto(Target.Get(), Button);
	TestNotNull(TEXT("Pasting produced a component"), Pasted);
	if (Pasted == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("The copy reports the widget it was pasted onto"), Pasted->GetWidget() == Target.Get());
	TestTrue(TEXT("The original stayed where it was"), Button->GetWidget() == Source.Get());
	TestEqual(TEXT("...and is still the source widget's only component"), Source->GetAllComponents().Num(), 1);
	return true;
}

// P18. The property editor asks about every row in the panel; only the rows the hierarchy picker can
// actually fill should be given an extension slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetReferencePropertyPredicateTest,
	"LGUI.Editor.DetailsExtension.OnlyPickableWidgetReferenceRowsAreExtended",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetReferencePropertyPredicateTest::RunTest(const FString& Parameters)
{
	const FProperty* ParentProperty = FindFProperty<FProperty>(ULexWidget::StaticClass(), TEXT("Parent"));
	const FProperty* VisualProperty = FindFProperty<FProperty>(ULexWidget::StaticClass(), TEXT("Visual"));
	const FProperty* DisplayNameProperty = FindFProperty<FProperty>(ULexWidget::StaticClass(), ULexWidget::GetPropertyName_DisplayName());
	TestNotNull(TEXT("ULexWidget still has a Parent property"), ParentProperty);
	TestNotNull(TEXT("ULexWidget still has a Visual property"), VisualProperty);
	TestNotNull(TEXT("ULexWidget still has a DisplayName property"), DisplayNameProperty);
	if (ParentProperty == nullptr || VisualProperty == nullptr || DisplayNameProperty == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("A reference to another widget is pickable"),
		FLexWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(ParentProperty));
	// An instanced sub-object is owned by the property, so there is nothing in the hierarchy to point
	// it at -- and this is the row whose stock value widget the picker used to replace.
	TestFalse(TEXT("An instanced sub-object is not pickable"),
		FLexWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(VisualProperty));
	TestFalse(TEXT("A property that is not an object reference at all is not pickable"),
		FLexWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(DisplayNameProperty));
	TestFalse(TEXT("Nothing is not pickable"),
		FLexWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(nullptr));
	return true;
}

// M13. A binding's only persistent half is the direct widget pointer, and a widget belonging to a
// sub-prefab is not serialized into this prefab, so the pointer is dropped on save and the track
// comes back bound to nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexSequencerSubPrefabBindingGateTest,
	"LGUI.Editor.PrefabAnimation.SubPrefabWidgetsCannotBeBoundToSequencer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexSequencerSubPrefabBindingGateTest::RunTest(const FString& Parameters)
{
	using namespace LexPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexWidget> Root(MakeWidget(TestWorld.World, TEXT("Root")));
	ULexWidget* OwnWidget = MakeWidget(Root.Get(), TEXT("PlayButton"));
	ULexWidget* NestedRoot = MakeWidget(Root.Get(), TEXT("HealthBar"));
	ULexWidget* NestedChild = MakeWidget(Root.Get(), TEXT("Fill"));
	OwnWidget->TrySetParent(Root.Get(), false);
	NestedRoot->TrySetParent(Root.Get(), false);
	NestedChild->TrySetParent(NestedRoot, false);

	TStrongObjectPtr<ULexUIPrefabHelperObject> Helper(NewObject<ULexUIPrefabHelperObject>());
	TStrongObjectPtr<ULexUIPrefab> SubPrefabAsset(NewObject<ULexUIPrefab>());

	// Control: with nothing registered as a sub-prefab, every widget in the tree is bindable, or the
	// menu would refuse every prefab that has no sub-prefab at all.
	TestTrue(TEXT("A widget this prefab owns is bindable"),
		LexUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), NestedRoot));

	RegisterAsSubPrefab(Helper.Get(), SubPrefabAsset.Get(), { NestedRoot, NestedChild });

	TestTrue(TEXT("The prefab's own widget stays bindable"),
		LexUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), OwnWidget));
	TestFalse(TEXT("The sub-prefab root is refused"),
		LexUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), NestedRoot));
	// Gating on "is a sub-prefab root" would still offer everything inside one.
	TestFalse(TEXT("A widget inside the sub-prefab is refused too"),
		LexUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), NestedChild));
	// Outside a prefab editor there is no helper and so no sub-prefab to be part of.
	TestTrue(TEXT("With no prefab helper at all, binding is allowed"),
		LexUIPrefabSequence_CanBindWidgetToSequencer(nullptr, NestedChild));
	TestFalse(TEXT("Nothing is never bindable"),
		LexUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), nullptr));

	// GetPrefabHelperObject_WhichManageThisWidget walks every live helper, so a fixture left holding
	// widgets would answer for tests that come after this one.
	Helper->ClearLoadedPrefab();
	return true;
}

// M15. The editor-only widget path is spelled out of display names and is the only thing "Try fix
// object reference" can match on once the direct pointer stops resolving -- so it has to be
// re-derived while that pointer still works. A rename is exactly the case where it goes stale and
// the repair path is exactly the one that then cannot help.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAnimationHelperPathFollowsRenamesTest,
	"LGUI.Editor.PrefabAnimation.BindingHelperPathsAreRederivedAfterARename",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAnimationHelperPathFollowsRenamesTest::RunTest(const FString& Parameters)
{
	using namespace LexPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexWidget> Root(MakeWidget(TestWorld.World, TEXT("Root")));
	ULexWidget* Target = MakeWidget(Root.Get(), TEXT("PlayButton"));
	TestTrue(TEXT("The target widget attached to the root"), Target->TrySetParent(Root.Get(), false));

	ULexUIPrefabSequenceComponent* AnimationHost = Root->AddComponent<ULexUIPrefabSequenceComponent>();
	TestNotNull(TEXT("The root carries an animation host"), AnimationHost);
	if (AnimationHost == nullptr)
	{
		return false;
	}
	ULexUIPrefabSequence* Sequence = AnimationHost->AddNewAnimation();
	TestNotNull(TEXT("The host has an animation"), Sequence);
	if (Sequence == nullptr)
	{
		return false;
	}
	Sequence->BindPossessableObject(FGuid::NewGuid(), *Target, Root.Get());

	// Sequencer records the binding against the possessed widget, so the stored path is only ever
	// correct relative to the host once something re-derives it.
	AnimationHost->FixEditorHelpers();
	TestTrue(TEXT("The binding's helper path matches the hierarchy"), Sequence->IsEditorHelpersGood(Root.Get()));

	Target->SetDisplayName(TEXT("ConfirmButton"));
	TestFalse(TEXT("Renaming the widget leaves the stored path pointing at a name that is gone"),
		Sequence->IsEditorHelpersGood(Root.Get()));

	AnimationHost->FixEditorHelpers();
	TestTrue(TEXT("...and re-deriving it while the binding still resolves puts the new name back"),
		Sequence->IsEditorHelpersGood(Root.Get()));
	TestTrue(TEXT("The binding itself was never broken by any of this"),
		Sequence->IsObjectReferencesGood(Root.Get()));
	return true;
}

#endif
