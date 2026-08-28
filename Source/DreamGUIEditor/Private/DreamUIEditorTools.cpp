// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIEditorTools.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
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
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "DreamGUIEditorModule.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
#include "PrefabEditor/DreamUIPrefabBehaviourUtils.h"
#include "Core/DreamUIBehaviour.h"
#include "UObject/UnrealType.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "PrefabSystem/DreamUIPrefabPresenterComponent.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "DreamGUIEditorTools"


FEditingPrefabChangedDelegate FDreamUIEditorTools::OnEditingPrefabChanged;
FBeforeApplyPrefabDelegate FDreamUIEditorTools::OnBeforeApplyPrefab;

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
		if (UDreamUIPrefabHelperObject* Helper = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ContextWidget))
		{
			if (IsValid(Helper->LoadedRootWidget))
			{
				return Helper->LoadedRootWidget;
			}
		}
		return ContextWidget->GetRootWidgetInHierarchy();
	}
};

TArray<FDreamUIEditorTools::FCopiedWidgetPrefab> FDreamUIEditorTools::CopiedWidgetPrefabList;


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
	UDreamUIPrefabHelperObject* ManagingHelper =
		UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(RootWidget);
	TSet<FName> UsedNames;
	int32 RenameCount = 0;
	for (UDreamWidget* Widget : Widgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		if (ManagingHelper
			&& ManagingHelper->IsWidgetBelongsToSubPrefab(Widget)
			&& !ManagingHelper->IsSubPrefabRootWidget(Widget))
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
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
		{
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetAnythingDirty();
		}
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

UDreamWidget* FDreamUIEditorTools::CreateUIControlsAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString InControlClassPath, TFunction<void(UDreamWidget*)> Callback)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot create control '%s': no parent widget is selected."), *InControlClassPath);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot create control '%s': widget '%s' is not a valid parent."), *InControlClassPath, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(SelectedWidget))
	{
		UClass* ControlClass = LoadClass<UDreamUserWidget>(nullptr, *InControlClassPath);
		if (ControlClass == nullptr)
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("Cannot create control: '%s' is not a loadable hierarchy class."), *InControlClassPath);
			return nullptr;
		}
		// A control IS a class now, so placing one is creating a widget of that class -- its contents
		// come from its own class when the preview instances it.
		UDreamWidget* Created = Designer->DesignerCreateWidget(SelectedWidget, ControlClass,
			ControlClass->GetName(), [Callback](UDreamWidget* InTemplate)
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
	UBlueprint* ControlBlueprint = LoadObject<UBlueprint>(nullptr, *(InControlClassPath + TEXT(".") + FPackageName::GetShortName(InControlClassPath)));
	UClass* ControlClass = ControlBlueprint != nullptr ? ControlBlueprint->GeneratedClass.Get() : nullptr;
	if (ControlClass == nullptr || !ControlClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Load control class error! Path:%s. Missing some content of the DreamUI plugin; reinstalling it may fix this."), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InControlClassPath);
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateUIControl_Transaction", "DreamUI Create UI Control"));
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	ModifyForHierarchyChange(SelectedWidget);
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		PrefabHelperObject->Modify();
		PrefabHelperObject->SetAnythingDirty();
	}

	UDreamWidget* CreatedWidget = CreateDreamWidget(SelectedWidget->GetWorld(), ControlClass, SelectedWidget);
	if (!IsValid(CreatedWidget))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Control class '%s' produced no widget."), *InControlClassPath);
		return nullptr;
	}
	if (Callback)Callback(CreatedWidget);
	EnsureUniqueWidgetDisplayNames(FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(CreatedWidget));
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->SelectNone();
	UDreamUISelection::GetInstance(SelectedWidget->GetWorld())->SelectWidget(CreatedWidget);
	return CreatedWidget;
}

