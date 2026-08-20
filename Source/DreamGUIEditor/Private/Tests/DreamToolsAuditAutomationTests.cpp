// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/DreamUIPrefabBehaviourTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamUIEditorTools.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// The delete warning has two halves and only one of them was pinned. Which bindings a delete breaks
// is covered next door in DreamEditorToolsAutomationTests, on a fixture that belongs to no prefab; the
// half covered here is everything that only happens when a prefab helper DOES claim the widgets --
// finding the companion at all, and which of its properties count as a binding worth stopping for.
namespace DreamToolsAuditTestLocal
{
	/** A prefab, its helper and a root carrying the companion: the state a real delete runs against. */
	struct FManagedPrefabFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UDreamUIPrefab> Prefab;
		TStrongObjectPtr<UDreamUIPrefabHelperObject> Helper;
		TStrongObjectPtr<UDreamWidget> Root;
		TStrongObjectPtr<UDreamUIAutoBindTestBehaviour> Companion;
		TArray<TStrongObjectPtr<UDreamWidget>> KeepAlive;

		FManagedPrefabFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false);
			Prefab.Reset(NewObject<UDreamUIPrefab>());
			// The explicit behaviour class is what lets a NATIVE companion take part; without it the
			// lookup falls back to the BP_<PrefabName> blueprint convention, which needs an asset.
			Prefab->SetBehaviourClass(UDreamUIAutoBindTestBehaviour::StaticClass());
			Root.Reset(MakeWidget(TEXT("Root")));
			Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
			Companion.Reset(Root->AddComponent<UDreamUIAutoBindTestBehaviour>());
			Helper.Reset(NewObject<UDreamUIPrefabHelperObject>(World));
			Helper->PrefabAsset = Prefab.Get();
			Helper->LoadedRootWidget = Root.Get();
		}
		~FManagedPrefabFixture()
		{
			KeepAlive.Empty();
			Companion.Reset();
			// The helper is what claims these widgets, and it answers a global iteration -- drop it
			// before the world so a later test cannot find this one still holding a dead root.
			Helper->LoadedRootWidget = nullptr;
			Helper.Reset();
			Root.Reset();
			Prefab.Reset();
			if (World != nullptr)
			{
				World->DestroyWorld(false);
			}
		}

		UDreamWidget* MakeWidget(const TCHAR* DisplayName)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(DisplayName);
			KeepAlive.Add(TStrongObjectPtr<UDreamWidget>(Widget));
			return Widget;
		}
		UDreamWidget* AddChild(const TCHAR* DisplayName)
		{
			UDreamWidget* Child = MakeWidget(DisplayName);
			Child->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
			Child->TrySetParent(Root.Get(), false);
			return Child;
		}
	};

	bool Reports(const TArray<FText>& Bindings, const TCHAR* Needle)
	{
		return Bindings.ContainsByPredicate([Needle](const FText& Binding) { return Binding.ToString().Contains(Needle); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteFindsCompanionThroughHelperTest,
	"DreamGUI.Editor.Delete.CompanionIsFoundThroughThePrefabHelper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteFindsCompanionThroughHelperTest::RunTest(const FString& Parameters)
{
	using namespace DreamToolsAuditTestLocal;
	FManagedPrefabFixture Fixture;
	UDreamWidget* PlayButton = Fixture.AddChild(TEXT("PlayButton"));
	if (!TestEqual(TEXT("the child attached"), Fixture.Root->GetChildren().Num(), 1))return false;

	// Widget -> its helper -> the helper's prefab -> that prefab's companion. Every delete warning
	// starts here, and answering null means the whole check silently passes on any real selection.
	TestTrue(TEXT("a widget the helper claims resolves to the prefab's companion"),
		FDreamUIEditorTools::FindCompanionForWidgets({ PlayButton }) == Fixture.Companion.Get());
	TestTrue(TEXT("and asking about the root resolves to the same one"),
		FDreamUIEditorTools::FindCompanionForWidgets({ Fixture.Root.Get() }) == Fixture.Companion.Get());

	// A widget no helper claims has no prefab, so there is nobody to warn about.
	UDreamWidget* Unmanaged = Fixture.MakeWidget(TEXT("Loose"));
	TestNull(TEXT("a widget belonging to no prefab has no companion"),
		FDreamUIEditorTools::FindCompanionForWidgets({ Unmanaged }));
	TestNull(TEXT("and an empty selection has none either"),
		FDreamUIEditorTools::FindCompanionForWidgets({}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteReportsVisualBindingTest,
	"DreamGUI.Editor.Delete.BindingToAVisualCountsToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteReportsVisualBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamToolsAuditTestLocal;
	FManagedPrefabFixture Fixture;
	UDreamWidget* Banner = Fixture.AddChild(TEXT("Banner"));
	UDreamImage* Image = Banner->CreateNewVisual<UDreamImage>();
	if (!TestNotNull(TEXT("the visual attached"), (UObject*)Image))return false;

	// A visual is not a behaviour -- it hangs off the widget's own Visual field and never appears in
	// GetAllComponents -- so it needs a key of its own, and the widget-keyed and behaviour-keyed
	// entries cannot stand in for it. The property here is the UObject-typed one because it is the
	// only fixture property a visual can be assigned to; what the collector filters on is the
	// property's flags, not its declared class.
	Fixture.Companion->Unrelated = Image;
	const TArray<FText> Bindings = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Banner });
	TestTrue(TEXT("a binding to a widget's visual is reported against its widget"), Reports(Bindings, TEXT("Unrelated -> Banner")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteIgnoresNotInstanceEditableBindingTest,
	"DreamGUI.Editor.Delete.NotInstanceEditableReferencesAreNotWorthWarningAbout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteIgnoresNotInstanceEditableBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamToolsAuditTestLocal;
	FManagedPrefabFixture Fixture;
	UDreamWidget* Child = Fixture.AddChild(TEXT("NotInstanceEditable"));
	Fixture.Companion->NotInstanceEditable = Child;

	// The prefab writer skips CPF_DisableEditOnInstance exactly as it skips transient, so this
	// reference already comes back empty after a save: the delete is not what breaks it, and naming
	// it in the dialog is the same false alarm the transient case was.
	const TArray<FText> Bindings = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Child });
	TestEqual(TEXT("a reference the writer drops raises no warning"), Bindings.Num(), 0);

	// The savable neighbour on the same companion is the control: the filter must be dropping this
	// one property, not every property.
	UDreamWidget* PlayButton = Fixture.AddChild(TEXT("PlayButton"));
	Fixture.Companion->PlayButton = PlayButton;
	const TArray<FText> Savable = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { PlayButton });
	TestEqual(TEXT("while a savable binding to the same kind of widget still warns"), Savable.Num(), 1);
	return true;
}

#endif
