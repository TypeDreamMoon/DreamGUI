// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIEditorTools.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamGUISettings.h"
#include "DreamUIControlRegistry.h"
#include "Core/DreamUIManager.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Widgets/SViewport.h"
#include "Engine/Selection.h"
#include "DreamGUIEditorModule.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/DreamUIBehaviour.h"
#include "UObject/UnrealType.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/DreamWidgetPresenterComponent.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "DreamGUIEditorTools"


FEditingWidgetChangedDelegate FDreamUIEditorTools::OnEditingWidgetChanged;

struct FDreamUIEditorToolsHelperFunctionHolder
{
	static FString RemoveNumericSuffix(const FString& Name)
	{
		int32 SuffixIndex = INDEX_NONE;
		if (!Name.FindLastChar(TEXT('_'), SuffixIndex) || SuffixIndex + 1 >= Name.Len())
		{
			return Name;
		}
		for (int32 Index = SuffixIndex + 1; Index < Name.Len(); ++Index)
		{
			if (Name[Index] < TEXT('0') || Name[Index] > TEXT('9'))
			{
				return Name;
			}
		}
		return Name.Left(SuffixIndex);
	}

	static FString MakeUniqueName(const FString& DesiredName, const TSet<FName>& UsedNames)
	{
		FString Candidate = DesiredName.TrimStartAndEnd();
		if (Candidate.IsEmpty())
		{
			Candidate = TEXT("Widget");
		}
		if (!UsedNames.Contains(FName(*Candidate)))
		{
			return Candidate;
		}

		FString BaseName = RemoveNumericSuffix(Candidate);
		if (BaseName.IsEmpty())
		{
			BaseName = TEXT("Widget");
		}
		Candidate = BaseName;
		int32 Postfix = 0;
		while (UsedNames.Contains(FName(*Candidate)))
		{
			++Postfix;
			Candidate = FString::Printf(TEXT("%s_%d"), *BaseName, Postfix);
		}
		return Candidate;
	}

	static UDreamWidget* GetNamingRoot(UDreamWidget* ContextWidget)
	{
		if (!IsValid(ContextWidget))
		{
			return nullptr;
		}
		return ContextWidget->GetRootWidgetInHierarchy();
	}
};



namespace
{
	/**
	 * Snapshot everything a hierarchy change writes, before it is written.
	 *
	 * Attaching a widget writes the parent's Children array, so the parent is what undo has to
	 * restore. DeleteWidgets has always done this; the create and paste paths opened a transaction
	 * and only Modify()'d the selection object and the prefab helper, so the transaction held
	 * nothing that describes the new widget being there. Ctrl+Z then popped an entry that restored
	 * nothing, and the next Ctrl+Z undid the user's *previous* edit -- so the visible effect of
	 * undoing a create was losing the change before it.
	 */
	void ModifyForHierarchyChange(UDreamWidget* InParent, UDreamWidget* InChild = nullptr)
	{
		if (IsValid(InParent))
		{
			if (UObject* Outer = InParent->GetOuter())Outer->Modify();
			InParent->SetFlags(RF_Public | RF_Transactional);
			InParent->Modify();
		}
		if (IsValid(InChild))
		{
			InChild->SetFlags(RF_Public | RF_Transactional);
			InChild->Modify();
		}
	}

