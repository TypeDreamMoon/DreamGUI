// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIEditorTools.h"
#include "LexUIControlRegistry.h"
#include "Core/LexUIManager.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
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

TMap<FString, TWeakObjectPtr<ULexUIPrefab>> FLexUIEditorTools::CopiedWidgetPrefabMap;

FString FLexUIEditorTools::LexUIPresetPrefabPath = TEXT("/LGUI/Prefabs/");

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
	TSet<FName> UsedNames;
	int32 RenameCount = 0;
	for (ULexWidget* Widget : Widgets)
	{
		if (!IsValid(Widget))
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
	if (SelectedWidget == nullptr)return nullptr;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return nullptr;
	const FScopedTransaction Transaction(LOCTEXT("CreateChildWidget_Transaction", "LexUI Child Widget"));
	ULexUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	auto NewWidget = NewObject<ULexWidget>(SelectedWidget->GetOuter(), ULexWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
	if (IsValid(NewWidget))
	{
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
	if (SelectedWidget == nullptr)return nullptr;
	if (!IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return nullptr;
	const FScopedTransaction Transaction(LOCTEXT("CreateUIControl_Transaction", "LexUI Create UI Control"));
	ULexUISelection::GetInstance(SelectedWidget->GetWorld())->Modify();
	ULexWidget* CreatedWidget = nullptr;
	if (auto Prefab = LoadObject<ULexUIPrefab>(NULL, *InPrefabPath))
	{
		if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget))
		{
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetAnythingDirty();
		}
		CreatedWidget = Prefab->LoadPrefabInEditor(SelectedWidget->GetWorld()
			, SelectedWidget->GetOuter()
			, SelectedWidget);
		if (Callback)Callback(CreatedWidget);
		EnsureUniqueWidgetDisplayNames(FLexUIEditorToolsHelperFunctionHolder::GetNamingRoot(CreatedWidget));
		ULexUISelection::GetInstance(SelectedWidget->GetWorld())->SelectNone();
		ULexUISelection::GetInstance(SelectedWidget->GetWorld())->SelectWidget(CreatedWidget);
	}
	else
	{
		UE_LOG(LGUIEditor, Error, TEXT("[%s].%d Load control prefab error! Path:%s. Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), ANSI_TO_TCHAR(__FUNCDNAME__), __LINE__, *InPrefabPath);
	}
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
	return CreateWidgetAndReturn(MoveTemp(GetSelectedWidgetFunction), Recipe.DisplayName.ToString(), Recipe.VisualClass.Get(),
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
	for (auto KeyValuePair : CopiedWidgetPrefabMap)
	{
		KeyValuePair.Value->RemoveFromRoot();
		KeyValuePair.Value->ConditionalBeginDestroy();
	}
	auto CopyWidgetList = FLexUIEditorTools::GetRootWidgetListFromSelection(SelectedWidgets);
	CopiedWidgetPrefabMap.Reset();
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
		CopiedWidgetPrefabMap.Add(Widget->GetDisplayName(), Prefab);
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

	const FScopedTransaction Transaction(LOCTEXT("PasteWidget_Transaction", "LexUI Paste Widgets"));
	auto World = ParentWidget->GetWorld();
	ULexUISelection::GetInstance(World)->Modify();
	ULexUISelection::GetInstance(World)->SelectNone();
	for (auto KeyValuePair : CopiedWidgetPrefabMap)
	{
		if (KeyValuePair.Value.IsValid())
		{
			TMap<FGuid, TObjectPtr<UObject>> OutMapGuidToObject;
			TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> LoadedSubPrefabMap;
			auto CopiedWidgetName = MakeUniqueWidgetDisplayName(ParentWidget, KeyValuePair.Key);
			auto CopiedWidget = KeyValuePair.Value->LoadPrefabInEditor(ParentWidget->GetWorld(), ParentWidget->GetOuter(), ParentWidget, LoadedSubPrefabMap, OutMapGuidToObject, false);
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
	if (FLexUIEditorTools::CopiedWidgetPrefabMap.Num() == 0)return false;
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (SelectedWidget == nullptr)return false;
	if (!FLexUIEditorTools::IsWidgetCompatibleWithLexUIToolsMenu(SelectedWidget))return false;
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
	if (CopiedWidgetPrefabMap.Num() == 0)return false;
	for (auto KeyValuePair : CopiedWidgetPrefabMap)
	{
		if (!KeyValuePair.Value.IsValid())
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
		static void RefreshAllPrefabsOnSubPrefabChange(const TArray<ULexUIPrefab*>& InPrefabs, ULexUIPrefab* InSubPrefab)
		{
			for (auto& Prefab : InPrefabs)
			{
				if (Prefab->IsPrefabBelongsToThisSubPrefab(InSubPrefab, false))
				{
					//check if is opened by prefab editor
					if (auto PrefabEditor = FLexUIPrefabEditor::GetEditorForPrefabIfValid(Prefab))//refresh opened prefab
					{
						PrefabEditor->RefreshOnSubPrefabDirty(InSubPrefab);
					}
					RefreshAllPrefabsOnSubPrefabChange(InPrefabs, Prefab);
				}
			}
		}
	};

	Local::RefreshAllPrefabsOnSubPrefabChange(AllPrefabs, InSubPrefab);
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
