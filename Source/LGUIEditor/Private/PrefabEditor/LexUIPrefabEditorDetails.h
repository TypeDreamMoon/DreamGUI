// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SSubobjectEditor.h"
#pragma once

class FLexUIPrefabEditor;
class ULexWidget;

/**
 * 
 */
class SLexUIPrefabEditorDetails : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabEditorDetails)
    {
    }

    SLATE_END_ARGS()

    /** Widget constructor */
    void Construct(const FArguments& Args, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor);

	virtual ~SLexUIPrefabEditorDetails();
private:
	UObject* GetActorContextAsObject() const;
	void OnEditorSelectionChanged();
	void OnSubObjectSelectionChanged(const TArray<FSubobjectEditorTreeNodePtrType>& SelectedNodes);
	void OnSubObjectItemDoubleClicked(const FSubobjectEditorTreeNodePtrType ClickedNode);

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;

	bool IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent);
	bool IsPrefabButtonEnable()const;
	FOptionalSize GetPrefabButtonHeight()const;
	EVisibility GetPrefabButtonVisibility()const;
	bool IsEditorAllowEditing()const;

	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<class SBox> ComponentsBox;
	TSharedPtr<class SSubobjectEditor> SubobjectEditor;
	TSharedPtr<class SLexUIPrefabOverrideDataViewer> PrefabOverrideDataViewer;
	TWeakObjectPtr<ULexWidget> CachedActor;
	bool bIsSelectFromLexUIEditor = false;
	bool bIsSelectFromDetails = false;
};