bool FDreamUIEditorTools::CanNestPrefabUnderWidget(UDreamUIPrefab* InPrefab, UDreamWidget* InParentWidget, FText& OutError)
{
	if (!IsValid(InPrefab))
	{
		OutError = LOCTEXT("Nest_MissingPrefab", "The prefab asset could not be loaded.");
		return false;
	}
	if (InPrefab->PrefabVersion <= (uint16)EDreamUIPrefabVersion::OldVersion)
	{
		OutError = LOCTEXT("Nest_UnsupportOldPrefabVersion", "Target prefab's version is too old! Please make it newer: open the prefab and hit \"Save\" button.");
		return false;
	}
	// The prefab the parent belongs to is the one being edited. Putting it inside itself -- directly,
	// or through a prefab that already contains it -- is what makes Apply bake a second copy in, so
	// every repeat doubles the asset.
	auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(InParentWidget);
	if (!IsValid(PrefabHelperObject) || !IsValid(PrefabHelperObject->PrefabAsset))return true;
	if (PrefabHelperObject->PrefabAsset == InPrefab)
	{
		OutError = LOCTEXT("Nest_SelfPrefabAsSubPrefab", "Target prefab is the one being edited; self cannot be self's child!");
		return false;
	}
	if (MayReferencePackage(InPrefab->GetOutermost()->GetFName(), PrefabHelperObject->PrefabAsset->GetOutermost()->GetFName())
		&& InPrefab->IsPrefabBelongsToThisSubPrefab(PrefabHelperObject->PrefabAsset, true))
	{
		OutError = LOCTEXT("Nest_EndlessNestedPrefab", "Target prefab has this prefab as a child prefab, which will result in cyclic nested prefab!");
		return false;
	}
	return true;
}