	/**
	 * Could InCandidatePackage reach InTargetPackage on disk?
	 *
	 * Asking the loaded prefab whether it nests another one is not free -- UDreamUIPrefab answers it by
	 * building a preview scene for itself -- and every palette click would pay that. Nesting implies
	 * a package reference, so walk the target's referencers, which number in the handful, rather than
	 * the candidate's dependency closure, which does not. A registry that cannot answer says yes, so
	 * the authoritative check still runs.
	 */
	bool MayReferencePackage(FName InCandidatePackage, FName InTargetPackage)
	{
		if (InCandidatePackage == InTargetPackage)return true;
		IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		// A registry still scanning returns partial or empty referencers, which reads exactly like
		// "no path exists" -- and this prefilter's whole contract is that a registry which cannot
		// answer says yes, so the authoritative check downstream still runs.
		if (AssetRegistry == nullptr || AssetRegistry->IsLoadingAssets())return true;
		TSet<FName> Visited;
		TArray<FName> Pending;
		Pending.Add(InTargetPackage);
		while (Pending.Num() > 0)
		{
			TArray<FName> Referencers;
			AssetRegistry->GetReferencers(Pending.Pop(), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			for (FName Referencer : Referencers)
			{
				if (Referencer == InCandidatePackage)return true;
				bool bAlreadyKnown = false;
				Visited.Add(Referencer, &bAlreadyKnown);
				if (!bAlreadyKnown)Pending.Add(Referencer);
			}
		}
		return false;
	}
}

FString FDreamUIEditorTools::MakeUniqueWidgetDisplayName(
	UDreamWidget* ContextWidget,
	const FString& DesiredName,
	const UDreamWidget* WidgetToIgnore)
{
	TSet<FName> UsedNames;
	if (UDreamWidget* RootWidget = FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(ContextWidget))
	{
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(RootWidget, Widgets);
		for (const UDreamWidget* Widget : Widgets)
		{
			if (IsValid(Widget) && Widget != WidgetToIgnore)
			{
				UsedNames.Add(FName(*Widget->GetDisplayName()));
			}
		}
	}
	return FDreamUIEditorToolsHelperFunctionHolder::MakeUniqueName(DesiredName, UsedNames);
}

int32 FDreamUIEditorTools::EnsureUniqueWidgetDisplayNames(UDreamWidget* RootWidget, TArray<FString>* OutRenamedWidgets)
{
	if (!IsValid(RootWidget))
	{
		return 0;
	}

	TArray<UDreamWidget*> Widgets;
	UDreamWidget::CollectChildrenWidgets(RootWidget, Widgets);
	TSet<FName> UsedNames;
	int32 RenameCount = 0;
	for (UDreamWidget* Widget : Widgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		const FString OldName = Widget->GetDisplayName();
		const FString UniqueName = FDreamUIEditorToolsHelperFunctionHolder::MakeUniqueName(OldName, UsedNames);
		UsedNames.Add(FName(*UniqueName));
		if (OldName.Equals(UniqueName, ESearchCase::CaseSensitive))
		{
			continue;
		}

		Widget->Modify();
		FDreamUIUtils::ChangePropertyWithNotify(Widget, UDreamWidget::GetPropertyName_DisplayName(), [Widget, UniqueName]()
		{
			Widget->SetDisplayName(UniqueName);
		});
		if (OutRenamedWidgets)
		{
			OutRenamedWidgets->Add(FString::Printf(TEXT("%s -> %s"), *OldName, *UniqueName));
		}
		++RenameCount;
	}
	return RenameCount;
}

TArray<UDreamWidget*> FDreamUIEditorTools::GetRootWidgetListFromSelection(const TArray<UDreamWidget*>& InSelectedWidgets)
{
	TArray<UDreamWidget*> RootWidgetList;
	auto count = InSelectedWidgets.Num();
	//search upward find parent and put into list, only root Widget can add to list
	for (int i = 0; i < count; i++)
	{
		auto obj = InSelectedWidgets[i];
		auto parent = obj->GetParent();
		bool isRootWidget = false;
		while (true)
		{
			if (parent == nullptr)//top level
			{
				isRootWidget = true;
				break;
			}
			if (InSelectedWidgets.Contains(parent))//if parent is already in list, skip it
			{
				isRootWidget = false;
				break;
			}
			else//if not in list, keep search upward
			{
				parent = parent->GetParent();
				continue;
			}
		}
		if (isRootWidget)
		{
			RootWidgetList.Add(obj);
		}
	}
	return RootWidgetList;
}

void FDreamUIEditorTools::CreateWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(UDreamWidget*)> Callback)
{
	CreateWidgetAndReturn(MoveTemp(GetSelectedWidgetFunction), MoveTemp(Name), VisualClass, MoveTemp(Callback));
}

