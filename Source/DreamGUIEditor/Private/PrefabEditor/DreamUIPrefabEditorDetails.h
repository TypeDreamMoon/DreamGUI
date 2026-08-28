// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#pragma once

class FDreamUIPrefabEditor;
class UDreamWidget;
class SDreamWidgetComponentEditor;

/**
 * 
 */
class SDreamUIPrefabEditorDetails : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPrefabEditorDetails)
    {
    }

    SLATE_END_ARGS()

    /** Widget constructor */
	void Construct(const FArguments& Args, UWorld* InWorld);
	void Refresh();

	virtual ~SDreamUIPrefabEditorDetails();
private:
	UDreamWidget* GetSelectedWidgetContext() const;
	void OnEditorSelectionChanged();
	void OnComponentSelectionChanged(const TArray<TWeakObjectPtr<class UDreamUIBehaviour>>& SelectedComponents);

	TWeakPtr<FDreamUIPrefabEditor> PrefabEditorPtr;
	TWeakObjectPtr<UWorld> World;

	bool IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent);
	bool IsEditorAllowEditing()const;

	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<class SDreamWidgetComponentEditor> ComponentEditor;
	TWeakObjectPtr<UDreamWidget> CachedWidget;
	bool bIsSelectFromDreamUIEditor = false;
	bool bIsSelectFromComponentList = false;
};