UDreamWidget* FDreamUIEditorTools::CreateSubPrefabAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString InPrefabPath, TFunction<void(UDreamWidget*)> Callback)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot add sub prefab '%s': no parent widget is selected."), *InPrefabPath);
		return nullptr;
	}
	// The one create path that is NOT routed to the authoring tree, deliberately: a prefab instance
	// carries override tracking and a link back to its source, none of which a widget class has
	// anywhere to keep. Building one on the preview would look like it worked until the next
	// rebuild, so this refuses and says what to do instead.
	if (FDreamWidgetBlueprintEditor::FindDesignerForWidget(SelectedWidget) != nullptr)
	{
		FNotificationInfo Info(LOCTEXT("SubPrefabInDesignerRefused",
			"A Widget Blueprint cannot nest a DreamUI prefab. Convert the prefab to a Widget Blueprint first, then place that."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 8.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		UE_LOG(DreamGUIEditor, Warning,
			TEXT("Refused to nest prefab '%s' in a Widget Blueprint: convert it to a Widget Blueprint first."), *InPrefabPath);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Cannot add sub prefab '%s': widget '%s' is not a valid parent."), *InPrefabPath, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Widget '%s' cannot accept another child prefab."), *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	auto Prefab = LoadObject<UDreamUIPrefab>(NULL, *InPrefabPath);
	if (Prefab == nullptr)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Load prefab error! Path:%s"), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InPrefabPath);
		return nullptr;
	}
	FText NestError;
	if (!CanNestPrefabUnderWidget(Prefab, SelectedWidget, NestError))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Cannot add sub prefab '%s': %s"), *InPrefabPath, *NestError.ToString());
		return nullptr;
	}
	// Without a helper object there is nowhere to record the link, and recording it is the entire
	// difference between this and CreateUIControlsAndReturn.
	auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
	if (!IsValid(PrefabHelperObject))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Cannot add sub prefab '%s': widget '%s' does not belong to a prefab."), *InPrefabPath, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("CreateSubPrefab_Transaction", "DreamUI Create Sub Prefab"));
	auto World = SelectedWidget->GetWorld();
	UDreamUISelection::GetInstance(World)->Modify();
	ModifyForHierarchyChange(SelectedWidget);
	PrefabHelperObject->Modify();
	PrefabHelperObject->SetAnythingDirty();
	TMap<FGuid, TObjectPtr<UObject>> SubPrefabMapGuidToObject;
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubSubPrefabMap;
	auto CreatedWidget = Prefab->LoadPrefabWithExistingObjects(World
		, SelectedWidget->GetOuter()
		, SelectedWidget
		, SubPrefabMapGuidToObject, SubSubPrefabMap);
	if (!IsValid(CreatedWidget))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Sub prefab '%s' produced no root widget."), *InPrefabPath);
		return nullptr;
	}
	PrefabHelperObject->MakePrefabAsSubPrefab(Prefab, CreatedWidget, SubPrefabMapGuidToObject, {});
	if (Callback)Callback(CreatedWidget);
	EnsureUniqueWidgetDisplayNames(FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(CreatedWidget));
	UDreamUISelection::GetInstance(World)->SelectNone();
	UDreamUISelection::GetInstance(World)->SelectWidget(CreatedWidget);
	return CreatedWidget;
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
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> DuplicatedSubPrefabMap;
		TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
		TMap<UObject*, FGuid> InMapObjectToGuid;
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			PrefabHelperObject->CleanupInvalidSubPrefab();//do cleanup before everything else
			PrefabHelperObject->Modify();
			struct LOCAL {
				static void CollectSubPrefabWidgets(UDreamWidget* InWidget, const TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap, TArray<UDreamWidget*>& OutSubPrefabRootWidgets)
				{
					if (InSubPrefabMap.Contains(InWidget))
					{
						OutSubPrefabRootWidgets.Add(InWidget);
					}
					else
					{
						for (auto& Child : InWidget->GetChildren())
						{
							CollectSubPrefabWidgets(Child, InSubPrefabMap, OutSubPrefabRootWidgets);
						}
					}
				}
			};
			TArray<UDreamWidget*> SubPrefabRootWidgets;
			LOCAL::CollectSubPrefabWidgets(Widget, PrefabHelperObject->SubPrefabMap, SubPrefabRootWidgets);//collect sub prefabs that is attached to this Widget
			for (auto& SubPrefabKeyValue : PrefabHelperObject->SubPrefabMap)//generate MapObjectToGuid
			{
				auto SubPrefabRootWidget = SubPrefabKeyValue.Key;
				if (SubPrefabRootWidgets.Contains(SubPrefabRootWidget))
				{
					auto& SubPrefabData = SubPrefabKeyValue.Value;
					PrefabHelperObject->RefreshOnSubPrefabDirty(SubPrefabData.PrefabAsset, SubPrefabRootWidget);//need to update subprefab to latest before duplicate
					auto FindObjectGuidInParentPrefab = [&](FGuid InGuidInSubPrefab) {
						for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
						{
							if (KeyValue.Value == InGuidInSubPrefab)
							{
								return KeyValue.Key;
							}
						}
						UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
						FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
						return FGuid::NewGuid();
					};
					for (auto& MapGuidToObjectKeyValue : SubPrefabData.MapGuidToObject)
					{
						InMapObjectToGuid.Add(MapGuidToObjectKeyValue.Value, FindObjectGuidInParentPrefab(MapGuidToObjectKeyValue.Key));
					}
				}
			}
			CopiedWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::DuplicateWidgetForEditor(Widget->GetWorld(), Widget, Parent, PrefabHelperObject->SubPrefabMap, InMapObjectToGuid, DuplicatedSubPrefabMap, OutMapGuidToObject);
			CopiedWidget->SetAsLastSibling();
			for (auto& KeyValue : DuplicatedSubPrefabMap)
			{
				TMap<FGuid, TObjectPtr<UObject>> SubMapGuidToObject;
				for (auto& MapGuidItem : KeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab)
				{
					SubMapGuidToObject.Add(MapGuidItem.Value, OutMapGuidToObject[MapGuidItem.Key]);
				}
				PrefabHelperObject->MakePrefabAsSubPrefab(KeyValue.Value.PrefabAsset, KeyValue.Key, SubMapGuidToObject, KeyValue.Value.ObjectOverrideParameterArray);
			}
		}
		else 
		{
			CopiedWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::DuplicateWidgetForEditor(Widget->GetWorld(), Widget, Parent, {}, InMapObjectToGuid, DuplicatedSubPrefabMap, OutMapGuidToObject);
		}
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
	for (auto& CopiedItem : CopiedWidgetPrefabList)
	{
		if (CopiedItem.Prefab.IsValid())
		{
			CopiedItem.Prefab->RemoveFromRoot();
			CopiedItem.Prefab->ConditionalBeginDestroy();
		}
	}
	auto CopyWidgetList = FDreamUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	CopiedWidgetPrefabList.Reset();
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
	for (auto Widget : CopyWidgetList)
	{
		auto Prefab = NewObject<UDreamUIPrefab>();
		Prefab->AddToRoot();
		TMap<UObject*, FGuid> MapObjectToGuid;
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			SubPrefabMap = PrefabHelperObject->SubPrefabMap;

			if (PrefabHelperObject->CleanupInvalidSubPrefab())//do cleanup before everything else
			{
				PrefabHelperObject->Modify();
			}
			struct LOCAL {
				static void CollectSubPrefabWidgets(UDreamWidget* InWidget, const TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap, TArray<UDreamWidget*>& OutSubPrefabRootWidgets)
				{
					if (InSubPrefabMap.Contains(InWidget))
					{
						OutSubPrefabRootWidgets.Add(InWidget);
					}
					else
					{
						for (auto& ChildWidget : InWidget->GetChildren())
						{
							CollectSubPrefabWidgets(ChildWidget, InSubPrefabMap, OutSubPrefabRootWidgets);
						}
					}
				}
			};
			TArray<UDreamWidget*> SubPrefabRootWidgets;
			LOCAL::CollectSubPrefabWidgets(Widget, PrefabHelperObject->SubPrefabMap, SubPrefabRootWidgets);//collect sub prefabs that is attached to this Widget
			for (auto& SubPrefabKeyValue : PrefabHelperObject->SubPrefabMap)//generate MapObjectToGuid
			{
				auto SubPrefabRootWidget = SubPrefabKeyValue.Key;
				if (SubPrefabRootWidgets.Contains(SubPrefabRootWidget))
				{
					auto& SubPrefabData = SubPrefabKeyValue.Value;
					PrefabHelperObject->RefreshOnSubPrefabDirty(SubPrefabData.PrefabAsset, SubPrefabRootWidget);//need to update subprefab to latest before duplicate
					auto FindObjectGuidInParentPrefab = [&](FGuid InGuidInSubPrefab) {
						for (auto& KeyValue : SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
						{
							if (KeyValue.Value == InGuidInSubPrefab)
							{
								return KeyValue.Key;
							}
						}
						UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
						FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
						return FGuid::NewGuid();
					};
					for (auto& MapGuidToObjectKeyValue : SubPrefabData.MapGuidToObject)
					{
						MapObjectToGuid.Add(MapGuidToObjectKeyValue.Value, FindObjectGuidInParentPrefab(MapGuidToObjectKeyValue.Key));
					}
				}
			}
		}

		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> TempSubPrefabMap;
		for (auto& SubPrefabKeyValue : SubPrefabMap)
		{
			if (SubPrefabKeyValue.Key->IsChildOf(Widget) || SubPrefabKeyValue.Key == Widget)
			{
				TempSubPrefabMap = SubPrefabMap;
				break;
			}
		}
		Prefab->SavePrefab(Widget, MapObjectToGuid, TempSubPrefabMap);
		CopiedWidgetPrefabList.Add({ Widget->GetDisplayName(), Prefab });
	}
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
	auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ParentWidget);
	if (PrefabHelperObject == nullptr)return;
	int32 PasteCount = 0;
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		PasteCount += CopiedItem.Prefab.IsValid() ? 1 : 0;
	}
	if (!ParentWidget->CanAcceptAdditionalChildren(PasteCount))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("Widget '%s' cannot accept %d pasted child widget(s)."),
			*ParentWidget->GetDisplayName(), PasteCount);
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("PasteWidget_Transaction", "DreamUI Paste Widgets"));
	auto World = ParentWidget->GetWorld();
	UDreamUISelection::GetInstance(World)->Modify();
	ModifyForHierarchyChange(ParentWidget);
	if (IsValid(PrefabHelperObject))PrefabHelperObject->Modify();
	UDreamUISelection::GetInstance(World)->SelectNone();
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		if (CopiedItem.Prefab.IsValid())
		{
			TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
			TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> LoadedSubPrefabMap;
			auto CopiedWidgetName = MakeUniqueWidgetDisplayName(ParentWidget, CopiedItem.DisplayName);
			auto CopiedWidget = CopiedItem.Prefab->LoadPrefabInEditor(ParentWidget->GetWorld(), ParentWidget->GetOuter(), ParentWidget, LoadedSubPrefabMap, OutMapGuidToObject, false);
			for (auto& KeyValue : LoadedSubPrefabMap)
			{
				TMap<FGuid, TObjectPtr<UObject>> SubMapGuidToObject;
				for (auto& MapGuidItem : KeyValue.Value.MapObjectGuidFromParentPrefabToSubPrefab)
				{
					SubMapGuidToObject.Add(MapGuidItem.Value, OutMapGuidToObject[MapGuidItem.Key]);
				}
				PrefabHelperObject->MakePrefabAsSubPrefab(KeyValue.Value.PrefabAsset, KeyValue.Key, SubMapGuidToObject, KeyValue.Value.ObjectOverrideParameterArray);
			}
			CopiedWidget->Modify();
			FDreamUIUtils::ChangePropertyWithNotify(CopiedWidget, UDreamWidget::GetPropertyName_DisplayName(), [CopiedWidget, CopiedWidgetName]()
			{
				CopiedWidget->SetDisplayName(CopiedWidgetName);
			});
			EnsureUniqueWidgetDisplayNames(FDreamUIEditorToolsHelperFunctionHolder::GetNamingRoot(CopiedWidget));
			PrefabHelperObject->SetAnythingDirty();
			UDreamUISelection::GetInstance(World)->SelectWidget(CopiedWidget);
		}
		else
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("Source copied widget is missing!"));
		}
	}
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
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
	// The companion is per prefab and a selection cannot span two prefabs, so the first widget
	// that reports a helper names the prefab whose bindings are at stake.
	for (UDreamWidget* Widget : InWidgets)
	{
		if (!IsValid(Widget))continue;
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			return DreamUIPrefabBehaviourUtils::FindBehaviourComponent(PrefabHelperObject->LoadedRootWidget, PrefabHelperObject->PrefabAsset);
		}
	}
	return nullptr;
}
bool FDreamUIEditorTools::ShouldContinueDeleteOperation(const TArray<UDreamWidget*>& InWidgets)
{
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
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetAnythingDirty();
			TArray<UDreamWidget*> ChildrenWidgets;
			UDreamWidget::CollectChildrenWidgets(Widget, ChildrenWidgets);
			for (auto ChildWidget : ChildrenWidgets)
			{
				PrefabHelperObject->RemoveSubPrefabByAnyWidgetOfSubPrefab(ChildWidget);
			}
		}
		Widget->SetParent(nullptr);
		Widget->DestroyWidget();
		Widget->MarkPackageDirty();
	}
	CleanupPrefabs();
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
	if (FDreamUIEditorTools::CopiedWidgetPrefabList.Num() == 0)return false;
	int32 PasteCount = 0;
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		PasteCount += CopiedItem.Prefab.IsValid() ? 1 : 0;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren(PasteCount))return false;
	return true;
}
bool FDreamUIEditorTools::CanCutWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	return CanDeleteWidget(GetSelectedWidgetArrayFunction);
}
bool FDreamUIEditorTools::CanDeleteWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)return false;
	for (auto Widget : SelectedWidgets)
	{
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			if (!PrefabHelperObject->IsSubPrefabRootWidget(Widget)//allowed to delete sub prefab's root Widget
				&& PrefabHelperObject->IsWidgetBelongsToSubPrefab(Widget))//not allowed to delete sub prefab's Widget
			{
				return false;
			}
		}
	}
	return true;
}

