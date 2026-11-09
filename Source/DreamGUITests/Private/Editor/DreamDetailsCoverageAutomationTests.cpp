// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "DetailCustomization/DreamDetailsLayout.h"
#include "DetailCustomization/DreamPropertyMeta.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Event/DreamUIEventDelegate.h"
#include "IDetailTreeNode.h"
#include "IDetailsView.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "PropertyPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"

/*
 * Whether any editable property of DreamGUI cannot be found in the editor.
 *
 * Every class of the plugin's modules is shown the way the editor shows it -- a widget in its own details, its visual, its
 * layouts and its navigation inside the widget's details, a component in its own, a slot in its child's details under
 * each kind of panel, anything else as its class defaults -- and every editable property it has, and every field of the
 * plain structs among them, must have a row there, unless it is tagged DreamGUI_Hidden. Every struct of the plugin is
 * also shown on its own, as a list element is. A row an EditCondition hides until its switch is on counts: it shows the
 * moment the switch does.
 */
namespace DreamDetailsCoverageTestLocal
{
	const TCHAR* const Packages[] = {
		TEXT("/Script/DreamGUIRenderer"), TEXT("/Script/DreamGUI"), TEXT("/Script/DreamGUIInput"), TEXT("/Script/DreamGUIControls"),
		TEXT("/Script/DreamGUIExtensions"), TEXT("/Script/DreamTween"), TEXT("/Script/DreamGUINiagara"), TEXT("/Script/DreamGUISamples"),
		TEXT("/Script/DreamGUIEditor"), TEXT("/Script/DreamGUIPsd"), TEXT("/Script/DreamGUINiagaraEditor"),
	};

	/** Editor plumbing with properties no panel shows: factories, commandlets, graph nodes and the like. */
	const TCHAR* const PlumbingBases[] = {
		TEXT("Factory"), TEXT("Commandlet"), TEXT("EdGraphNode"), TEXT("EdGraphSchema"), TEXT("ThumbnailRenderer"),
		TEXT("ActorFactory"), TEXT("Subsystem"), TEXT("BlueprintFunctionLibrary"), TEXT("AssetDefinition"), TEXT("Interface"),
		TEXT("AutomationTestSettings"),
	};

	bool IsOurs(const UField* InField)
	{
		if (InField == nullptr)
		{
			return false;
		}
		const FName Package = InField->GetOutermost()->GetFName();
		for (const TCHAR* Name : Packages)
		{
			if (Package == FName(Name))
			{
				return true;
			}
		}
		return false;
	}

	bool IsPlumbing(const UClass* InClass)
	{
		for (const UClass* Each = InClass; Each != nullptr; Each = Each->GetSuperClass())
		{
			for (const TCHAR* Name : PlumbingBases)
			{
				if (Each->GetFName() == FName(Name))
				{
					return true;
				}
			}
		}
		return false;
	}

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** What a panel showed: the properties with rows, the groups named after a header property, and the struct rows with children. */
	struct FShown
	{
		TSet<const FProperty*> Properties;
		TSet<FName> GroupNames;
		TSet<const FProperty*> WithChildren;
		/** How many rows each of the object's own properties has, array elements aside: more than one is a property drawn twice. */
		TMap<const FProperty*, int32> RowCounts;
		void Append(const FShown& InOther)
		{
			Properties.Append(InOther.Properties);
			GroupNames.Append(InOther.GroupNames);
			WithChildren.Append(InOther.WithChildren);
			for (const TPair<const FProperty*, int32>& Pair : InOther.RowCounts)
			{
				int32& Count = RowCounts.FindOrAdd(Pair.Key);
				Count = FMath::Max(Count, Pair.Value);
			}
		}
	};

