// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "LexUIEditorTools.h"
#include "LexUIControlRegistry.h"
#include "Core/LexUIManager.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Widgets/SViewport.h"
#include "Engine/Selection.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "LGUIEditorModule.h"
#include "PrefabEditor/LexUIPrefabEditor.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "PrefabSystem/LexUIPrefabPresenterComponent.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LGUIEditorTools"


FEditingPrefabChangedDelegate FLexUIEditorTools::OnEditingPrefabChanged;
FBeforeApplyPrefabDelegate FLexUIEditorTools::OnBeforeApplyPrefab;

struct FLexUIEditorToolsHelperFunctionHolder
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

	static ULexWidget* GetNamingRoot(ULexWidget* ContextWidget)
	{
		if (!IsValid(ContextWidget))
		{
			return nullptr;
		}
		if (ULexUIPrefabHelperObject* Helper = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ContextWidget))
		{
			if (IsValid(Helper->LoadedRootWidget))
			{
				return Helper->LoadedRootWidget;
			}
		}
		return ContextWidget->GetRootWidgetInHierarchy();
	}
};

TArray<FLexUIEditorTools::FCopiedWidgetPrefab> FLexUIEditorTools::CopiedWidgetPrefabList;

FString FLexUIEditorTools::LexUIPresetPrefabPath = TEXT("/LGUI/Prefabs/");

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
	void ModifyForHierarchyChange(ULexWidget* InParent, ULexWidget* InChild = nullptr)
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
	 * Asking the loaded prefab whether it nests another one is not free -- ULexUIPrefab answers it by
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

FString FLexUIEditorTools::MakeUniqueWidgetDisplayName(
	ULexWidget* ContextWidget,
	const FString& DesiredName,
	const ULexWidget* WidgetToIgnore)
{
	TSet<FName> UsedNames;
	if (ULexWidget* RootWidget = FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(ContextWidget))
	{
		TArray<ULexWidget*> Widgets;
		ULexWidget::CollectChildrenWidgets(RootWidget, Widgets);
		for (const ULexWidget* Widget : Widgets)
		{
			if (IsValid(Widget) && Widget != WidgetToIgnore)
			{
				UsedNames.Add(FName(*Widget->GetDisplayName()));
			}
		}
	}
	return FLexUIEditorToolsHelperFunctionHolder::MakeUniqueName(DesiredName, UsedNames);
}

int32 FLexUIEditorTools::EnsureUniqueWidgetDisplayNames(ULexWidget* RootWidget, TArray<FString>* OutRenamedWidgets)
{
	if (!IsValid(RootWidget))
	{
		return 0;
	}

	TArray<ULexWidget*> Widgets;
	ULexWidget::CollectChildrenWidgets(RootWidget, Widgets);
	ULexUIPrefabHelperObject* ManagingHelper =
		ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(RootWidget);
	TSet<FName> UsedNames;
	int32 RenameCount = 0;
	for (ULexWidget* Widget : Widgets)
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
		const FString UniqueName = FLexUIEditorToolsHelperFunctionHolder::MakeUniqueName(OldName, UsedNames);
		UsedNames.Add(FName(*UniqueName));
		if (OldName.Equals(UniqueName, ESearchCase::CaseSensitive))
		{
			continue;
		}

		Widget->Modify();
		FLexUIUtils::ChangePropertyWithNotify(Widget, ULexWidget::GetPropertyName_DisplayName(), [Widget, UniqueName]()
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

TArray<ULexWidget*> FLexUIEditorTools::GetRootWidgetListFromSelection(const TArray<ULexWidget*>& InSelectedWidgets)
{
	TArray<ULexWidget*> RootWidgetList;
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

void FLexUIEditorTools::CreateWidget(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(ULexWidget*)> Callback)
{
	CreateWidgetAndReturn(MoveTemp(GetSelectedWidgetFunction), MoveTemp(Name), VisualClass, MoveTemp(Callback));
}

ULexWidget* FLexUIEditorTools::CreateWidgetAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(ULexWidget*)> Callback)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	// Every one of these refusals used to be silent, so a palette double-click with nothing selected
	// looked like a broken panel rather than a missing parent.
	if (SelectedWidget == nullptr)
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Cannot create widget '%s': no parent widget is selected."), *Name);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Cannot create widget '%s': widget '%s' is not a valid parent."), *Name, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren())
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Widget '%s' cannot accept another child."), *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("CreateChildWidget_Transaction", "LexUI Child Widget"));
	ULexUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	ModifyForHierarchyChange(SelectedWidget);
	auto NewWidget = NewObject<ULexWidget>(SelectedWidget->GetOuter(), ULexWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
	if (IsValid(NewWidget))
	{
		NewWidget->Modify();
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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
			ULexUISelection::GetInstance(SelectedWidget->GetWorld())->SelectNone();
		} 
		if (VisualClass)
		{
			NewWidget->CreateNewVisual(VisualClass);
		}
		if (Callback)
		{
			Callback(NewWidget);
		}
		EnsureUniqueWidgetDisplayNames(FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(NewWidget));
		ULexUISelection::GetInstance(SelectedWidget->GetWorld())->SelectWidget(NewWidget);
	}
	return NewWidget;
}