bool FDreamUIEditorTools::HaveValidCopiedWidgets()
{
	if (CopiedWidgetPrefabList.Num() == 0)return false;
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		if (!CopiedItem.Prefab.IsValid())
		{
			return false;
		}
	}
	return true;
}

bool FDreamUIEditorTools::CanCreatePrefab(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	if (SelectedWidget->HasAnyFlags(EObjectFlags::RF_Transient))return false;
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		if (PrefabHelperObject->LoadedRootWidget == SelectedWidget)
		{
			return false;
		}
		if (PrefabHelperObject->IsWidgetBelongsToSubPrefab(SelectedWidget))
		{
			return false;
		}
		else if (PrefabHelperObject->IsWidgetBelongsToMissingSubPrefab(SelectedWidget))
		{
			return false;
		}
	}
	return true;
}
FString FDreamUIEditorTools::PrevSavePrefabFolder = TEXT("");
void FDreamUIEditorTools::CreatePrefabAsset(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)//@todo: make some referenced parameter as override parameter(eg: Widget parameter reference other Widget that is not belongs to prefab hierarchy)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return;
	auto OldPrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
	if (IsValid(OldPrefabHelperObject) && OldPrefabHelperObject->LoadedRootWidget == SelectedWidget)//If create prefab from an existing prefab's root Widget, this is not allowed
	{
		auto Message = LOCTEXT("CreatePrefabError_BelongToOtherPrefab", "This Widget is a root Widget of another prefab, this is not allowed! Instead you can duplicate the prefab asset.");
		FMessageDialog::Open(EAppMsgType::Ok, Message);
		return;
	}
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFileNames;
		DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(FSlateApplication::Get().GetGameViewport()),
			TEXT("Choose a path to save prefab asset, must inside Content folder"),
			PrevSavePrefabFolder.IsEmpty() ? FPaths::ProjectContentDir() : PrevSavePrefabFolder,
			SelectedWidget->GetDisplayName() + TEXT("_Prefab"),
			TEXT("*.*"),
			EFileDialogFlags::None,
			OutFileNames
		);
		if (OutFileNames.Num() > 0)
		{
			FString selectedFilePath = OutFileNames[0];
			if (selectedFilePath.StartsWith(FPaths::ProjectDir()))
			{
				PrevSavePrefabFolder = FPaths::GetPath(selectedFilePath);
				if (FPaths::FileExists(selectedFilePath + TEXT(".uasset")))
				{
					auto returnValue = FMessageDialog::Open(EAppMsgType::YesNo
						, FText::Format(LOCTEXT("Error_AssetAlreadyExist", "Asset already exist at path: \"{0}\" !\nReplace it?"), FText::FromString(selectedFilePath)));
					if (returnValue != EAppReturnType::Yes)
					{
						return;
					}
				}
				selectedFilePath.RemoveFromStart(FPaths::ProjectContentDir(), ESearchCase::CaseSensitive);
				FString packageName = TEXT("/Game/") + selectedFilePath;
				UPackage* package = CreatePackage(*packageName);
				if (package == nullptr)
				{
					FMessageDialog::Open(EAppMsgType::Ok
						, LOCTEXT("Error_NotValidPathForSavePrefab", "Selected path not valid, please choose another path to save prefab."));
					return;
				}
				package->FullyLoad();
				FString fileName = FPaths::GetBaseFilename(selectedFilePath);
				auto OutPrefab = NewObject<UDreamUIPrefab>(package, UDreamUIPrefab::StaticClass(), *fileName, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
				FAssetRegistryModule::AssetCreated(OutPrefab);

				auto PrefabHelperObjectWhichManageThisWidget = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
				check(PrefabHelperObjectWhichManageThisWidget != nullptr)
				{
					struct LOCAL
					{
						static auto Make_MapGuidFromParentToSub(const TMap<UObject*, FGuid>& InNewParentMapObjectToGuid, UDreamUIPrefabHelperObject* InPrefabHelperObject, const FDreamUISubPrefabData& InOriginSubPrefabData)
						{
							TMap<FGuid, FGuid> Result;
							for (auto& KeyValue : InOriginSubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab)
							{
								auto Object = InPrefabHelperObject->MapGuidToObject[KeyValue.Key];
								if (IsValid(Object))
								{
									auto Guid = InNewParentMapObjectToGuid[Object];
									if (!Result.Contains(Guid))
									{
										Result.Add(Guid, KeyValue.Value);
									}
								}
							}
							return Result;
						}
						static void CollectSubPrefab(UDreamWidget* InWidget, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InOutSubPrefabMap, UDreamUIPrefabHelperObject* InPrefabHelperObject, const TMap<UObject*, FGuid>& InMapObjectToGuid)
						{
							if (InPrefabHelperObject->IsWidgetBelongsToSubPrefab(InWidget))
							{
								auto OriginSubPrefabData = InPrefabHelperObject->GetSubPrefabData(InWidget);
								FDreamUISubPrefabData SubPrefabData;
								SubPrefabData.PrefabAsset = OriginSubPrefabData.PrefabAsset;
								SubPrefabData.ObjectOverrideParameterArray = OriginSubPrefabData.ObjectOverrideParameterArray;
								SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab = Make_MapGuidFromParentToSub(InMapObjectToGuid, InPrefabHelperObject, OriginSubPrefabData);
								InOutSubPrefabMap.Add(InWidget, SubPrefabData);
								return;
							}
							for (auto& ChildWidget : InWidget->GetChildren())
							{
								CollectSubPrefab(ChildWidget, InOutSubPrefabMap, InPrefabHelperObject, InMapObjectToGuid);//collect all Widget, include subprefab's Widget
							}
						}
					};
					TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
					TMap<UObject*, FGuid> MapObjectToGuid;
					OutPrefab->SavePrefab(SelectedWidget, MapObjectToGuid, SubPrefabMap);//save prefab first step, just collect guid and sub prefab
					LOCAL::CollectSubPrefab(SelectedWidget, SubPrefabMap, PrefabHelperObjectWhichManageThisWidget, MapObjectToGuid);
					for (auto& KeyValue : SubPrefabMap)
					{
						PrefabHelperObjectWhichManageThisWidget->RemoveSubPrefabByAnyWidgetOfSubPrefab(KeyValue.Key);//remove prefab from origin PrefabHelperObject
					}
					OutPrefab->SavePrefab(SelectedWidget, MapObjectToGuid, SubPrefabMap);//save prefab second step, store sub prefab data
					OutPrefab->EnsureInstanceObjects();

					//make it as sub-prefab
					TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
					for (auto KeyValue : MapObjectToGuid)
					{
						MapGuidToObject.Add(KeyValue.Value, KeyValue.Key);
					}
					PrefabHelperObjectWhichManageThisWidget->MakePrefabAsSubPrefab(OutPrefab, SelectedWidget, MapGuidToObject, {});
				}
				CleanupPrefabs();
			}
			else
			{
				FMessageDialog::Open(EAppMsgType::Ok
					, LOCTEXT("Error_PrefabSaveLocation", "Prefab should only save inside Content folder!"));
			}
		}
	}
}