UDreamWidget* FDreamUIEditorTools::CreateWidgetAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(UDreamWidget*)> Callback)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	// Every one of these refusals used to be silent, so a palette double-click with nothing selected
	// looked like a broken panel rather than a missing parent.
	if (SelectedWidget == nullptr)
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot create widget '%s': no parent widget is selected."), *Name);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot create widget '%s': widget '%s' is not a valid parent."), *Name, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Widget '%s' cannot accept another child."), *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("CreateChildWidget_Transaction", "DreamUI Child Widget"));
	// In a designer the selection is a PREVIEW of the asset, and anything built next to it is thrown
	// away by the next rebuild. Build into the authoring tree instead and hand back the preview of
	// what was built, which is what the caller goes on to select.
	if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(SelectedWidget))
	{
		UDreamWidget* Created = Designer->DesignerCreateWidget(SelectedWidget, UDreamWidget::StaticClass(), Name,
			[VisualClass, Callback](UDreamWidget* InTemplate)
			{
				InTemplate->SetAnchoredPosition(FVector2D::ZeroVector);
				if (VisualClass)
				{
					InTemplate->CreateNewVisual(VisualClass);
				}
				if (Callback)
				{
					Callback(InTemplate);
				}
			});
		if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(SelectedWidget->GetWorld()))
		{
			Selection->SelectNone();
			if (Created != nullptr)
			{
				Selection->SelectWidget(Created);
			}
		}
		return Created;
	}
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	ModifyForHierarchyChange(SelectedWidget);
	auto NewWidget = NewObject<UDreamWidget>(SelectedWidget->GetOuter(), UDreamWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
	if (IsValid(NewWidget))
	{
		NewWidget->Modify();
		NewWidget->SetDisplayName(MakeUniqueWidgetDisplayName(SelectedWidget, Name));
		NewWidget->OnRegister();
		if (SelectedWidget != nullptr)
		{
			NewWidget->SetParent(SelectedWidget, false);
			NewWidget->SetAnchoredPosition(FVector2D::ZeroVector);
			UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->SelectNone();
		} 
		if (VisualClass)
		{
			NewWidget->CreateNewVisual(VisualClass);
		}
		if (Callback)
		{
			Callback(NewWidget);
		}
		EnsureUniqueWidgetDisplayNames(FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(NewWidget));
		UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->SelectWidget(NewWidget);
	}
	return NewWidget;
}

void FDreamUIEditorTools::CreateUIControls(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString InControlClassPath)
{
	CreateUIControlsAndReturn(MoveTemp(GetSelectedWidgetFunction), MoveTemp(InControlClassPath));
}

UDreamWidget* FDreamUIEditorTools::PlaceControlClassAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction,
	UClass* ControlClass, const FString& InDisplayName, TFunction<void(UDreamWidget*)> Callback)
{
	// Everything below the class-resolution step, which is the only part the two roads to a control
	// class disagree about: a Blueprint one arrives as an asset path to load, and a code-built one
	// arrives as the class. Placing them is identical work, because a UDreamUIControl subclass is a
	// UDreamUserWidget subclass exactly as a compiled DreamUI Blueprint is, and both are placed by
	// instancing. Keeping it one function is what stops the second road from growing its own
	// almost-right copy of the designer/no-designer split below.
	if (ControlClass == nullptr || !ControlClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Cannot create control '%s': it is not a DreamUI user widget class."), *InDisplayName);
		return nullptr;
	}
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot create control '%s': no parent widget is selected."), *InDisplayName);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot create control '%s': widget '%s' is not a valid parent."), *InDisplayName, *SelectedWidget->GetDisplayName());
		return nullptr;
	}

	if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(SelectedWidget))
	{
		// A control IS a class now, so placing one is creating a widget of that class -- its contents
		// come from its own class when the preview instances it.
		//
		// Named after the BLUEPRINT, not the class: a generated class is BP_TextInput_C, and the
		// display name is what the hierarchy shows and what the compiler makes a variable of.
		UDreamWidget* Created = Designer->DesignerCreateWidget(SelectedWidget, ControlClass,
			InDisplayName, [Callback](UDreamWidget* InTemplate)
			{
				InTemplate->SetAnchoredPosition(FVector2D::ZeroVector);
				if (Callback)
				{
					Callback(InTemplate);
				}
			});
		if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(SelectedWidget->GetWorld()))
		{
			Selection->SelectNone();
			if (Created != nullptr)
			{
				Selection->SelectWidget(Created);
			}
		}
		return Created;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Widget '%s' cannot accept another child control."), *SelectedWidget->GetDisplayName());
		return nullptr;
	}

	// The control is a CLASS, and what lands in the hierarchy is an instance of it.
	//
	// This used to load a prefab and flatten a copy of its widgets into the tree, which is why fixing
	// the shipped Button never reached a single Button anyone had already dropped -- there was no link
	// left to follow. An instance keeps one.
	const FScopedTransaction Transaction(LOCTEXT("CreateUIControl_Transaction", "DreamUI Create UI Control"));
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	ModifyForHierarchyChange(SelectedWidget);

	UDreamWidget* CreatedWidget = CreateDreamWidget(SelectedWidget->GetWorld(), ControlClass, SelectedWidget);
	if (!IsValid(CreatedWidget))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Control class '%s' produced no widget."), *InDisplayName);
		return nullptr;
	}
	if (Callback)Callback(CreatedWidget);
	EnsureUniqueWidgetDisplayNames(FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(CreatedWidget));
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->SelectNone();
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->SelectWidget(CreatedWidget);
	return CreatedWidget;
}