void FLexUIEditorTools::CreateUIControls(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString InPrefabPath)
{
	CreateUIControlsAndReturn(MoveTemp(GetSelectedWidgetFunction), MoveTemp(InPrefabPath));
}

ULexWidget* FLexUIEditorTools::CreateUIControlsAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString InPrefabPath, TFunction<void(ULexWidget*)> Callback)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Cannot create prefab '%s': no parent widget is selected."), *InPrefabPath);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Cannot create prefab '%s': widget '%s' is not a valid parent."), *InPrefabPath, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren())
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Widget '%s' cannot accept another child prefab."), *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	auto Prefab = LoadObject<ULexUIPrefab>(NULL, *InPrefabPath);
	if (Prefab == nullptr)
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load control prefab error! Path:%s. Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InPrefabPath);
		return nullptr;
	}
	// Flattening happens before anything can look at the result, so the nesting guard has to run
	// ahead of the transaction rather than as a dialog on the way out.
	FText NestError;
	if (!CanNestPrefabUnderWidget(Prefab, SelectedWidget, NestError))
	{
		UE_LOG(LGUIEditor, Error, TEXT("Cannot create prefab '%s': %s"), *InPrefabPath, *NestError.ToString());
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("CreateUIControl_Transaction", "LexUI Create UI Control"));
	ULexUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	ModifyForHierarchyChange(SelectedWidget);
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		PrefabHelperObject->Modify();
		PrefabHelperObject->SetAnythingDirty();
	}
	auto CreatedWidget = Prefab->LoadPrefabInEditor(SelectedWidget->GetWorld()
		, SelectedWidget->GetOuter()
		, SelectedWidget);
	if (Callback)Callback(CreatedWidget);
	EnsureUniqueWidgetDisplayNames(FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(CreatedWidget));
	ULexUISelection::GetInstance(SelectedWidget->GetWorld())->SelectNone();
	ULexUISelection::GetInstance(SelectedWidget->GetWorld())->SelectWidget(CreatedWidget);
	return CreatedWidget;
}