void FDreamUIEditorTools::RefreshLoadedPrefab()
{
	for (TObjectIterator<UDreamUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CheckPrefabVersion();
	}
	// Presenters hold a class now, and recompiling a Blueprint reinstances its objects, so there is
	// no hand-rolled version check left to run over them.
}

void FDreamUIEditorTools::RefreshOpenedPrefabEditor(UDreamUIPrefab* InPrefab)
{
	// Nothing to do: a prefab has no editor to reopen. It used to close and reopen the prefab
	// editor so the live copy would pick up what had changed on disk, which is a problem that only
	// exists when the editor holds a copy at all.
}

void FDreamUIEditorTools::RefreshOnSubPrefabChange(UDreamUIPrefab* InSubPrefab)
{
	auto AllPrefabs = GetAllPrefabArray();

	struct Local
	{
	public:
		static void RefreshAllPrefabsOnSubPrefabChange(
			const TArray<UDreamUIPrefab*>& InPrefabs,
			UDreamUIPrefab* InSubPrefab,
			TSet<const UDreamUIPrefab*>& VisitedPrefabs)
		{
			if (!IsValid(InSubPrefab) || VisitedPrefabs.Contains(InSubPrefab))return;
			VisitedPrefabs.Add(InSubPrefab);
			for (auto& Prefab : InPrefabs)
			{
				if (Prefab->IsPrefabBelongsToThisSubPrefab(InSubPrefab, false))
				{
					//check if is opened by prefab editor
					RefreshAllPrefabsOnSubPrefabChange(InPrefabs, Prefab, VisitedPrefabs);
				}
			}
		}
	};

	TSet<const UDreamUIPrefab*> VisitedPrefabs;
	Local::RefreshAllPrefabsOnSubPrefabChange(AllPrefabs, InSubPrefab, VisitedPrefabs);
}