UDreamWidget* FDreamUIEditorTools::CreateUIControlsAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString InControlClassPath, TFunction<void(UDreamWidget*)> Callback)
{
	// Resolved ONCE, here. A control is named by its ASSET path and the class is the Blueprint's
	// generated one; resolving it a second time is how the designer branch came to call LoadClass on
	// an asset path and refuse every control in the palette.
	UBlueprint* ControlBlueprint = LoadObject<UBlueprint>(nullptr, *(InControlClassPath + TEXT(".") + FPackageName::GetShortName(InControlClassPath)));
	UClass* ControlClass = ControlBlueprint != nullptr ? ControlBlueprint->GeneratedClass.Get() : nullptr;
	if (ControlClass == nullptr || !ControlClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Load control class error! Path:%s. Missing some content of the DreamUI plugin; reinstalling it may fix this."), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InControlClassPath);
		return nullptr;
	}
	// The BLUEPRINT's name, not the class's: a generated class is BP_TextInput_C, and that suffix
	// would be what the hierarchy shows and what the compiler makes a variable of.
	return PlaceControlClassAndReturn(MoveTemp(GetSelectedWidgetFunction), ControlClass,
		ControlBlueprint->GetName(), MoveTemp(Callback));
}

void FDreamUIEditorTools::CreateRegisteredControl(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FName ControlName)
{
	CreateRegisteredControlAndReturn(MoveTemp(GetSelectedWidgetFunction), ControlName);
}

