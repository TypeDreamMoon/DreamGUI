// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/UIRecyclableScrollView.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "UIListView.generated.h"

class UUIListView;

UENUM(BlueprintType)
enum class EUIListSelectionMode : uint8
{
	None,
	Single,
	SingleToggle,
	Multi,
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIListEntry : public UDreamUIBehaviour, public IUIRecyclableScrollViewCell, public IDreamPointerClickInterface
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ListEntry")
	TObjectPtr<UObject> Item = nullptr;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ListEntry")
	int32 ItemIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ListEntry")
	int32 TreeDepth = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ListEntry")
	bool bSelected = false;
	TWeakObjectPtr<UUIListView> OwnerList;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData) override;

public:
	void Assign(UUIListView* InOwner, UObject* InItem, int32 InIndex, int32 InTreeDepth, bool bInSelected);
	UFUNCTION(BlueprintImplementableEvent, Category = "ListEntry", meta = (DisplayName = "On List Item Assigned"))
	void ReceiveOnListItemAssigned(UObject* InItem, int32 InIndex, bool bInSelected);
	UFUNCTION(BlueprintImplementableEvent, Category = "ListEntry", meta = (DisplayName = "On Selection Changed"))
	void ReceiveOnSelectionChanged(bool bInSelected);
	UFUNCTION(BlueprintPure, Category = "ListEntry") UObject* GetItem()const { return Item; }
	UFUNCTION(BlueprintPure, Category = "ListEntry") int32 GetItemIndex()const { return ItemIndex; }
	UFUNCTION(BlueprintPure, Category = "ListEntry") int32 GetTreeDepth()const { return TreeDepth; }
	UFUNCTION(BlueprintPure, Category = "ListEntry") bool IsSelected()const { return bSelected; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FUIListItemEvent, UObject*, Item, UUIListEntry*, Entry);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FUIListSelectionChangedEvent, UObject*, Item, bool, bSelected);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIListView : public UUIRecyclableScrollView, public IUIRecyclableScrollViewDataSource
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ListView", meta = (AllowPrivateAccess = true))
	TArray<TObjectPtr<UObject>> Items;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ListView")
	EUIListSelectionMode SelectionMode = EUIListSelectionMode::Single;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "ListView")
	TSet<TObjectPtr<UObject>> SelectedItems;
	UPROPERTY(BlueprintAssignable, Category = "ListView")
	FUIListItemEvent OnEntryGenerated;
	UPROPERTY(BlueprintAssignable, Category = "ListView")
	FUIListItemEvent OnEntryReleased;
	UPROPERTY(BlueprintAssignable, Category = "ListView")
	FUIListItemEvent OnItemClicked;
	UPROPERTY(BlueprintAssignable, Category = "ListView")
	FUIListSelectionChangedEvent OnSelectionChanged;
	virtual void Awake() override;
	virtual int GetItemCount_Implementation() override;
	virtual void InitOnCreate_Implementation(UDreamUIBehaviour* Component) override;
	virtual void BeforeSetCell_Implementation() override;
	virtual void SetCell_Implementation(UDreamUIBehaviour* Component, int Index) override;
	virtual void AfterSetCell_Implementation() override;
	virtual int32 GetEntryDepth(int32 Index) const { return 0; }
	UUIListEntry* ResolveEntry(UDreamUIBehaviour* Component) const;
	void RefreshVisibleSelection();

public:
	UFUNCTION(BlueprintCallable, Category = "ListView")
	virtual void SetListItems(const TArray<UObject*>& InItems);
	UFUNCTION(BlueprintCallable, Category = "ListView")
	void AddItem(UObject* Item);
	UFUNCTION(BlueprintCallable, Category = "ListView")
	bool RemoveItem(UObject* Item);
	UFUNCTION(BlueprintCallable, Category = "ListView")
	void ClearListItems();
	UFUNCTION(BlueprintPure, Category = "ListView")
	TArray<UObject*> GetListItems()const;
	UFUNCTION(BlueprintCallable, Category = "ListView")
	void SetItemSelection(UObject* Item, bool bSelected, bool bClearOthers = true);
	UFUNCTION(BlueprintCallable, Category = "ListView")
	void ClearSelection();
	UFUNCTION(BlueprintPure, Category = "ListView")
	bool IsItemSelected(UObject* Item)const { return SelectedItems.Contains(Item); }
	UFUNCTION(BlueprintPure, Category = "ListView")
	TArray<UObject*> GetSelectedItems()const;
	void HandleEntryClicked(UUIListEntry* Entry);
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUITileView : public UUIListView
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TileView", meta = (ClampMin = "1"))
	int32 NumColumns = 4;
	virtual void Awake() override;
};

UINTERFACE(Blueprintable)
class DREAMGUI_API UUITreeViewItem : public UInterface
{
	GENERATED_BODY()
};

class DREAMGUI_API IUITreeViewItem
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "TreeView")
	void GetTreeChildren(TArray<UObject*>& OutChildren) const;
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUITreeView : public UUIListView
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TreeView")
	TArray<TObjectPtr<UObject>> RootItems;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "TreeView")
	TSet<TObjectPtr<UObject>> ExpandedItems;
	UPROPERTY(Transient)
	TArray<int32> ItemDepths;
	virtual int32 GetEntryDepth(int32 Index) const override;
	void RebuildTree();
	void AppendItemRecursive(UObject* Item, int32 Depth, TSet<UObject*>& Visited);

public:
	UFUNCTION(BlueprintCallable, Category = "TreeView")
	void SetRootItems(const TArray<UObject*>& InRootItems);
	UFUNCTION(BlueprintCallable, Category = "TreeView")
	void SetItemExpansion(UObject* Item, bool bExpanded);
	UFUNCTION(BlueprintPure, Category = "TreeView")
	bool IsItemExpanded(UObject* Item)const { return ExpandedItems.Contains(Item); }
	UFUNCTION(BlueprintCallable, Category = "TreeView")
	void ToggleItemExpansion(UObject* Item);
};