TArray<UDreamUIPrefab*> FDreamUIEditorTools::GetAllPrefabArray()
{
#if 0//Why disable? Because we don't need to refresh not-loaded prefab, because prefab will reload all sub prefab when load
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Need to do this if running in the editor with -game to make sure that the assets in the following path are available
	TArray<FString> PathsToScan;
	PathsToScan.Add(TEXT("/Game/"));
	AssetRegistry.ScanPathsSynchronous(PathsToScan);

	// Get asset in path
	TArray<FAssetData> ScriptAssetList;
	AssetRegistry.GetAssetsByPath(FName("/Game/"), ScriptAssetList, /*bRecursive=*/true);

	TArray<UDreamUIPrefab*> AllPrefabs;
	auto PrefabClassName = UDreamUIPrefab::StaticClass()->GetClassPathName();
	// Ensure all assets are loaded
	for (const FAssetData& Asset : ScriptAssetList)
	{
		// Gets the loaded asset, loads it if necessary
		if (Asset.AssetClassPath == PrefabClassName)
		{
			auto AssetObject = Asset.GetAsset();
			if (auto Prefab = Cast<UDreamUIPrefab>(AssetObject))
			{
				Prefab->MakeAgentObjectsInPreviewWorld();
				AllPrefabs.Add(Prefab);
			}
		}
	}
#else
	TArray<UDreamUIPrefab*> AllPrefabs;
#endif
	//collect prefabs that are not saved to disc yet
	for (TObjectIterator<UDreamUIPrefab> Itr; Itr; ++Itr)
	{
		if (!AllPrefabs.Contains(*Itr))
		{
			AllPrefabs.Add(*Itr);
		}
	}
	return AllPrefabs;
}