UDreamWidget* FDreamUIEditorTools::CreateRegisteredControlAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FName ControlName, TFunction<void(UDreamWidget*)> Callback)
{
	const FDreamUIControlDescriptor* Descriptor = FDreamUIControlRegistry::Get().GetDescriptors().FindByPredicate([ControlName](const FDreamUIControlDescriptor& Item)
	{
		return Item.Name == ControlName;
	});
	if (!Descriptor)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Unknown registered control: %s"), *ControlName.ToString());
		return nullptr;
	}
	FText ValidationError;
	if (!FDreamUIControlRegistry::Get().Validate(*Descriptor, ValidationError))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Cannot create control '%s': %s"), *ControlName.ToString(), *ValidationError.ToString());
		return nullptr;
	}
	if (Descriptor->CreationKind == EDreamUIControlCreationKind::WidgetClass)
	{
		return CreateUIControlsAndReturn(MoveTemp(GetSelectedWidgetFunction), Descriptor->WidgetClassPath, MoveTemp(Callback));
	}
	if (Descriptor->CreationKind == EDreamUIControlCreationKind::ControlClass)
	{
		// The class is already in hand -- Validate above has established it is a concrete
		// UDreamUserWidget subclass -- so the only step the Blueprint road adds is the one this
		// road does not need.
		//
		// Named after the class with the family prefix off: the name becomes the hierarchy row and
		// the Blueprint variable, and "ProgressBar" is what an author calls the thing while
		// "DreamProgressBar" is what C++ calls it. Not the registry name, which is a key and has to
		// stay distinct from the legacy entry's ("ProgressBar" is taken); not the tag, which carries
		// a dot that no variable name can.
		FString ControlName = Descriptor->ControlClass->GetName();
		ControlName.RemoveFromStart(TEXT("Dream"));
		// NativeConfigure runs for this kind too, and it is what lets ONE control class back two
		// palette rows. A slider is one class with a Direction, but "Horizontal Slider" and
		// "Vertical Slider" are two things an author looks for by name; the entry carries the
		// property write that tells them apart. Ordered before the caller's callback so a drop that
		// positions the new widget still gets the last word.
		TFunction<void(UDreamWidget*)> Configure = Descriptor->NativeConfigure;
		return PlaceControlClassAndReturn(MoveTemp(GetSelectedWidgetFunction), Descriptor->ControlClass.Get(),
			ControlName, [Configure, Callback = MoveTemp(Callback)](UDreamWidget* InWidget) mutable
			{
				if (Configure)
				{
					Configure(InWidget);
				}
				if (Callback)
				{
					Callback(InWidget);
				}
			});
	}

	const FDreamUIControlDescriptor Recipe = *Descriptor;
	// Name new widgets from the terse registry name ("CanvasPanel"), not the palette label — labels
	// now carry family prefixes ("UMG Canvas Panel") that would pollute hierarchy names and prefabs.
	return CreateWidgetAndReturn(MoveTemp(GetSelectedWidgetFunction), Recipe.Name.ToString(), Recipe.VisualClass.Get(),
		[Recipe, Callback = MoveTemp(Callback)](UDreamWidget* InWidget) mutable
		{
			if (Recipe.LayoutContainerClass.IsValid())
			{
				InWidget->CreateNewLayoutContainer(Recipe.LayoutContainerClass.Get());
			}
			if (Recipe.LayoutSelfClass.IsValid())
			{
				InWidget->CreateNewLayoutSelf(Recipe.LayoutSelfClass.Get());
			}
			if (Recipe.BehaviourClass.IsValid())
			{
				InWidget->AddComponent(Recipe.BehaviourClass.Get());
			}
			if (Recipe.MeshModifierClass.IsValid())
			{
				if (UDreamVisualBatchMesh* Visual = Cast<UDreamVisualBatchMesh>(InWidget->GetVisual()))
				{
					Visual->AddMeshModifier(Recipe.MeshModifierClass.Get());
				}
			}
			if (Recipe.NativeConfigure)
			{
				Recipe.NativeConfigure(InWidget);
			}
			if (Callback)
			{
				Callback(InWidget);
			}
		});
}