bool FLexUIEditorTools::CanNestPrefabUnderWidget(ULexUIPrefab* InPrefab, ULexWidget* InParentWidget, FText& OutError)
{
	if (!IsValid(InPrefab))
	{
		OutError = LOCTEXT("Nest_MissingPrefab", "The prefab asset could not be loaded.");
		return false;
	}
	if (InPrefab->PrefabVersion <= (uint16)ELexUIPrefabVersion::OldVersion)
	{
		OutError = LOCTEXT("Nest_UnsupportOldPrefabVersion", "Target prefab's version is too old! Please make it newer: open the prefab and hit \"Save\" button.");
		return false;
	}
	// The prefab the parent belongs to is the one being edited. Putting it inside itself -- directly,
	// or through a prefab that already contains it -- is what makes Apply bake a second copy in, so
	// every repeat doubles the asset.
	auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(InParentWidget);
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

ULexWidget* FLexUIEditorTools::CreateSubPrefabAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString InPrefabPath, TFunction<void(ULexWidget*)> Callback)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Cannot add sub prefab '%s': no parent widget is selected."), *InPrefabPath);
		return nullptr;
	}
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Cannot add sub prefab '%s': widget '%s' is not a valid parent."), *InPrefabPath, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren())
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Widget '%s' cannot accept another child prefab."), *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	auto Prefab = LoadObject<ULexUIPrefab>(NULL, *InPrefabPath);
	if (Prefab == nullptr)
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load prefab error! Path:%s"), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InPrefabPath);
		return nullptr;
	}
	FText NestError;
	if (!CanNestPrefabUnderWidget(Prefab, SelectedWidget, NestError))
	{
		UE_LOG(LGUIEditor, Error, TEXT("Cannot add sub prefab '%s': %s"), *InPrefabPath, *NestError.ToString());
		return nullptr;
	}
	// Without a helper object there is nowhere to record the link, and recording it is the entire
	// difference between this and CreateUIControlsAndReturn.
	auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
	if (!IsValid(PrefabHelperObject))
	{
		UE_LOG(LGUIEditor, Error, TEXT("Cannot add sub prefab '%s': widget '%s' does not belong to a prefab."), *InPrefabPath, *SelectedWidget->GetDisplayName());
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("CreateSubPrefab_Transaction", "LexUI Create Sub Prefab"));
	auto World = SelectedWidget->GetWorld();
	ULexUISelection::GetInstance(World)->Modify();
	ModifyForHierarchyChange(SelectedWidget);
	PrefabHelperObject->Modify();
	PrefabHelperObject->SetAnythingDirty();
	TMap<FGuid, TObjectPtr<UObject>> SubPrefabMapGuidToObject;
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubSubPrefabMap;
	auto CreatedWidget = Prefab->LoadPrefabWithExistingObjects(World
		, SelectedWidget->GetOuter()
		, SelectedWidget
		, SubPrefabMapGuidToObject, SubSubPrefabMap);
	if (!IsValid(CreatedWidget))
	{
		UE_LOG(LGUIEditor, Error, TEXT("Sub prefab '%s' produced no root widget."), *InPrefabPath);
		return nullptr;
	}
	PrefabHelperObject->MakePrefabAsSubPrefab(Prefab, CreatedWidget, SubPrefabMapGuidToObject, {});
	if (Callback)Callback(CreatedWidget);
	EnsureUniqueWidgetDisplayNames(FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(CreatedWidget));
	ULexUISelection::GetInstance(World)->SelectNone();
	ULexUISelection::GetInstance(World)->SelectWidget(CreatedWidget);
	return CreatedWidget;
}

void FLexUIEditorTools::CreateRegisteredControl(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FName ControlName)
{
	CreateRegisteredControlAndReturn(MoveTemp(GetSelectedWidgetFunction), ControlName);
}

