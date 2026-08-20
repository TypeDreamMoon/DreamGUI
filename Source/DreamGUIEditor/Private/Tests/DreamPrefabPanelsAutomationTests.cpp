// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "PrefabEditor/DreamWidgetDetailPropertyExtensionHandler.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// The details panel and the animation panel are Slate, and neither can be built headlessly, so each
// one's decision lives in a function taking the objects it decides about: the component clipboard,
// the property-row predicate, the sequencer's sub-prefab gate, and the animation binding paths.
//
// The three free functions below are defined in the panels' own .cpp files -- their headers describe
// Slate classes and are no place for them -- so this file declares the prototypes it needs. A
// signature that drifts apart from the definition is a link error, not a silent pass.
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_Snapshot(UDreamUIBehaviour* InSource);
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_PasteOnto(UDreamWidget* InTargetWidget, UDreamUIBehaviour* InSource);
bool DreamUIPrefabSequence_CanBindWidgetToSequencer(UDreamUIPrefabHelperObject* InPrefabHelper, const UDreamWidget* InWidget);

namespace DreamPrefabPanelsTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A widget that can take children: a panel-less widget refuses attachment. */
	UDreamWidget* MakeWidget(UObject* Outer, const TCHAR* DisplayName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		Widget->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
		return Widget;
	}

	/**
	 * Register InWidgets as one sub-prefab instance rooted at the first, as MakePrefabAsSubPrefab
	 * would. The asset has to be a real one: the helper drops any entry whose asset went away, and
	 * it does that sweep on the way into every sub-prefab query.
	 */
	void RegisterAsSubPrefab(UDreamUIPrefabHelperObject* Helper, UDreamUIPrefab* SubPrefabAsset, const TArray<UDreamWidget*>& InWidgets)
	{
		FDreamUISubPrefabData Data;
		Data.PrefabAsset = SubPrefabAsset;
		for (UDreamWidget* Widget : InWidgets)
		{
			Data.MapGuidToObject.Add(FGuid::NewGuid(), Widget);
		}
		Helper->SubPrefabMap.Add(InWidgets[0], MoveTemp(Data));
	}
}

// P9. Cut has to leave something to paste, which is the whole reason the clipboard holds a
// stand-alone copy rather than a pointer to the component the user just deleted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamComponentClipboardSurvivesTheCutTest,
	"DreamGUI.Editor.ComponentClipboard.ACutComponentCanStillBePasted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamComponentClipboardSurvivesTheCutTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	// GC follows UPROPERTY references, not outer chains, so a widget whose only tie to the world is
	// being outered to it can be collected in the middle of a test.
	TStrongObjectPtr<UDreamWidget> Source(MakeWidget(TestWorld.World, TEXT("Source")));
	TStrongObjectPtr<UDreamWidget> Target(MakeWidget(TestWorld.World, TEXT("Target")));

	UUIButton* Button = Source->AddComponent<UUIButton>();
	TestNotNull(TEXT("The source widget carries a button"), Button);
	if (Button == nullptr)
	{
		return false;
	}
	const FColor TunedColor(11, 22, 33, 44);
	Button->SetNormalColor(TunedColor);

	UDreamUIBehaviour* Clipboard = DreamUIWidgetComponentClipboard_Snapshot(Button);
	TestNotNull(TEXT("Copying produced a clipboard component"), Clipboard);
	if (Clipboard == nullptr)
	{
		return false;
	}
	TStrongObjectPtr<UDreamUIBehaviour> ClipboardGuard(Clipboard);

	// The rest of Cut: the component the copy came from is gone before the paste happens.
	Source->RemoveComponent(Button);
	TestEqual(TEXT("The cut emptied the source widget"), Source->GetAllComponents().Num(), 0);

	UDreamUIBehaviour* Pasted = DreamUIWidgetComponentClipboard_PasteOnto(Target.Get(), Clipboard);
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
	FDreamComponentPasteDoesNotInheritSourceWidgetTest,
	"DreamGUI.Editor.ComponentClipboard.APastedComponentBelongsToTheWidgetItWasPastedOnto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamComponentPasteDoesNotInheritSourceWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Source(MakeWidget(TestWorld.World, TEXT("Source")));
	TStrongObjectPtr<UDreamWidget> Target(MakeWidget(TestWorld.World, TEXT("Target")));

	UUIButton* Button = Source->AddComponent<UUIButton>();
	TestNotNull(TEXT("The source widget carries a button"), Button);
	if (Button == nullptr)
	{
		return false;
	}
	// Populate the cache the copy would otherwise carry across.
	TestTrue(TEXT("...whose widget is the source"), Button->GetWidget() == Source.Get());

	UDreamUIBehaviour* Pasted = DreamUIWidgetComponentClipboard_PasteOnto(Target.Get(), Button);
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
	FDreamWidgetReferencePropertyPredicateTest,
	"DreamGUI.Editor.DetailsExtension.OnlyPickableWidgetReferenceRowsAreExtended",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetReferencePropertyPredicateTest::RunTest(const FString& Parameters)
{
	const FProperty* ParentProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), TEXT("Parent"));
	const FProperty* VisualProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), TEXT("Visual"));
	const FProperty* DisplayNameProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), UDreamWidget::GetPropertyName_DisplayName());
	TestNotNull(TEXT("UDreamWidget still has a Parent property"), ParentProperty);
	TestNotNull(TEXT("UDreamWidget still has a Visual property"), VisualProperty);
	TestNotNull(TEXT("UDreamWidget still has a DisplayName property"), DisplayNameProperty);
	if (ParentProperty == nullptr || VisualProperty == nullptr || DisplayNameProperty == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("A reference to another widget is pickable"),
		FDreamWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(ParentProperty));
	// An instanced sub-object is owned by the property, so there is nothing in the hierarchy to point
	// it at -- and this is the row whose stock value widget the picker used to replace.
	TestFalse(TEXT("An instanced sub-object is not pickable"),
		FDreamWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(VisualProperty));
	TestFalse(TEXT("A property that is not an object reference at all is not pickable"),
		FDreamWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(DisplayNameProperty));
	TestFalse(TEXT("Nothing is not pickable"),
		FDreamWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(nullptr));
	return true;
}