void FDreamUIEditorTools::DuplicateWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	auto RootWidgetList = FDreamUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	if (RootWidgetList.Num() > 0)
	{
		if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(RootWidgetList[0]))
		{
			const FScopedTransaction Transaction(LOCTEXT("DuplicateWidget_Transaction", "DreamUI Duplicate Widgets"));
			const TArray<UDreamWidget*> Created = Designer->DesignerDuplicateWidgets(RootWidgetList);
			if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(RootWidgetList[0]->GetWorld()))
			{
				Selection->SelectNone();
				for (UDreamWidget* Widget : Created)
				{
					Selection->SelectWidget(Widget);
				}
			}
			return;
		}
	}
	TMap<UDreamWidget*, int32> AdditionalChildrenByParent;
	for (UDreamWidget* Widget : RootWidgetList)
	{
		if (IsValid(Widget) && IsValid(Widget->GetParent()))
		{
			++AdditionalChildrenByParent.FindOrAdd(Widget->GetParent());
		}
	}
	for (const TPair<UDreamWidget*, int32>& Pair : AdditionalChildrenByParent)
	{
		if (!Pair.Key->CanAcceptAdditionalChildren(Pair.Value))
		{
			UE_LOG(DreamGUIEditor, Warning, TEXT("Widget '%s' cannot accept %d duplicated child widget(s)."),
				*Pair.Key->GetDisplayName(), Pair.Value);
			return;
		}
	}
	const FScopedTransaction Transaction(LOCTEXT("DuplicateWidget_Transaction", "DreamUI Duplicate Widgets"));
	auto World = SelectedWidgets[0]->GetWorld();
	UDreamUISelection::GetInstance(World)->Modify();
	UDreamUISelection::GetInstance(World)->SelectNone();
	for (auto Widget : RootWidgetList)
	{
		Widget->GetOuter()->Modify();
		auto CopiedWidgetName = MakeUniqueWidgetDisplayName(Widget, Widget->GetDisplayName());
		UDreamWidget* CopiedWidget = nullptr;
		auto Parent = Widget->GetParent();
		if (Parent)
		{
			Parent->Modify();
		}
		// One deep copy of the whole subtree; the sub-prefab bookkeeping either side of it had no
		// subject left, since a class-model widget has no prefab helper for that branch to find.
		CopiedWidget = DuplicateDreamWidgetHierarchy(Widget->GetOuter(), Widget, Parent);
		if (!IsValid(CopiedWidget))
		{
			continue;
		}
		CopiedWidget->SetAsLastSibling();
		CopiedWidget->Modify();
		FDreamUIUtils::ChangePropertyWithNotify(CopiedWidget, UDreamWidget::GetPropertyName_DisplayName(), [CopiedWidget, CopiedWidgetName]()
		{
			CopiedWidget->SetDisplayName(CopiedWidgetName);
		});
		EnsureUniqueWidgetDisplayNames(FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(CopiedWidget));
		UDreamUISelection::GetInstance(World)->SelectWidget(CopiedWidget);
	}
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
}
void FDreamUIEditorTools::CopyWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	auto CopyWidgetList = FDreamUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	if (CopyWidgetList.Num() > 0)
	{
		if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(CopyWidgetList[0]))
		{
			// A designer clipboard holds duplicated TEMPLATE subtrees. The prefab-blob clipboard below
			// round-trips through a serializer that has nothing left to serialize into.
			Designer->DesignerCopyWidgets(CopyWidgetList);
			return;
		}
	}
	// Nothing below the designer route any more. The fallback here copied the selection into a
	// transient UDreamUIPrefab and pasted it back by loading the blob; a widget that no designer owns
	// is not something this command can be invoked on.
}

void FDreamUIEditorTools::PasteWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	auto ParentWidget = SelectedWidgets[0];
	if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(ParentWidget))
	{
		const FScopedTransaction Transaction(LOCTEXT("PasteWidget_Transaction", "DreamUI Paste Widgets"));
		const TArray<UDreamWidget*> Pasted = Designer->DesignerPasteWidgets(ParentWidget);
		if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(ParentWidget->GetWorld()))
		{
			Selection->SelectNone();
			for (UDreamWidget* Widget : Pasted)
			{
				Selection->SelectWidget(Widget);
			}
		}
		return;
	}
}