ULexWidget* FLexUIEditorTools::CreateRegisteredControlAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FName ControlName, TFunction<void(ULexWidget*)> Callback)
{
	const FLexUIControlDescriptor* Descriptor = FLexUIControlRegistry::Get().GetDescriptors().FindByPredicate([ControlName](const FLexUIControlDescriptor& Item)
	{
		return Item.Name == ControlName;
	});
	if (!Descriptor)
	{
		UE_LOG(LGUIEditor, Error, TEXT("Unknown registered control: %s"), *ControlName.ToString());
		return nullptr;
	}
	FText ValidationError;
	if (!FLexUIControlRegistry::Get().Validate(*Descriptor, ValidationError))
	{
		UE_LOG(LGUIEditor, Error, TEXT("Cannot create control '%s': %s"), *ControlName.ToString(), *ValidationError.ToString());
		return nullptr;
	}
	if (Descriptor->CreationKind == ELexUIControlCreationKind::Prefab)
	{
		return CreateUIControlsAndReturn(MoveTemp(GetSelectedWidgetFunction), Descriptor->PrefabPath, MoveTemp(Callback));
	}

	const FLexUIControlDescriptor Recipe = *Descriptor;
	// Name new widgets from the terse registry name ("CanvasPanel"), not the palette label — labels
	// now carry family prefixes ("UMG Canvas Panel") that would pollute hierarchy names and prefabs.
	return CreateWidgetAndReturn(MoveTemp(GetSelectedWidgetFunction), Recipe.Name.ToString(), Recipe.VisualClass.Get(),
		[Recipe, Callback = MoveTemp(Callback)](ULexWidget* InWidget) mutable
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
				if (ULexVisualBatchMesh* Visual = Cast<ULexVisualBatchMesh>(InWidget->GetVisual()))
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

void FLexUIEditorTools::DuplicateWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	auto RootWidgetList = FLexUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	TMap<ULexWidget*, int32> AdditionalChildrenByParent;
	for (ULexWidget* Widget : RootWidgetList)
	{
		if (IsValid(Widget) && IsValid(Widget->GetParent()))
		{
			++AdditionalChildrenByParent.FindOrAdd(Widget->GetParent());
		}
	}
	for (const TPair<ULexWidget*, int32>& Pair : AdditionalChildrenByParent)
	{
		if (!Pair.Key->CanAcceptAdditionalChildren(Pair.Value))
		{
			UE_LOG(LGUIEditor, Warning, TEXT("Widget '%s' cannot accept %d duplicated child widget(s)."),
				*Pair.Key->GetDisplayName(), Pair.Value);
			return;
		}
	}
	const FScopedTransaction Transaction(LOCTEXT("DuplicateWidget_Transaction", "LexUI Duplicate Widgets"));
	auto World = SelectedWidgets[0]->GetWorld();
	ULexUISelection::GetInstance(World)->Modify();
	ULexUISelection::GetInstance(World)->SelectNone();
	for (auto Widget : RootWidgetList)
	{
		Widget->GetOuter()->Modify();
		auto CopiedWidgetName = MakeUniqueWidgetDisplayName(Widget, Widget->GetDisplayName());
		ULexWidget* CopiedWidget = nullptr;
		auto Parent = Widget->GetParent();
		if (Parent)
		{
			Parent->Modify();
		}
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> DuplicatedSubPrefabMap;
		TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
		TMap<UObject*, FGuid> InMapObjectToGuid;
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			PrefabHelperObject->CleanupInvalidSubPrefab();//do cleanup before everything else
			PrefabHelperObject->Modify();
			struct LOCAL {
				static void CollectSubPrefabWidgets(ULexWidget* InWidget, const TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& InSubPrefabMap, TArray<ULexWidget*>& OutSubPrefabRootWidgets)
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
			TArray<ULexWidget*> SubPrefabRootWidgets;
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
						UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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
		FLexUIUtils::ChangePropertyWithNotify(CopiedWidget, ULexWidget::GetPropertyName_DisplayName(), [CopiedWidget, CopiedWidgetName]()
		{
			CopiedWidget->SetDisplayName(CopiedWidgetName);
		});
		EnsureUniqueWidgetDisplayNames(FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(CopiedWidget));
		ULexUISelection::GetInstance(World)->SelectWidget(CopiedWidget);
	}
	ULexUIManagerWorldSubsystem::RefreshAllUI();
}
void FLexUIEditorTools::CopyWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
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
	auto CopyWidgetList = FLexUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	CopiedWidgetPrefabList.Reset();
	for (auto Widget : CopyWidgetList)
	{
		auto Prefab = NewObject<ULexUIPrefab>();
		Prefab->AddToRoot();
		TMap<UObject*, FGuid> MapObjectToGuid;
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubPrefabMap;
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			SubPrefabMap = PrefabHelperObject->SubPrefabMap;

			if (PrefabHelperObject->CleanupInvalidSubPrefab())//do cleanup before everything else
			{
				PrefabHelperObject->Modify();
			}
			struct LOCAL {
				static void CollectSubPrefabWidgets(ULexWidget* InWidget, const TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& InSubPrefabMap, TArray<ULexWidget*>& OutSubPrefabRootWidgets)
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
			TArray<ULexWidget*> SubPrefabRootWidgets;
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
						UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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

		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> TempSubPrefabMap;
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
void FLexUIEditorTools::PasteWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)
	{
		UE_LOG(LGUIEditor, Error, TEXT("NothingSelected"));
		return;
	}
	auto ParentWidget = SelectedWidgets[0];
	auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ParentWidget);
	if (PrefabHelperObject == nullptr)return;
	int32 PasteCount = 0;
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		PasteCount += CopiedItem.Prefab.IsValid() ? 1 : 0;
	}
	if (!ParentWidget->CanAcceptAdditionalChildren(PasteCount))
	{
		UE_LOG(LGUIEditor, Warning, TEXT("Widget '%s' cannot accept %d pasted child widget(s)."),
			*ParentWidget->GetDisplayName(), PasteCount);
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("PasteWidget_Transaction", "LexUI Paste Widgets"));
	auto World = ParentWidget->GetWorld();
	ULexUISelection::GetInstance(World)->Modify();
	ModifyForHierarchyChange(ParentWidget);
	if (IsValid(PrefabHelperObject))PrefabHelperObject->Modify();
	ULexUISelection::GetInstance(World)->SelectNone();
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		if (CopiedItem.Prefab.IsValid())
		{
			TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
			TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> LoadedSubPrefabMap;
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
			FLexUIUtils::ChangePropertyWithNotify(CopiedWidget, ULexWidget::GetPropertyName_DisplayName(), [CopiedWidget, CopiedWidgetName]()
			{
				CopiedWidget->SetDisplayName(CopiedWidgetName);
			});
			EnsureUniqueWidgetDisplayNames(FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(CopiedWidget));
			PrefabHelperObject->SetAnythingDirty();
			ULexUISelection::GetInstance(World)->SelectWidget(CopiedWidget);
		}
		else
		{
			UE_LOG(LGUIEditor, Error, TEXT("Source copied widget is missing!"));
		}
	}
	ULexUIManagerWorldSubsystem::RefreshAllUI();
}
void FLexUIEditorTools::DeleteWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	auto RootWidgetList = FLexUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	const FScopedTransaction Transaction(LOCTEXT("DestroyWidget_Transaction", "LexUI Destroy Widgets"));
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
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
		{
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetAnythingDirty();
			TArray<ULexWidget*> ChildrenWidgets;
			ULexWidget::CollectChildrenWidgets(Widget, ChildrenWidgets);
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
void FLexUIEditorTools::CutWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	CopyWidgets(GetSelectedWidgetArrayFunction);
	DeleteWidgets(GetSelectedWidgetArrayFunction);
}

