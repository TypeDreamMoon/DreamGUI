// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/DreamWidgetBehaviourTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamUIEditorTools.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// The delete warning has two halves and only one of them was pinned. Which bindings a delete breaks
// is covered next door in DreamEditorToolsAutomationTests, on a fixture that belongs to no prefab; the
// half covered here is everything that only happens when a prefab helper DOES claim the widgets --
// finding the companion at all, and which of its properties count as a binding worth stopping for.
namespace DreamToolsAuditTestLocal
{
	/** A root carrying the companion behaviour: the state a real delete warning runs against. */
	struct FManagedPrefabFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UDreamWidget> Root;
		TStrongObjectPtr<UDreamUIAutoBindTestBehaviour> Companion;
		TArray<TStrongObjectPtr<UDreamWidget>> KeepAlive;

		FManagedPrefabFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false);
			Root.Reset(MakeWidget(TEXT("Root")));
			Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
			Companion.Reset(Root->AddComponent<UDreamUIAutoBindTestBehaviour>());
		}
		~FManagedPrefabFixture()
		{
			KeepAlive.Empty();
			Companion.Reset();
			Root.Reset();
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