TArray<FText> FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(UDreamUIBehaviour* InCompanionBehaviour, const TArray<UDreamWidget*>& InWidgets)
{
	TArray<FText> Result;
	if (!IsValid(InCompanionBehaviour))return Result;

	// Everything the delete takes with it, mapped back to the widget whose name the designer will
	// recognize. Descendants count: deleting a parent deletes them, and a variable bound to one
	// dangles exactly the same. A variable may hold the widget, its visual, or one of its
	// behaviours, so all three are keys.
	TMap<const UObject*, const UDreamWidget*> DoomedObjects;
	for (UDreamWidget* Widget : InWidgets)
	{
		if (!IsValid(Widget))continue;
		TArray<UDreamWidget*> Subtree;
		UDreamWidget::CollectChildrenWidgets(Widget, Subtree);
		for (const UDreamWidget* Member : Subtree)
		{
			if (!IsValid(Member))continue;
			DoomedObjects.Add(Member, Member);
			if (auto Visual = Member->GetVisual())DoomedObjects.Add(Visual, Member);
			for (auto Behaviour : Member->GetAllComponents())
			{
				if (IsValid(Behaviour))DoomedObjects.Add(Behaviour, Member);
			}
		}
	}

	// Only references the prefab writer actually persists are worth naming. It skips transient and
	// CPF_DisableEditOnInstance properties alike (DreamUIObjectReaderAndWriter.cpp), so a binding on
	// one of those already comes back empty after a save -- the delete is not what breaks it.
	for (TFieldIterator<FObjectProperty> It(InCompanionBehaviour->GetClass()); It; ++It)
	{
		FObjectProperty* Prop = *It;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnInstance))continue;
		UObject* Value = Prop->GetObjectPropertyValue_InContainer(InCompanionBehaviour);
		if (Value == nullptr)continue;
		if (const UDreamWidget* const* BoundWidget = DoomedObjects.Find(Value))
		{
			Result.Add(FText::Format(LOCTEXT("BehaviourBinding", "{0} -> {1}")
				, FText::FromString(Prop->GetName()), FText::FromString((*BoundWidget)->GetDisplayName())));
		}
	}
	return Result;
}
UDreamUIBehaviour* FDreamUIEditorTools::FindCompanionForWidgets(const TArray<UDreamWidget*>& InWidgets)
{
	// The companion was the behaviour a PREFAB carried its graph in, one per prefab. A widget class is
	// its own Blueprint and has no companion beside it, so there is nothing here to find.
	return nullptr;
}
bool FDreamUIEditorTools::ShouldContinueDeleteOperation(const TArray<UDreamWidget*>& InWidgets)
{
	if (InWidgets.Num() > 0)
	{
		if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(InWidgets[0]))
		{
			// A class has no companion behaviour blueprint, so the prefab question below answers
			// "nothing is bound" here no matter what the graphs do. What breaks instead is the
			// compiler's own variables: the nodes reading them stop compiling. Saying so now is the
			// whole point -- otherwise it surfaces as an error on the next compile.
			const TArray<FText> References = Designer->CollectGraphReferencesToWidgets(InWidgets);
			if (References.Num() == 0)return true;
			const FText GraphMessage = FText::Format(
				LOCTEXT("ConfirmDeleteReferencedWidgets", "One or more widgets are used by this Blueprint's graphs. Deleting them removes the variables those nodes read, and the Blueprint will not compile until the nodes are fixed. Delete anyway?\n\n{0}")
				, FText::Join(FText::FromString(TEXT("\n")), References));
			return FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::Yes, GraphMessage
				, LOCTEXT("ConfirmDeleteReferencedWidgetsTitle", "Delete Widgets")) == EAppReturnType::Yes;
		}
	}
	const TArray<FText> Bindings = CollectBehaviourBindingsToWidgets(FindCompanionForWidgets(InWidgets), InWidgets);
	if (Bindings.Num() == 0)return true;
	const FText Message = FText::Format(
		LOCTEXT("ConfirmDeleteBoundWidgets", "One or more widgets are bound to variables on this prefab's behaviour blueprint. Deleting them leaves those variables holding nothing. The blueprint still compiles, so the loss only shows up at runtime. Delete anyway?\n\n{0}")
		, FText::Join(FText::FromString(TEXT("\n")), Bindings));
	// A run with nobody to ask must not be answered with the YesNo default, which is No: that would
	// silently cancel the delete and log the unanswered prompt at Error verbosity (MessageDialog.cpp).
	return FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::Yes, Message
		, LOCTEXT("ConfirmDeleteBoundWidgetsTitle", "Delete Widgets")) == EAppReturnType::Yes;
}
void FDreamUIEditorTools::DeleteWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction, EDeleteWidgetWarningType WarningType)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	auto RootWidgetList = FDreamUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	if (WarningType == EDeleteWidgetWarningType::WarnAndAskUser && !ShouldContinueDeleteOperation(RootWidgetList))return;
	const FScopedTransaction Transaction(LOCTEXT("DestroyWidget_Transaction", "DreamUI Destroy Widgets"));
	if (RootWidgetList.Num() > 0)
	{
		if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(RootWidgetList[0]))
		{
			Designer->DesignerDeleteWidgets(RootWidgetList);
			return;
		}
	}
	for (auto Widget : RootWidgetList)
	{
		Widget->GetOuter()->Modify();
		Widget->SetFlags(RF_Public | RF_Transactional);
		Widget->Modify();
		if (auto Parent = Widget->GetParent())
		{
			Parent->SetFlags(RF_Public | RF_Transactional);
			Parent->Modify();
		}
		Widget->SetParent(nullptr);
		Widget->DestroyWidget();
		Widget->MarkPackageDirty();
	}
}
void FDreamUIEditorTools::CutWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	// Cut is a delete as far as the behaviour blueprint is concerned -- the pasted widget is a new
	// object, so the variable is left pointing at nothing either way. Asked before the copy, because
	// the copy overwrites the clipboard: declining afterwards leaves Ctrl+X having thrown away
	// whatever was staged there and deleted nothing.
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (!ShouldContinueDeleteOperation(GetRootWidgetListFromSelection(SelectedWidgets)))return;
	CopyWidgets(GetSelectedWidgetArrayFunction);
	DeleteWidgets(GetSelectedWidgetArrayFunction, EDeleteWidgetWarningType::DeleteSilently);
}

