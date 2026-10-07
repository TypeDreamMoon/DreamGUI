// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamEventBindingTestTypes.h"
#include "DreamOnDiskFixture.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/SDreamWidgetComponentEditor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DreamAuthoredEventComponentEditingTestLocal
{
	const FArrayProperty* EventListProperty()
	{
		const FArrayProperty* Property = FindFProperty<FArrayProperty>(FDreamUIEventDelegate::StaticStruct(), TEXT("EventList"));
		check(Property != nullptr);
		return Property;
	}

	int32 BindingCount(FDreamUIEventDelegate& InEvent)
	{
		return FScriptArrayHelper(EventListProperty(), EventListProperty()->ContainerPtrToValuePtr<void>(&InEvent)).Num();
	}

	FDreamUIEventDelegateData* Binding(FDreamUIEventDelegate& InEvent)
	{
		FScriptArrayHelper List(EventListProperty(), EventListProperty()->ContainerPtrToValuePtr<void>(&InEvent));
		return List.Num() == 1 ? static_cast<FDreamUIEventDelegateData*>(static_cast<void*>(List.GetRawPtr(0))) : nullptr;
	}

	int32 BindingIndex(FDreamUIEventDelegate& InEvent)
	{
		const FIntProperty* Property = FindFProperty<FIntProperty>(FDreamUIEventDelegateData::StaticStruct(), TEXT("HelperComponentIndex"));
		check(Property != nullptr);
		FDreamUIEventDelegateData* Entry = Binding(InEvent);
		return Entry != nullptr ? Property->GetPropertyValue_InContainer(Entry) : INDEX_NONE;
	}

	void ClearCache(FDreamUIEventDelegate& InEvent)
	{
		if (FDreamUIEventDelegateData* Entry = Binding(InEvent))
		{
			for (const TCHAR* Name : { TEXT("TargetObject"), TEXT("CacheFunction") })
			{
				const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(FDreamUIEventDelegateData::StaticStruct(), Name);
				check(Property != nullptr);
				Property->SetObjectPropertyValue_InContainer(Entry, nullptr);
			}
		}
	}

	UObject* Resolve(FDreamUIEventDelegate& InEvent)
	{
		ClearCache(InEvent);
		FString Error;
		FDreamUIEventDelegateData* Entry = Binding(InEvent);
		return Entry != nullptr ? Entry->ResolveTargetForValidation(Error) : nullptr;
	}

	void MakeLegacy(FDreamUIEventDelegate& InEvent)
	{
		const FIntProperty* Property = FindFProperty<FIntProperty>(FDreamUIEventDelegateData::StaticStruct(), TEXT("HelperComponentIndex"));
		check(Property != nullptr && Binding(InEvent) != nullptr);
		Property->SetPropertyValue_InContainer(Binding(InEvent), INDEX_NONE);
		ClearCache(InEvent);
	}

	struct FSample
	{
		UDreamWidget* ReceiverWidget = nullptr;
		UDreamEventBindingTestBehaviour* Sender = nullptr;
		UDreamEventBindingTestBehaviour* First = nullptr;
		UDreamEventBindingTestBehaviour* Second = nullptr;
		UDreamEventBindingTestBehaviour* Third = nullptr;
	};

	FSample BuildSample(UDreamWidget* InRoot)
	{
		auto AddChild = [InRoot](const TCHAR* InName)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(InRoot->GetOuter(), NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(InName);
			Widget->SetParentBeforeRegister(InRoot);
			return Widget;
		};
		FSample Sample;
		Sample.Sender = AddChild(TEXT("Sender"))->AddComponent<UDreamEventBindingTestBehaviour>();
		Sample.ReceiverWidget = AddChild(TEXT("Receiver"));
		Sample.First = Sample.ReceiverWidget->AddComponent<UDreamEventBindingTestBehaviour>();
		Sample.Second = Sample.ReceiverWidget->AddComponent<UDreamEventBindingTestBehaviour>();
		Sample.Third = Sample.ReceiverWidget->AddComponent<UDreamEventBindingTestBehaviour>();
		auto BindTo = [&Sample](FDreamUIEventDelegate& Event, UDreamEventBindingTestBehaviour* Target)
		{
			Event.AddFunctionBinding(Sample.ReceiverWidget, Target, TEXT("Touch"), EDreamUIEventDelegateParameterType::Empty, false);
			ClearCache(Event);
		};
		BindTo(Sample.Sender->AuthoredEvent, Sample.Second);
		Sample.Sender->NestedAuthoredEvents.SetNum(2);
		BindTo(Sample.Sender->NestedAuthoredEvents[0], Sample.Third);
		BindTo(Sample.Sender->NestedAuthoredEvents[1], Sample.First);
		MakeLegacy(Sample.Sender->NestedAuthoredEvents[1]);
		return Sample;
	}

	bool HasEditor(FAutomationTestBase& InTest)
	{
		return InTest.TestTrue(TEXT("the editor has a transaction buffer"), GEditor != nullptr && GEditor->Trans != nullptr);
	}

	bool TestMovedTargets(FAutomationTestBase& InTest, FSample& InSample)
	{
		if (!InTest.TestEqual(TEXT("the restored receiver has all three components"), InSample.ReceiverWidget->GetAllComponents().Num(), 3)) return false;
		InTest.TestEqual(TEXT("the second component is now first"), InSample.ReceiverWidget->GetAllComponents()[0],
			static_cast<UDreamUIBehaviour*>(InSample.Second));
		InTest.TestEqual(TEXT("the authored route followed its original component"), BindingIndex(InSample.Sender->AuthoredEvent), 0);
		InTest.TestEqual(TEXT("and resolves that component without a transient cache"), Resolve(InSample.Sender->AuthoredEvent),
			static_cast<UObject*>(InSample.Second));
		InTest.TestEqual(TEXT("a nested event followed the third component"), BindingIndex(InSample.Sender->NestedAuthoredEvents[0]), 1);
		InTest.TestEqual(TEXT("and resolves it"), Resolve(InSample.Sender->NestedAuthoredEvents[0]), static_cast<UObject*>(InSample.Third));
		InTest.TestEqual(TEXT("the legacy name was upgraded before the move"), BindingIndex(InSample.Sender->NestedAuthoredEvents[1]), 2);
		InTest.TestEqual(TEXT("and still names the original first component"), Resolve(InSample.Sender->NestedAuthoredEvents[1]),
			static_cast<UObject*>(InSample.First));
		return true;
	}

	void TestRemovedTargets(FAutomationTestBase& InTest, FSample& InSample)
	{
		InTest.TestEqual(TEXT("deleting the route's target removed the binding"), BindingCount(InSample.Sender->AuthoredEvent), 0);
		InTest.TestEqual(TEXT("the following route's index moved down"), BindingIndex(InSample.Sender->NestedAuthoredEvents[0]), 0);
		InTest.TestEqual(TEXT("and still names the third component"), Resolve(InSample.Sender->NestedAuthoredEvents[0]),
			static_cast<UObject*>(InSample.Third));
		InTest.TestEqual(TEXT("the legacy route also followed its target"), BindingIndex(InSample.Sender->NestedAuthoredEvents[1]), 1);
		ClearCache(InSample.Sender->AuthoredEvent);
		const int32 Before = InSample.First->TouchCount + InSample.Third->TouchCount;
		InSample.Sender->AuthoredEvent.FireEvent();
		InTest.TestEqual(TEXT("an unbound event never falls back to another component of that class"),
			InSample.First->TouchCount + InSample.Third->TouchCount, Before);
	}

	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;
		FSample Sample;

		FScopedDesigner()
		{
			Package = CreatePackage(TEXT("/Temp/DreamGUITests/BP_AuthoredEventComponentEditing"));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamUserWidget::StaticClass(),
				Package, FName(TEXT("BP_AuthoredEventComponentEditing")), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(),
				UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr) return;
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->SetDisplayName(TEXT("Root"));
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			Sample = BuildSample(Tree->RootWidget);
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			Editors->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false));
		}

		~FScopedDesigner()
		{
			if (GEditor != nullptr && Blueprint != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr) Package->RemoveFromRoot();
		}

		UDreamWidget* PreviewReceiver() const
		{
			const TSharedPtr<FDreamWidgetPreviewHost> Host = Designer != nullptr ? Designer->GetPreviewHost() : nullptr;
			if (!Host.IsValid()) return nullptr;
			Host->RebuildPreviewIfInvalidated();
			return Host->FindPreviewForTemplate(Sample.ReceiverWidget);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAuthoredEventComponentUndoAndSaveTest,
	"DreamGUI.Editor.Bindings.AuthoredEventComponentTargetsSurviveReorderUndoAndSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredEventComponentUndoAndSaveTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuthoredEventComponentEditingTestLocal;
	if (!HasEditor(*this)) return false;
	DreamOnDiskFixture::FScopedOnDiskPackage Package(TEXT("AuthoredEventComponentTargets"));
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(Package.Package, TEXT("AuthoredEventComponentTargets"), RF_Public | RF_Standalone | RF_Transactional);
	Tree->RootWidget = Tree->ConstructWidget<UDreamWidget>();
	Tree->RootWidget->SetDisplayName(TEXT("Root"));
	FSample Sample = BuildSample(Tree->RootWidget);
	{
		FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "ReorderAuthoredTargets", "Reorder event targets"));
		Sample.ReceiverWidget->Modify();
		Sample.ReceiverWidget->MoveComponentToIndex(Sample.First, 2);
	}
	TestMovedTargets(*this, Sample);
	Sample.Sender->AuthoredEvent.FireEvent();
	TestEqual(TEXT("the event calls only its original second component"), Sample.Second->TouchCount, 1);
	TestEqual(TEXT("the original first was not called"), Sample.First->TouchCount, 0);
	TestEqual(TEXT("nor was the third"), Sample.Third->TouchCount, 0);
	GEditor->UndoTransaction();
	TestEqual(TEXT("undo restores the old component array"), Sample.ReceiverWidget->GetAllComponents()[0], static_cast<UDreamUIBehaviour*>(Sample.First));
	TestEqual(TEXT("and the event owner's old index"), BindingIndex(Sample.Sender->AuthoredEvent), 1);
	TestEqual(TEXT("and the unresolved legacy index"), BindingIndex(Sample.Sender->NestedAuthoredEvents[1]), INDEX_NONE);
	TestEqual(TEXT("undo still resolves the original target"), Resolve(Sample.Sender->AuthoredEvent), static_cast<UObject*>(Sample.Second));
	GEditor->RedoTransaction();
	TestMovedTargets(*this, Sample);
	{
		FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "DeleteAuthoredTarget", "Delete an event target"));
		Sample.ReceiverWidget->Modify();
		Sample.Second->Modify();
		Sample.ReceiverWidget->RemoveComponent(Sample.Second);
	}
	TestRemovedTargets(*this, Sample);
	GEditor->UndoTransaction();
	TestMovedTargets(*this, Sample);
	GEditor->RedoTransaction();
	TestRemovedTargets(*this, Sample);

	FString Error;
	if (!TestTrue(TEXT("the edited hierarchy saves"), Package.Save(Tree, Error))) { AddInfo(Error); return false; }
	UDreamWidgetTree* Loaded = Cast<UDreamWidgetTree>(Package.Reload(Error));
	if (!TestNotNull(TEXT("the hierarchy was loaded from disk"), Loaded)) { AddInfo(Error); return false; }
	TestNotEqual(TEXT("the serializer produced a new tree"), Loaded, Tree);
	UDreamWidget* LoadedSenderWidget = Loaded->FindWidgetByVariableName(TEXT("Sender"));
	UDreamWidget* LoadedReceiver = Loaded->FindWidgetByVariableName(TEXT("Receiver"));
	if (!TestNotNull(TEXT("the sender was loaded"), LoadedSenderWidget)
		|| !TestNotNull(TEXT("the receiver was loaded"), LoadedReceiver)) return false;
	UDreamEventBindingTestBehaviour* LoadedSender = LoadedSenderWidget->GetComponent<UDreamEventBindingTestBehaviour>();
	if (!TestNotNull(TEXT("the sender's events were loaded"), LoadedSender)
		|| !TestEqual(TEXT("the deleted target stays deleted"), LoadedReceiver->GetAllComponents().Num(), 2)) return false;
	TestEqual(TEXT("its authored binding stays removed after saving"), BindingCount(LoadedSender->AuthoredEvent), 0);
	TestEqual(TEXT("the surviving serialized index is current"), BindingIndex(LoadedSender->NestedAuthoredEvents[0]), 0);
	TestEqual(TEXT("the surviving route resolves the reloaded third component"), Resolve(LoadedSender->NestedAuthoredEvents[0]),
		static_cast<UObject*>(LoadedReceiver->GetAllComponents()[0]));
	LoadedSender->NestedAuthoredEvents[0].FireEvent();
	TestEqual(TEXT("firing the loaded route calls its own third component"), CastChecked<UDreamEventBindingTestBehaviour>(LoadedReceiver->GetAllComponents()[0])->TouchCount, 1);
	TestEqual(TEXT("the other loaded component was not called"), CastChecked<UDreamEventBindingTestBehaviour>(LoadedReceiver->GetAllComponents()[1])->TouchCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAuthoredEventDesignerComponentEditingTest,
	"DreamGUI.Designer.ComponentEditsPreserveAuthoredEventTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredEventDesignerComponentEditingTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuthoredEventComponentEditingTestLocal;
	if (!HasEditor(*this)) return false;
	FScopedDesigner Scoped;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)
		|| !TestNotNull(TEXT("the receiver has a preview"), Scoped.PreviewReceiver())) return false;
	{
		FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "DesignerReorderEventTargets", "Reorder event targets in the designer"));
		UDreamWidget* Preview = Scoped.PreviewReceiver();
		TestTrue(TEXT("the real designer accepted the reorder"), Scoped.Designer->DesignerMoveComponent(Preview, Preview->GetAllComponents()[0], 2));
	}
	TestMovedTargets(*this, Scoped.Sample);
	GEditor->UndoTransaction();
	TestEqual(TEXT("the designer's undo restores the event owner"), BindingIndex(Scoped.Sample.Sender->AuthoredEvent), 1);
	GEditor->RedoTransaction();
	TestMovedTargets(*this, Scoped.Sample);
	{
		FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "DesignerDeleteEventTarget", "Delete an event target in the designer"));
		UDreamWidget* Preview = Scoped.PreviewReceiver();
		if (!TestNotNull(TEXT("the receiver was republished"), Preview)) return false;
		TestTrue(TEXT("the real designer accepted the removal"), Scoped.Designer->DesignerRemoveComponent(Preview, Preview->GetAllComponents()[0]));
	}
	TestRemovedTargets(*this, Scoped.Sample);
	GEditor->UndoTransaction();
	TestMovedTargets(*this, Scoped.Sample);
	GEditor->RedoTransaction();
	TestRemovedTargets(*this, Scoped.Sample);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAuthoredEventLevelComponentPanelTest,
	"DreamGUI.Editor.Bindings.TheLevelComponentPanelPreservesAuthoredEventTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredEventLevelComponentPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuthoredEventComponentEditingTestLocal;
	if (!HasEditor(*this) || !TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized())) return false;
	DreamTests::FScopedGameWorld World(EWorldType::Editor);
	TStrongObjectPtr<UDreamWidget> Root(NewObject<UDreamWidget>(World.World, NAME_None, RF_Public | RF_Transactional));
	FSample Sample = BuildSample(Root.Get());
	{
		// This is the in-place primitive the level component panel's reorder calls.
		FScopedTransaction Transaction(NSLOCTEXT("DreamGUITests", "LevelReorderEventTargets", "Reorder level event targets"));
		Sample.ReceiverWidget->Modify();
		Sample.ReceiverWidget->MoveComponentToIndex(Sample.First, 2);
	}
	TestMovedTargets(*this, Sample);
	TSharedRef<SDreamWidgetComponentEditor> Panel = SNew(SDreamWidgetComponentEditor)
		.GetWidgetContext_Lambda([&Sample]() { return Sample.ReceiverWidget; })
		.CanEdit_Lambda([]() { return true; });
	Panel->SelectComponent(Sample.Second);
	const FKeyEvent Delete(EKeys::Delete, FModifierKeysState(), 0, false, 0, 0);
	TestTrue(TEXT("the panel's real Delete command handled the selected component"), Panel->OnKeyDown(FGeometry(), Delete).IsEventHandled());
	TestRemovedTargets(*this, Sample);
	GEditor->UndoTransaction();
	TestMovedTargets(*this, Sample);
	GEditor->RedoTransaction();
	TestRemovedTargets(*this, Sample);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAuthoredEventHashContainersAreNotMutatedTest,
	"DreamGUI.Editor.Bindings.ComponentRemappingSkipsEventsInsideMapKeysAndSetElements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredEventHashContainersAreNotMutatedTest::RunTest(const FString& Parameters)
{
	using namespace DreamAuthoredEventComponentEditingTestLocal;
	TStrongObjectPtr<UDreamWidget> Root(NewObject<UDreamWidget>(GetTransientPackage(), NAME_None, RF_Transactional));
	FSample Sample = BuildSample(Root.Get());
	FDreamEventBindingTestKey Key;
	Key.Key = 7;
	Key.AuthoredEvent.AddFunctionBinding(Sample.ReceiverWidget, Sample.Second, TEXT("Touch"), EDreamUIEventDelegateParameterType::Empty, false);
	ClearCache(Key.AuthoredEvent);
	Sample.Sender->AuthoredEventKeys.Add(Key, 42);
	Sample.Sender->AuthoredEventSet.Add(Key);
	Sample.ReceiverWidget->MoveComponentToIndex(Sample.First, 2);
	TestMovedTargets(*this, Sample);
	const int32* Value = Sample.Sender->AuthoredEventKeys.Find(Key);
	TestTrue(TEXT("the map can still find its key"), Value != nullptr && *Value == 42);
	TestTrue(TEXT("the set can still find its element"), Sample.Sender->AuthoredEventSet.Contains(Key));
	for (TPair<FDreamEventBindingTestKey, int32>& Entry : Sample.Sender->AuthoredEventKeys)
	{
		TestEqual(TEXT("the event inside the map's struct key was left untouched"), BindingIndex(Entry.Key.AuthoredEvent), 1);
	}
	for (FDreamEventBindingTestKey& Entry : Sample.Sender->AuthoredEventSet)
	{
		TestEqual(TEXT("the event inside the set's struct element was left untouched"), BindingIndex(Entry.AuthoredEvent), 1);
	}
	return true;
}

#endif