bool FLexUIEditorTools::CanDuplicateWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() <= 0)return false;
	TMap<ULexWidget*, int32> AdditionalChildrenByParent;
	for (ULexWidget* Widget : GetRootWidgetListFromSelection(SelectedWidgets))
	{
		if (IsValid(Widget) && IsValid(Widget->GetParent()))
		{
			++AdditionalChildrenByParent.FindOrAdd(Widget->GetParent());
		}
	}
	for (const TPair<ULexWidget*, int32>& Pair : AdditionalChildrenByParent)
	{
		if (!Pair.Key->CanAcceptAdditionalChildren(Pair.Value))return false;
	}
	return true;
}
bool FLexUIEditorTools::CanCopyWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() <= 0)return false;
	return true;
}
bool FLexUIEditorTools::CanPasteWidget(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	if (FLexUIEditorTools::CopiedWidgetPrefabList.Num() == 0)return false;
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!FLexUIEditorTools::IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
	int32 PasteCount = 0;
	for (const FCopiedWidgetPrefab& CopiedItem : CopiedWidgetPrefabList)
	{
		PasteCount += CopiedItem.Prefab.IsValid() ? 1 : 0;
	}
	if (!SelectedWidget->CanAcceptAdditionalChildren(PasteCount))return false;
	return true;
}
bool FLexUIEditorTools::CanCutWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	return CanDeleteWidget(GetSelectedWidgetArrayFunction);
}
bool FLexUIEditorTools::CanDeleteWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction)
{
	auto SelectedWidgets = GetSelectedWidgetArrayFunction();
	if (SelectedWidgets.Num() == 0)return false;
	for (auto Widget : SelectedWidgets)
	{
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
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

bool FLexUIEditorTools::HaveValidCopiedWidgets()
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

bool FLexUIEditorTools::CanCreatePrefab(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
	if (SelectedWidget->HasAnyFlags(EObjectFlags::RF_Transient))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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
FString FLexUIEditorTools::PrevSavePrefabFolder = TEXT("");
void FLexUIEditorTools::CreatePrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction)//@todo: make some referenced parameter as override parameter(eg: Widget parameter reference other Widget that is not belongs to prefab hierarchy)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return;
	auto OldPrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
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
				auto OutPrefab = NewObject<ULexUIPrefab>(package, ULexUIPrefab::StaticClass(), *fileName, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
				FAssetRegistryModule::AssetCreated(OutPrefab);

				auto PrefabHelperObjectWhichManageThisWidget = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
				check(PrefabHelperObjectWhichManageThisWidget != nullptr)
				{
					struct LOCAL
					{
						static auto Make_MapGuidFromParentToSub(const TMap<UObject*, FGuid>& InNewParentMapObjectToGuid, ULexUIPrefabHelperObject* InPrefabHelperObject, const FLexUISubPrefabData& InOriginSubPrefabData)
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
						static void CollectSubPrefab(ULexWidget* InWidget, TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& InOutSubPrefabMap, ULexUIPrefabHelperObject* InPrefabHelperObject, const TMap<UObject*, FGuid>& InMapObjectToGuid)
						{
							if (InPrefabHelperObject->IsWidgetBelongsToSubPrefab(InWidget))
							{
								auto OriginSubPrefabData = InPrefabHelperObject->GetSubPrefabData(InWidget);
								FLexUISubPrefabData SubPrefabData;
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
					TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubPrefabMap;
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

void FLexUIEditorTools::RefreshLoadedPrefab()
{
	for (TObjectIterator<ULexUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CheckPrefabVersion();
	}
	for (TObjectIterator<ULexUIPrefabPresenterComponent> Itr; Itr; ++Itr)
	{
		if (Itr->GetWorld())
		{
			Itr->CheckPrefabVersion();
		}
	}
}

void FLexUIEditorTools::RefreshOpenedPrefabEditor(ULexUIPrefab* InPrefab)
{
	if (auto PrefabEditor = FLexUIPrefabEditor::GetEditorForPrefabIfValid(InPrefab))//refresh opened prefab
	{
		if (PrefabEditor->GetAnythingDirty())
		{
			auto Msg = LOCTEXT("PrefabEditorChangedDataWillLose", "Prefab editor will automaticallly refresh changed prefab, but detect some data changed in prefab editor, refresh the prefab editor will lose these data, do you want to continue?");
			auto Result = FMessageDialog::Open(EAppMsgType::YesNo, Msg);
			if (Result == EAppReturnType::Yes)
			{
				//reopen this prefab editor
				PrefabEditor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				AssetEditorSubsystem->OpenEditorForAsset(InPrefab);
			}
		}
		else
		{
			//reopen this prefab editor
			PrefabEditor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(InPrefab);
		}
	}
}

void FLexUIEditorTools::RefreshOnSubPrefabChange(ULexUIPrefab* InSubPrefab)
{
	auto AllPrefabs = GetAllPrefabArray();

	struct Local
	{
	public:
		static void RefreshAllPrefabsOnSubPrefabChange(
			const TArray<ULexUIPrefab*>& InPrefabs,
			ULexUIPrefab* InSubPrefab,
			TSet<const ULexUIPrefab*>& VisitedPrefabs)
		{
			if (!IsValid(InSubPrefab) || VisitedPrefabs.Contains(InSubPrefab))return;
			VisitedPrefabs.Add(InSubPrefab);
			for (auto& Prefab : InPrefabs)
			{
				if (Prefab->IsPrefabBelongsToThisSubPrefab(InSubPrefab, false))
				{
					//check if is opened by prefab editor
					if (auto PrefabEditor = FLexUIPrefabEditor::GetEditorForPrefabIfValid(Prefab))//refresh opened prefab
					{
						PrefabEditor->RefreshOnSubPrefabDirty(InSubPrefab);
					}
					RefreshAllPrefabsOnSubPrefabChange(InPrefabs, Prefab, VisitedPrefabs);
				}
			}
		}
	};

	TSet<const ULexUIPrefab*> VisitedPrefabs;
	Local::RefreshAllPrefabsOnSubPrefabChange(AllPrefabs, InSubPrefab, VisitedPrefabs);
}

TArray<ULexUIPrefab*> FLexUIEditorTools::GetAllPrefabArray()
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

	TArray<ULexUIPrefab*> AllPrefabs;
	auto PrefabClassName = ULexUIPrefab::StaticClass()->GetClassPathName();
	// Ensure all assets are loaded
	for (const FAssetData& Asset : ScriptAssetList)
	{
		// Gets the loaded asset, loads it if necessary
		if (Asset.AssetClassPath == PrefabClassName)
		{
			auto AssetObject = Asset.GetAsset();
			if (auto Prefab = Cast<ULexUIPrefab>(AssetObject))
			{
				Prefab->MakeAgentObjectsInPreviewWorld();
				AllPrefabs.Add(Prefab);
			}
		}
	}
#else
	TArray<ULexUIPrefab*> AllPrefabs;
#endif
	//collect prefabs that are not saved to disc yet
	for (TObjectIterator<ULexUIPrefab> Itr; Itr; ++Itr)
	{
		if (!AllPrefabs.Contains(*Itr))
		{
			AllPrefabs.Add(*Itr);
		}
	}
	return AllPrefabs;
}

bool FLexUIEditorTools::CanUnpackWidgetForPrefab(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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
void FLexUIEditorTools::UnpackPrefab(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{	
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return;
	const FScopedTransaction Transaction(LOCTEXT("UnpackPrefab_Transaction", "LexUI UnpackPrefab"));
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
	{
		SelectedWidget->GetWorld()->Modify();
		check(PrefabHelperObject->SubPrefabMap.Contains(SelectedWidget) || PrefabHelperObject->MissingPrefab.Contains(SelectedWidget));//should already filtered by menu
		PrefabHelperObject->Modify();
		PrefabHelperObject->RemoveSubPrefabByRootWidget(SelectedWidget);//the SelectedWidget must be root Widget, should already filtered by menu
	}
	CleanupPrefabs();
}

void FLexUIEditorTools::SelectPrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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
bool FLexUIEditorTools::CanBrowsePrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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

void FLexUIEditorTools::OpenPrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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

bool FLexUIEditorTools::CanCheckPrefabOverrideParameter(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
	if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
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

bool FLexUIEditorTools::CanCreateWidget(TFunction<ULexWidget*()> GetSelectedWidgetFunction)
{
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
	return true;
}

void FLexUIEditorTools::CleanupPrefabs()
{
	for (TObjectIterator<ULexUIPrefabHelperObject> Itr; Itr; ++Itr)
	{
		Itr->CleanupInvalidSubPrefab();
	}
}

bool FLexUIEditorTools::IsWidgetCompatibleWithLexUIToolsMenu(ULexWidget* InWidget)
{
	if (!FLexUIPrefabEditor::WidgetIsRootAgent(InWidget))
	{
		return true;
	}
	return false;
}


#undef LOCTEXT_NAMESPACE