bool FDreamUIEditorTools::CanDuplicateWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() <= 0)return false;
	TMap<UDreamWidget*, int32> AdditionalChildrenByParent;
	for (UDreamWidget* Widget : GetRootWidgetListFromSelection(SelectedWidgets))
	{
		if (IsValid(Widget) && IsValid(Widget->GetParent()))
		{
			++AdditionalChildrenByParent.FindOrAdd(Widget->GetParent());
		}
	}
	for (const TPair<UDreamWidget*, int32>& Pair : AdditionalChildrenByParent)
	{
		if (!Pair.Key->CanAcceptAdditionalChildren(Pair.Value))return false;
	}
	return true;
}
bool FDreamUIEditorTools::CanCopyWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() <= 0)return false;
	return true;
}
bool FDreamUIEditorTools::CanPasteWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!FDreamUIEditorTools::IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	if (FDreamWidgetBlueprintEditor::FindDesignerForWidget(SelectedWidget) != nullptr)
	{
		return FDreamWidgetBlueprintEditor::DesignerHasClipboardContent()
			&& SelectedWidget->CanAcceptAdditionalChildren(1);
	}
	// Outside a designer there is no clipboard: the only one is the designer's own.
	return false;
}
bool FDreamUIEditorTools::CanCutWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	return CanDeleteWidget(GetSelectedWidgetArrayFunction);
}
bool FDreamUIEditorTools::CanDeleteWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)return false;
	// The refusal this used to make was "you cannot delete a widget inside a sub-prefab instance".
	// A class model has no sub-prefab instances, so nothing here is off limits.
	return true;
}

bool FDreamUIEditorTools::CanCheckNestedOverrideParameter(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	// Override parameters belonged to sub-prefab instances. There are none.
	return false;
}

bool FDreamUIEditorTools::CanCreateWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	return true;
}

bool FDreamUIEditorTools::IsWidgetCompatibleWithDreamUIToolsMenu(UDreamWidget* InWidget)
{
	if (!FDreamWidgetBlueprintEditor::WidgetIsRootAgent(InWidget))
	{
		return true;
	}
	// The design canvas is not something anyone can parent to in a level, which is what this refusal
	// is for. In a DESIGNER it is what "no particular selection" resolves to -- the palette's
	// empty-selection fallback hands it over by design -- and DesignerCreateWidget retargets it to
	// the authored root. Refusing it here made a palette double-click with the canvas row selected
	// do nothing at all except log, which reads as a dead panel.
	return FDreamWidgetBlueprintEditor::FindDesignerForWidget(InWidget) != nullptr;
}


#undef LOCTEXT_NAMESPACE