	void CollectFromNode(const TSharedRef<IDetailTreeNode>& InNode, FShown& OutShown)
	{
		const TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle();
		const FProperty* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
		if (Property)
		{
			OutShown.Properties.Add(Property);
			if (Property->GetOwner<UClass>() != nullptr && Handle->GetIndexInArray() == INDEX_NONE)
			{
				++OutShown.RowCounts.FindOrAdd(Property);
			}
		}
		// A group whose header is a property (IDetailGroup::HeaderProperty) is named after it and lists no node for it.
		OutShown.GroupNames.Add(InNode->GetNodeName());
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/true);
		if (Property && Children.Num() > 0)
		{
			OutShown.WithChildren.Add(Property);
		}
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			CollectFromNode(Child, OutShown);
		}
	}

	/**
	 * The rows of a row generator over InObjects, or InStruct: every row, hidden ones included, and the fields of each struct
	 * row. It lays out no object a category adds from outside (that needs a details view), which the details view adds.
	 */
	FShown FromRowGenerator(const TArray<UObject*>& InObjects, const TSharedPtr<FStructOnScope>& InStruct = nullptr)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		const TSharedRef<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		if (InStruct.IsValid())
		{
			Generator->SetStructure(InStruct);
		}
		else
		{
			Generator->SetObjects(InObjects);
		}
		FShown Shown;
		for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
		{
			CollectFromNode(Root, Shown);
		}
		return Shown;
	}

	/** The rows of a real details view over InObjects, advanced ones shown: what an author sees, objects added from outside included. */
	FShown FromDetailsView(const TArray<UObject*>& InObjects)
	{
		FDetailsViewArgs Args;
		Args.bAllowSearch = false;
		Args.bShowOptions = false;
		Args.bHideSelectionTip = true;
		Args.bAllowFavoriteSystem = false;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		const TSharedRef<IDetailsView> View = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
		View->ShowAllAdvancedProperties();
		View->SetObjects(InObjects, /*bForceRefresh*/true);
		FShown Shown;
		for (const FPropertyPath& Path : View->GetPropertiesInOrderDisplayed())
		{
			for (int32 Index = 0; Index < Path.GetNumProperties(); ++Index)
			{
				if (const FProperty* Property = Path.GetPropertyInfo(Index).Property.Get())
				{
					Shown.Properties.Add(Property);
				}
			}
		}
		View->SetObjects(TArray<UObject*>());
		return Shown;
	}

	bool IsEditable(const FProperty* InProperty)
	{
		return InProperty->HasAnyPropertyFlags(CPF_Edit) && !InProperty->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditConst)
#if WITH_EDITORONLY_DATA
			&& !InProperty->HasMetaData(TEXT("HideInDetailPanel"))
			// A toggle drawn inline on the row whose edit condition it is: a check box, not a row of its own.
			&& !InProperty->HasMetaData(TEXT("InlineEditConditionToggle"))
