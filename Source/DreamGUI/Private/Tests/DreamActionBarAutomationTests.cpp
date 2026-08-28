// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUIActionBar.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Tests/DreamNavigationTestTypes.h"
#include "Tests/DreamTextTestFont.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

/*
 * The row of key prompts. It reads the router rather than being told what to show, which is the whole
 * point: a hand-authored hint bar goes stale the first time a screen changes a binding and nobody
 * remembers to edit the hint. What is left to get wrong is when it refreshes, and which of the glyph
 * and the key name it shows.
 */

namespace DreamActionBarTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		Widget->OnRegister();
		return Widget;
	}

	UDataTable* MakeActionTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		FDreamUIInputActionData Row;
		Row.DisplayName = FText::FromString(TEXT("Confirm"));
		Row.KeyboardKey = EKeys::Enter;
		Row.GamepadKey = EKeys::Gamepad_FaceButton_Bottom;
		Table->AddRow(TEXT("Confirm"), Row);
		return Table;
	}

	FDataTableRowHandle MakeHandle(UDataTable* Table, FName RowName)
	{
		FDataTableRowHandle Handle;
		Handle.DataTable = Table;
		Handle.RowName = RowName;
		return Handle;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionBarRefreshTest,
	"DreamGUI.Navigation.ActionBar.RefreshesWhenTheAnswerChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionBarRefreshTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDreamWidget* BarWidget = MakeWidget(TestWorld.World, nullptr);
	UDreamCountingActionBar* Bar = BarWidget->AddComponent<UDreamCountingActionBar>();
	Bar->ForceEnable();
	const int32 AfterEnable = Bar->RebuildCount;
	TestTrue(TEXT("Coming up builds once"), AfterEnable >= 1);
	// No entry prefab, so there is nothing it could have built -- and asking it to must not crash or
	// leave phantom entries behind.
	TestEqual(TEXT("With no entry prefab it shows nothing"), Bar->GetEntryWidgets().Num(), 0);

	UDataTable* Table = MakeActionTable();
	UDreamActionCallCounter* Counter = NewObject<UDreamActionCallCounter>();
	FDreamUIActionExecutedDelegate Delegate;
	Delegate.BindUFunction(Counter, TEXT("Fire"));

	const FDreamUIActionHandle Handle = Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Confirm")), Delegate);
	TestTrue(TEXT("A new binding refreshes the bar"), Bar->RebuildCount > AfterEnable);

	const int32 AfterRegister = Bar->RebuildCount;
	// Another player's bindings are none of this bar's business, and refreshing on them would reload a
	// prefab per entry for nothing.
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Confirm")), Delegate, 1);
	TestEqual(TEXT("Another player's binding does not"), Bar->RebuildCount, AfterRegister);

	Router->UnregisterAction(Handle);
	TestTrue(TEXT("Taking a binding away refreshes it too"), Bar->RebuildCount > AfterRegister);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionBarEntryTest,
	"DreamGUI.Navigation.ActionBar.AnEntryShowsTheGlyphOrTheKeyName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionBarEntryTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr);
	UDreamWidget* LabelWidget = MakeWidget(TestWorld.World, Root);
	UDreamWidget* KeyWidget = MakeWidget(TestWorld.World, Root);
	UDreamWidget* IconWidget = MakeWidget(TestWorld.World, Root);

	UDreamText* Label = LabelWidget->CreateNewVisual<UDreamText>();
	Label->SetFont(Font);
	UDreamText* KeyLabel = KeyWidget->CreateNewVisual<UDreamText>();
	KeyLabel->SetFont(Font);
	UDreamImage* Icon = IconWidget->CreateNewVisual<UDreamImage>();

	UDreamUIActionBarEntry* Entry = Root->AddComponent<UDreamUIActionBarEntry>();
	Entry->SetLabelText(Label);
	Entry->SetKeyText(KeyLabel);
	Entry->SetIconImage(Icon);

	// No glyph authored: the key's own name is all the player has to go on, so it must be shown.
	FDreamUIActionBinding Binding;
	Binding.DisplayName = FText::FromString(TEXT("Confirm"));
	Binding.Key = EKeys::Enter;
	Entry->SetBinding(Binding);

	TestEqual(TEXT("The label says what the action does"), Label->GetText().ToString(), FString(TEXT("Confirm")));
	TestEqual(TEXT("The key name is shown"), KeyLabel->GetText().ToString(), EKeys::Enter.GetDisplayName().ToString());
	TestEqual(TEXT("...and is visible"), KeyWidget->GetVisibility(), EDreamWidgetVisibility::Visible);
	TestEqual(TEXT("...while the empty glyph is not"), IconWidget->GetVisibility(), EDreamWidgetVisibility::Collapsed);

	// With a glyph the two swap. Printing the key's name beside its own picture says it twice.
	Binding.Icon = NewObject<UTexture2D>(TestWorld.World);
	Entry->SetBinding(Binding);
	TestEqual(TEXT("The glyph is shown"), IconWidget->GetVisibility(), EDreamWidgetVisibility::Visible);
	TestEqual(TEXT("...and the key name is not"), KeyWidget->GetVisibility(), EDreamWidgetVisibility::Collapsed);

	Root->DestroyWidget();
	return true;
}

#endif