// M13. A binding's only persistent half is the direct widget pointer, and a widget belonging to a
// sub-prefab is not serialized into this prefab, so the pointer is dropped on save and the track
// comes back bound to nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequencerSubPrefabBindingGateTest,
	"DreamGUI.Editor.PrefabAnimation.SubPrefabWidgetsCannotBeBoundToSequencer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequencerSubPrefabBindingGateTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, TEXT("Root")));
	UDreamWidget* OwnWidget = MakeWidget(Root.Get(), TEXT("PlayButton"));
	UDreamWidget* NestedRoot = MakeWidget(Root.Get(), TEXT("HealthBar"));
	UDreamWidget* NestedChild = MakeWidget(Root.Get(), TEXT("Fill"));
	OwnWidget->TrySetParent(Root.Get(), false);
	NestedRoot->TrySetParent(Root.Get(), false);
	NestedChild->TrySetParent(NestedRoot, false);

	TStrongObjectPtr<UDreamUIPrefabHelperObject> Helper(NewObject<UDreamUIPrefabHelperObject>());
	TStrongObjectPtr<UDreamUIPrefab> SubPrefabAsset(NewObject<UDreamUIPrefab>());

	// Control: with nothing registered as a sub-prefab, every widget in the tree is bindable, or the
	// menu would refuse every prefab that has no sub-prefab at all.
	TestTrue(TEXT("A widget this prefab owns is bindable"),
		DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), NestedRoot));

	RegisterAsSubPrefab(Helper.Get(), SubPrefabAsset.Get(), { NestedRoot, NestedChild });

	TestTrue(TEXT("The prefab's own widget stays bindable"),
		DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), OwnWidget));
	TestFalse(TEXT("The sub-prefab root is refused"),
		DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), NestedRoot));
	// Gating on "is a sub-prefab root" would still offer everything inside one.
	TestFalse(TEXT("A widget inside the sub-prefab is refused too"),
		DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), NestedChild));
	// Outside a prefab editor there is no helper and so no sub-prefab to be part of.
	TestTrue(TEXT("With no prefab helper at all, binding is allowed"),
		DreamUIPrefabSequence_CanBindWidgetToSequencer(nullptr, NestedChild));
	TestFalse(TEXT("Nothing is never bindable"),
		DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper.Get(), nullptr));

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
	FDreamAnimationHelperPathFollowsRenamesTest,
	"DreamGUI.Editor.PrefabAnimation.BindingHelperPathsAreRederivedAfterARename",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnimationHelperPathFollowsRenamesTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabPanelsTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, TEXT("Root")));
	UDreamWidget* Target = MakeWidget(Root.Get(), TEXT("PlayButton"));
	TestTrue(TEXT("The target widget attached to the root"), Target->TrySetParent(Root.Get(), false));

	UDreamUIPrefabSequenceComponent* AnimationHost = Root->AddComponent<UDreamUIPrefabSequenceComponent>();
	TestNotNull(TEXT("The root carries an animation host"), AnimationHost);
	if (AnimationHost == nullptr)
	{
		return false;
	}
	UDreamUIPrefabSequence* Sequence = AnimationHost->AddNewAnimation();
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