#endif
			;
	}

	/** The properties of InStruct and the fields of its plain structs (and of a struct whose layout shows its fields) a panel owes a row. */
	void CollectExpected(const UStruct* InStruct, const FShown& InShown, const FString& InPrefix, int32 InDepth,
		TArray<TPair<FString, const FProperty*>>& OutExpected)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			const FProperty* Property = *It;
			// Only DreamGUI's own: what an engine base class declares is the engine's to show.
			if (!IsOurs(Property->GetOwnerStruct()) || !IsEditable(Property) || DreamPropertyMeta::IsHidden(Property))
			{
				continue;
			}
			const FString Path = InPrefix.IsEmpty() ? Property->GetName() : InPrefix + TEXT(".") + Property->GetName();
			OutExpected.Emplace(Path, Property);
			const FStructProperty* AsStruct = CastField<FStructProperty>(Property);
			if (AsStruct == nullptr || InDepth >= 3 || Property->ArrayDim != 1 || !InShown.Properties.Contains(Property))
			{
				continue;
			}
			// A struct with a layout of its own that edits it whole (a curve, a colour) owes no rows for its fields; one that
			// shows some of them owes them all.
			const bool bCustomized = PropertyEditor.IsCustomizedStruct(AsStruct->Struct, FCustomPropertyTypeLayoutMap());
			if (!bCustomized || InShown.WithChildren.Contains(Property))
			{
				CollectExpected(AsStruct->Struct, InShown, Path, InDepth + 1, OutExpected);
			}
		}
	}

	TArray<FString> Missing(const UStruct* InStruct, const FShown& InShown, TFunctionRef<bool(const FProperty*)> InExcuse)
	{
		TArray<TPair<FString, const FProperty*>> Expected;
		CollectExpected(InStruct, InShown, FString(), 0, Expected);
		TArray<FString> Out;
		for (const TPair<FString, const FProperty*>& Pair : Expected)
		{
			const bool bAsGroup = !Pair.Key.Contains(TEXT(".")) && InShown.GroupNames.Contains(Pair.Value->GetFName());
			if (!InShown.Properties.Contains(Pair.Value) && !bAsGroup && !InExcuse(Pair.Value))
			{
				Out.Add(Pair.Key);
			}
		}
		return Out;
	}

	/** The categories of InObject's details and the properties directly in each: "Layer: RenderOpacity, bClipToBelow; ...". */
	FString CategoriesOf(UObject* InObject)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		const TSharedRef<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->SetObjects({ InObject });
		TArray<FString> Lines;
		for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
		{
			TArray<FString> Names;
			TArray<TSharedRef<IDetailTreeNode>> Children;
			Root->GetChildren(Children, /*bInIgnoreVisibility*/true);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				const TSharedPtr<IPropertyHandle> Handle = Child->CreatePropertyHandle();
				Names.Add(Handle.IsValid() && Handle->GetProperty() ? Handle->GetProperty()->GetName() : FString::Printf(TEXT("(%s)"), *Child->GetNodeName().ToString()));
			}
			Lines.Add(FString::Printf(TEXT("%s: %s"), *Root->GetNodeName().ToString(), *FString::Join(Names, TEXT(", "))));
		}
		return FString::Join(Lines, TEXT("; "));
	}

	/** A widget to host what is shown inside a widget's details, with a visual for the components that need one. */
	UDreamWidget* MakeWidget(UWorld* InWorld, UClass* InVisual = UDreamRectBlock::StaticClass())
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld);
		Widget->SetWidth(200.0f);
		Widget->SetHeight(100.0f);
		if (InVisual)
		{
			Widget->CreateNewVisual(InVisual);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDetailsCoverageTest,
	"DreamGUI.Editor.Details.EveryEditablePropertyHasARowUnlessTaggedHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDetailsCoverageTest::RunTest(const FString& Parameters)
{
	using namespace DreamDetailsCoverageTestLocal;
	FScopedTestWorld TestWorld;
	// A font's class defaults name no font file, and the details of a font load the file they name: shown as defaults, each
	// says it found none.
	AddExpectedError(TEXT("InitFreeType"), EAutomationExpectedErrorFlags::Contains, 0, /*bIsRegex*/false);
	TArray<FString> AllMissing;
	TArray<FString> AllTwice;
	int32 Subjects = 0;
	auto Report = [this, &AllMissing, &Subjects](const FString& InSubject, const TArray<FString>& InMissing)
	{
		++Subjects;
		for (const FString& Path : InMissing)
		{
			AllMissing.Add(FString::Printf(TEXT("%s.%s"), *InSubject, *Path));
		}
	};
	const auto NoExcuse = [](const FProperty*) { return false; };

	TArray<UClass*> PanelLayouts;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!IsOurs(Class) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || IsPlumbing(Class))
		{
			continue;
		}
		if (Class->IsChildOf(UDreamPanelLayoutBase::StaticClass()))
		{
			PanelLayouts.Add(Class);
		}
		bool bHasOwn = false;
		for (TFieldIterator<FProperty> Field(Class); Field && !bHasOwn; ++Field)
		{
			bHasOwn = IsOurs(Field->GetOwnerStruct()) && IsEditable(*Field);
		}
		if (!bHasOwn || Class->IsChildOf(UDreamPanelSlot::StaticClass()))
		{
			continue;
		}

		FShown Shown;
		if (Class->IsChildOf(UDreamWidget::StaticClass()))
		{
			TStrongObjectPtr<UObject> Widget(NewObject<UObject>(TestWorld.World, Class));
			Shown.Append(FromDetailsView({ Widget.Get() }));
			Shown.Append(FromRowGenerator({ Widget.Get() }));
		}
		else if (Class->IsChildOf(UDreamVisual::StaticClass()) || Class->IsChildOf(UDreamLayoutContainer::StaticClass())
			|| Class->IsChildOf(UDreamLayoutSelf::StaticClass()) || Class->IsChildOf(UDreamWidgetNavigation::StaticClass()))
		{
			// Shown inside the widget's details, under its own layout there.
			TStrongObjectPtr<UDreamWidget> Widget(MakeWidget(TestWorld.World, nullptr));
			UObject* Inner = Class->IsChildOf(UDreamVisual::StaticClass()) ? (UObject*)Widget->CreateNewVisual(Class)
				: Class->IsChildOf(UDreamLayoutContainer::StaticClass()) ? (UObject*)Widget->CreateNewLayoutContainer(Class)
				: Class->IsChildOf(UDreamLayoutSelf::StaticClass()) ? (UObject*)Widget->CreateNewLayoutSelf(Class)
				: (UObject*)Widget->GetOrCreateNavigation();
			if (!TestNotNull(*FString::Printf(TEXT("%s was made on a widget"), *Class->GetName()), Inner))
			{
				continue;
			}
			Shown.Append(FromDetailsView({ Widget.Get() }));
			Shown.Append(FromRowGenerator({ Inner }));
		}
		else if (Class->IsChildOf(UDreamUIBehaviour::StaticClass()))
		{
			TStrongObjectPtr<UDreamWidget> Widget(MakeWidget(TestWorld.World));
			UDreamUIBehaviour* Component = Widget->AddComponent(Class);
			if (Component == nullptr)
			{
				Widget.Reset(MakeWidget(TestWorld.World, UDreamText::StaticClass()));
				Component = Widget->AddComponent(Class);
			}
			if (!TestNotNull(*FString::Printf(TEXT("%s was put on a widget"), *Class->GetName()), Component))
			{
				continue;
			}
			Shown.Append(FromDetailsView({ Component }));
			Shown.Append(FromRowGenerator({ Component }));
		}
		else
		{
			UObject* Defaults = Class->GetDefaultObject();
			Shown.Append(FromDetailsView({ Defaults }));
			Shown.Append(FromRowGenerator({ Defaults }));
		}
		Report(Class->GetName(), Missing(Class, Shown, [Class](const FProperty* InProperty)
		{
			// A visual's Layer rows are drawn by the widget's Layer section, which the widget's details view holds; an event's
			// layout (or a data table row's, the engine's) draws its rows in place of the property's own, which leaves no row of it to find.
			// What only class defaults edit (EditDefaultsOnly) has no row on the instance shown here, as it should not.
			const FStructProperty* AsStruct = CastField<FStructProperty>(InProperty);
			return FDreamDetailsLayout::IsDrawnByHost(Class, DreamPropertyMeta::GetSection(InProperty))
				|| (AsStruct != nullptr && (AsStruct->Struct->IsChildOf(FDreamUIEventDelegate::StaticStruct()) || AsStruct->Struct == FDataTableRowHandle::StaticStruct()))
				|| InProperty->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
		}));
		// And none drawn twice: a row of a customization's own and the default row it was meant to replace.
		for (const TPair<const FProperty*, int32>& Pair : Shown.RowCounts)
		{
			if (Pair.Value > 1 && IsOurs(Pair.Key->GetOwnerStruct()))
			{
				AllTwice.Add(FString::Printf(TEXT("%s.%s"), *Class->GetName(), *Pair.Key->GetName()));
			}
		}
		if (Class == UDreamWidget::StaticClass() || Class == UDreamUserWidget::StaticClass())
		{
			// Where each of the widget's own properties is, by category: none of them should be left in a category of its
			// declaration ("DreamGUI") for want of a section.
			TStrongObjectPtr<UObject> Widget(NewObject<UObject>(TestWorld.World, Class));
			AddInfo(FString::Printf(TEXT("[%s] %s"), *Class->GetName(), *CategoriesOf(Widget.Get())));
		}
	}

	// A slot shows what the panel it is in arranges by: each of its properties under at least one kind of panel.
	{
		FShown Shown;
		for (UClass* Layout : PanelLayouts)
		{
			TStrongObjectPtr<UDreamWidget> Parent(MakeWidget(TestWorld.World, nullptr));
			Parent->CreateNewLayoutContainer(Layout);
			UDreamWidget* Child = MakeWidget(TestWorld.World, nullptr);
			Parent->AddChild(Child);
			Shown.Append(FromDetailsView({ Child }));
		}
		Report(TEXT("DreamPanelSlot"), Missing(UDreamPanelSlot::StaticClass(), Shown, NoExcuse));
	}

	// Every struct on its own, as an element of a list is shown.
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!IsOurs(Struct) || PropertyEditor.IsCustomizedStruct(Struct, FCustomPropertyTypeLayoutMap()))
		{
			continue;
		}
		bool bHasOwn = false;
		for (TFieldIterator<FProperty> Field(Struct); Field && !bHasOwn; ++Field)
		{
			bHasOwn = IsEditable(*Field);
		}
		if (!bHasOwn)
		{
			continue;
		}
		const FShown Shown = FromRowGenerator({}, MakeShared<FStructOnScope>(Struct));
		Report(FString(TEXT("F")) + Struct->GetName(), Missing(Struct, Shown, NoExcuse));
	}

	AddInfo(FString::Printf(TEXT("%d classes and structs shown; %d editable properties without a row"), Subjects, AllMissing.Num()));
	for (const FString& Path : AllMissing)
	{
		AddInfo(FString::Printf(TEXT("no row: %s"), *Path));
	}
	TestEqual(TEXT("Every editable property not tagged DreamGUI_Hidden has a row where the editor shows it"), AllMissing.Num(), 0);
	for (const FString& Path : AllTwice)
	{
		AddInfo(FString::Printf(TEXT("two rows: %s"), *Path));
	}
	TestEqual(TEXT("No property has two rows in one panel"), AllTwice.Num(), 0);
	return true;
}

#endif