bool FDreamUIEditorTools::CanUnpackWidgetForPrefab(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedWidget))
		{
			return true;
		}
		else if (PrefabHelperObject->MissingPrefab.Contains(SelectedWidget))
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}
void FDreamUIEditorTools::UnpackPrefab(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{	
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return;
	const FScopedTransaction Transaction(LOCTEXT("UnpackPrefab_Transaction", "DreamUI UnpackPrefab"));
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		SelectedWidget->GetWorld()->Modify();
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedWidget) || PrefabHelperObject->MissingPrefab.Contains(SelectedWidget));//should already filtered by menu
		PrefabHelperObject->Modify();
		PrefabHelperObject->RemoveSubPrefabByRootWidget(SelectedWidget);//the SelectedWidget must be root Widget, should already filtered by menu
	}
	CleanupPrefabs();
}

void FDreamUIEditorTools::SelectPrefabAsset(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return;
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedWidget));//should have being checked in Browse button
		auto PrefabAsset = PrefabHelperObject->GetSubPrefabAsset(SelectedWidget);
		if (IsValid(PrefabAsset))
		{
			TArray<UObject*> ObjectsToSync;
			ObjectsToSync.Add(PrefabAsset);
			GEditor->SyncBrowserToObjects(ObjectsToSync);
		}
	}
}
bool FDreamUIEditorTools::CanBrowsePrefabAsset(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedWidget))
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}

void FDreamUIEditorTools::OpenPrefabAsset(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return;
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedWidget));//should have being check in menu
		auto PrefabAsset = PrefabHelperObject->GetSubPrefabAsset(SelectedWidget);
		if (IsValid(PrefabAsset))
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(PrefabAsset);
		}
	}
}

bool FDreamUIEditorTools::CanCheckPrefabOverrideParameter(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
		{
			if (KeyValue.Key == SelectedWidget || SelectedWidget->IsChildOf(KeyValue.Key))
			{
				return true;
			}
		}
		return false;
	}
	else
	{
		return false;
	}
}

bool FDreamUIEditorTools::CanCreateWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithDreamUIToolsMenu(SelectedWidget))return false;
	return true;
}

void FDreamUIEditorTools::CleanupPrefabs()
{
	for (TObjectIterator<UDreamUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CleanupInvalidSubPrefab();
	}
}

bool FDreamUIEditorTools::IsWidgetCompatibleWithDreamUIToolsMenu(UDreamWidget* InWidget)
{
	if (!FDreamWidgetBlueprintEditor::WidgetIsRootAgent(InWidget))
	{
		return true;
	}
	return false;
}


#undef LOCTEXT_NAMESPACE
